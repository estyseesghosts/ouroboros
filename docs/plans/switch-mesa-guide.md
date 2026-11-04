# Switch Optimization Guide (eidos, SDL + switch-mesa)

How to make the Switch port of eidos (native Pokémon Black/White from the aletheia decomp) run well on Switch hardware. Read this before changing the Switch host, the GL renderers, the worker pools, or anything that runs once per DS frame.

- Target: original Switch hardware (v1, v2, Lite, OLED) under Atmosphère. Switch 2 is out of scope.
- Stack: devkitA64, libnx, devkitPro's SDL port, switch-mesa (OpenGL on the Switch GPU through Mesa's nouveau driver), EGL.
- **MUST** = required. **SHOULD** = default; break it only with a comment saying why.
- Rule IDs use the prefix `NX` (e.g. `NXGL-3`). Cite them in commits and reviews.
- GL and SDL behaviour can differ between switch-mesa and SDL releases. Log `GL_VERSION`, `GL_RENDERER` and the SDL version at startup, and check claims here against your build when something doesn't match.

---

## 0. The rules that matter most

1. **Run in application mode** (hold R while launching a game). Applet (Album) mode has a small fraction of the memory. Detect it and refuse to start, with a message.
2. **Budget for handheld mode.** The GPU runs at roughly 40% of its docked clock, and memory bandwidth is low in both modes.
3. **GL calls are expensive on the CPU.** Mesa's driver does a lot of work per call, on a slow CPU. Fewer draws, fewer state changes, fewer uploads.
4. **Never make the CPU wait for the GPU** in a frame: no synchronous readbacks, no re-specifying textures in use, no `glFinish`.
5. **Shaders compile at runtime and it is slow.** Compile and link every program at startup or behind a loading screen, never on first use mid-game.
6. **Three cores.** The app gets cores 0–2; core 3 is the system's. Size every worker pool for that.
7. **The game's clock is the DS frame (59.83 Hz).** Logic runs every frame; drawing can give.
8. **Handle docking.** The output size changes from 1280×720 to 1920×1080 while the game runs.
9. **Measure on hardware, in handheld mode.** Emulators don't show Mesa's CPU cost or real memory limits.

---

## 1. Hardware at a glance

| Part | Switch | What it means for eidos |
| --- | --- | --- |
| CPU | 4× Cortex-A57 (ARMv8-A, 64-bit) at 1020 MHz, NEON | About a third of a modern PC core per thread. The CPU 2D compositor and the GL driver compete for it. |
| Cores for the app | 0, 1, 2 (core 3: system) | Game on one, GL thread on one, one left for workers. |
| Caches | 32 KB L1 data per core, 2 MB shared L2, 64-byte lines | Row-band compositing must keep per-band data small. |
| RAM | 4 GB shared by CPU and GPU; games get about 3.2 GB; applet mode much less | Plenty in application mode, but no swap and no lazy commit. |
| GPU | NVIDIA Maxwell GM20B, 256 cores; ~307 MHz handheld, 768 MHz docked | Fine for DS-scale 3D; full-screen passes and MSAA are what cost. |
| Bandwidth | ~21 GB/s handheld, 25.6 GB/s docked, shared with the CPU | Uploads, readbacks and overdraw are the limit, not shader math. |
| Screen | 1280×720 handheld; up to 1920×1080 docked | 3D internal resolution has to follow the mode. |

---

## 2. Startup and system integration

### 2.1 Application mode

- **NXSYS-1 MUST** Check the applet type before any big allocation:

```c
static int in_application_mode(void)
{
    AppletType t = appletGetAppletType();
    return t == AppletType_Application || t == AppletType_SystemApplication;
}
```

If not, show one screen ("Launch eidos by holding R while starting any game") and exit cleanly.

- **NXSYS-2 MUST** Log total and used memory at startup (`svcGetInfo` with the total/used memory info types) and the GL strings.

### 2.2 Main loop and the HOME menu

- **NXSYS-3 MUST** One thread owns `appletMainLoop()` (or SDL's event pump, which calls into applet handling), once per frame. When it reports a close, flush the save and leave in order.
- **NXSYS-4 MUST** Sleep and the HOME menu pause the process for an unknown time. When the frame loop resumes, it must not try to catch up the missed frames: start a fresh pacing grid.
- **NXSYS-5 SHOULD** While in the background (focus lost), stop drawing and pause audio. The system takes GPU time away anyway.

### 2.3 Files

- **NXSYS-6 MUST** Data root and profile live on the SD card (`sdmc:/switch/eidos/` or a similar folder). Extraction from the ROM happens there on first run, with a progress screen; it's slow on the SD card and must not look like a hang.
- **NXSYS-7 MUST** Saves: write to a temporary file, close it, then rename over the old file. A crash or power-off mid-write must not destroy the save.
- **NXSYS-8 SHOULD** Read data in large blocks (64 KB or more) and keep frequently read archives open. Many small reads are slow on the SD card.

---

## 3. Memory

The Switch has no swap and libnx has no `mmap` with lazy commit. Memory you reserve is memory you use.

- **NXMEM-1 MUST** Any host code that reserves a large block "uncommitted" (the DS address space, fiber stacks, a wasm module's memory) must, on Switch, either allocate exactly what it needs or use libnx's virtual memory API. Don't assume a 1 GB reservation is free.
- **NXMEM-2 MUST** Fixed-address mappings (an identity mapping of DS main RAM at host `0x02000000`) are not available in a Switch process's address space. The movable-block fallback is the Switch path; make sure it is the one built and tested.
- **NXMEM-3 MUST** Fiber and thread stacks are sized for what they use. Many fibers × 16 MB each is real memory on Switch. Measure the deepest stack in a long session, then set the size with margin (e.g. 2× the measured peak).
- **NXMEM-4 MUST** If the game runs as a translated wasm32 module (wasm2c), its memory can't use the guard-page scheme (that needs an 8 GB reservation). Use wasm2c's explicit bounds checks, or allocate the module's maximum memory up front. Measure the bounds-check cost; it's per memory access.
- **NXMEM-5 SHOULD** Decoded-texture caches and snapshot copies have a size cap and evict least-recently-used entries. Unbounded caches grow over a long session.
- **NXMEM-6 SHOULD** Log free memory on every map change during testing. It must not trend down.

---

## 4. CPU

### 4.1 Build

- **NXCPU-1 MUST** Build with devkitA64's Switch flags (the toolchain file sets them) and `-O2` for everything, including large generated sources. A file left at `-O0` can cost milliseconds per frame.
- **NXCPU-2 SHOULD** Try `-O3` and LTO on the hot code (2D compositor, 3D rasteriser, mixer, translated module) and keep them only if a measurement shows a win.
- **NXCPU-3 SHOULD** Let the compiler vectorise simple per-pixel loops (NEON). Check the generated code once for the compositor's inner loop; restructure the loop (plain arrays, no aliasing, fixed widths) before writing intrinsics.

### 4.2 Threads and cores

| Thread | Core | Does |
| --- | --- | --- |
| Game (all game "fibers" or SDK threads, one running at a time) | 0 | game logic, the DS frame |
| GL thread (owns the context) | 1 | uploads, draws, present |
| Workers (2D row bands, CPU 3D fallback, decode) | 2 (+ 0/1 when idle) | parallel compositing |
| Audio | SDL's audio thread | mixing into SDL's callback/stream |

- **NXTHR-1 MUST** Worker pools size themselves to the cores actually available. A pool sized for a 6–8 core PC (e.g. 7 workers) on Switch puts several busy threads per core and makes everything slower.
- **NXTHR-2 SHOULD** On Switch, 2D row bands use at most 2 workers, plus the calling thread. Measure 1, 2 and 3; more bands than cores never helps.
- **NXTHR-3 MUST** Never spin-wait. Threads at the same priority on the same core don't share time automatically on Horizon (except at the special priority 0x3B). A spinning thread starves the one it waits for. Use condition variables, events or semaphores.
- **NXTHR-4 SHOULD** Pin threads to cores where the threading API allows it (libnx `threadCreate` takes a core id; SDL threads can be pinned with libnx calls from inside the thread). Unpinned threads may pile onto one core.
- **NXTHR-5 MUST** The audio thread runs at a higher priority than the game and workers. It must never wait on the game's lock for long; mixing takes its own lock.

### 4.3 Hot paths

- **NXCPU-4 SHOULD** Keep per-band and per-line working sets small (inside L1: 32 KB). Decode tiles once and cache them.
- **NXCPU-5 SHOULD** Skip work that didn't change: screens whose registers, VRAM, palette and OAM are unchanged keep last frame's image.
- **NXCPU-6 MUST** No per-frame `malloc`/`free` in the frame loop. Allocate buffers once and reuse them.
- **NXCPU-7 SHOULD** Profile with `armGetSystemTick()` deltas per stage, averaged and logged once per second.

---

## 5. OpenGL on switch-mesa

### 5.1 Context

- **NXGL-1 MUST** Create the context once, on the thread that will own it, and keep every GL call on that thread. Mesa contexts are not shared between threads here.
- **NXGL-2 MUST** Request the profile the renderer's shaders are written for (eidos's desktop renderer uses GLSL `#version 330 core`) and check what you got. If the context only offers GLES, use the same translation path the mobile hosts use.
- **NXGL-3 MUST** Log `GL_VERSION`, `GL_RENDERER` and `GL_SHADING_LANGUAGE_VERSION` at startup.
- **NXGL-4 SHOULD** Turn GL error checking (`glGetError` after each call) off in release builds. It's a driver round trip each time.

### 5.2 Shaders

Mesa compiles GLSL on the device at runtime: parse, optimise and generate Maxwell code, on the A57. A complex program can take tens to hundreds of milliseconds.

- **NXGL-5 MUST** Compile and link every program at startup, behind the loading screen. No program is created the first time a scene needs it.
- **NXGL-6 MUST** No shader variants generated per scene or per material at run time. Build a fixed set at startup and select between them with uniforms.
- **NXGL-7 SHOULD** Check whether your switch-mesa build has a working shader disk cache (and where it writes). If it does, point it at the SD card's eidos folder so later launches start faster. If not, budget startup time for compiling all programs.
- **NXGL-8 SHOULD** Keep fragment shaders short. A full-screen pass at 1080p docked is 2 million fragments; at handheld clocks every instruction counts.

### 5.3 Uploads

The DS screens are composited on the CPU and uploaded every frame (two 256×192 images per 3D screen, plus 2D-only screens). Decoded 3D textures are uploaded when their texels change.

- **NXGL-9 MUST** Never re-specify a texture's storage each frame (`glTexImage2D` with a new size/format). Allocate once (`glTexImage2D` or `glTexStorage2D`), then update with `glTexSubImage2D`.
- **NXGL-10 MUST** Don't update a texture the GPU may still be reading from the previous frame. Keep two (or three) copies of each per-frame texture and rotate, or update through a pixel buffer object.
- **NXGL-11 SHOULD** Upload in the format the driver stores natively (RGBA8, 4-byte aligned rows). A format conversion inside the driver runs on the CPU.
- **NXGL-12 MUST** Skip uploads when the content didn't change (a hash or dirty flag per texture). Upload-only-on-change already exists on desktop; keep it on Switch.
- **NXGL-13 SHOULD** Combine small per-frame images into one texture (an atlas of the screen images) to reduce upload calls.

### 5.4 Readbacks

- **NXGL-14 MUST** No synchronous `glReadPixels` in the frame. It stalls the CPU until the GPU finishes everything queued.
- **NXGL-15 MUST** When the game needs the 3D frame back (display capture), read it asynchronously: into a pixel buffer object, consumed one frame later, as the desktop path already does where possible. Scenes that need the capture in the same frame fall back to the CPU rasteriser at DS size.

### 5.5 Draws and state

- **NXGL-16 MUST** Batch the 3D snapshot into as few draws as possible: one vertex buffer per frame, sorted by texture and state, one draw per run.
- **NXGL-17 MUST** Stream per-frame vertices with buffer orphaning (`glBufferData(..., NULL, ...)` then `glBufferSubData`) or a ring buffer with fences. Never write into a buffer range the GPU may still read.
- **NXGL-18 SHOULD** Change state only when it differs from the current state. Track bound program, textures, blend and depth state on the CPU and skip redundant calls.
- **NXGL-19 SHOULD** Keep uniforms that change per frame in one uniform buffer, updated once.

### 5.6 Resolution and passes

| Setting | Handheld | Docked |
| --- | --- | --- |
| Output | 1280×720 | 1920×1080 |
| 3D internal resolution | 3× DS (768×576) default; 4× max | 4–5× DS default |
| MSAA | Off | Off by default; 2× if measured at full speed |
| Edge marking | On (it's a short pass) | On |
| Presenter filter | sharp-bilinear (one-pixel seam) | same |

- **NXGL-20 MUST** Choose the internal resolution from the operation mode, not a fixed setting. Recreate render targets on a docking change.
- **NXGL-21 SHOULD** MSAA and edge marking can't run together (edge marking needs a single-sample frame). On Switch, default to edge marking without MSAA.
- **NXGL-22 SHOULD** Avoid extra full-screen passes. Each one at 1080p reads and writes the whole frame through the shared memory bus.
- **NXGL-23 SHOULD** Present with vsync on (swap interval 1). Pace the DS's 59.83 Hz against the display's 60 Hz with audio rate control (small resampling changes) or by dropping/repeating one frame every few seconds. Tearing on the handheld screen is very visible.

### 5.7 Docking

- **NXGL-24 MUST** On an operation-mode change: wait until the current frame is done, resize the window/drawable (via SDL's resize events or `nwindowSetCrop`/EGL surface recreation, depending on the port), recreate size-dependent render targets, and continue. Handle it at the start of a frame, not inside a callback.

---

## 6. Audio

- **NXAUD-1 MUST** Open the audio device at 48 kHz (the Switch's native output rate) and resample the DS's mix once. Other rates are resampled again by the system.
- **NXAUD-2 SHOULD** Buffer enough to cover a slow frame (2–3 frames of audio) but no more; too much adds input-to-sound lag.
- **NXAUD-3 MUST** The mixer never waits on the game thread. If the game falls behind, play what's mixed and let the game slow; never drop or speed up audio.

---

## 7. Input

- **NXIN-1 MUST** Map the Switch buttons to the DS buttons in the host layer: A, B, X, Y, L, R, Plus → Start, Minus → Select, D-pad and left stick → D-pad.
- **NXIN-2 SHOULD** Touch screen: in handheld mode the Switch's touch screen maps to the DS bottom screen's position on screen. Docked, offer the right stick as a pointer or a button-driven cursor, since some screens need touch.
- **NXIN-3 SHOULD** Fast forward (desktop: Tab) maps to ZR, with the speed capped by what the CPU can do; logic speed rises, drawing skips.

---

## 8. Mods

- **NXMOD-1 MUST** Any mod runtime runs as an interpreter or AOT-compiled code on Switch. Don't rely on runtime JIT (writable-then-executable memory needs special handling on Horizon).
- **NXMOD-2 SHOULD** Mods run off the frame's critical path where possible, with a time budget per frame.

---

## 9. Symptoms and causes

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Crash or out-of-memory right at start | Applet mode, or a huge reservation (fibers, wasm memory) | NXSYS-1, NXMEM-1, NXMEM-4 |
| Hitch the first time a scene appears | Shader compiled on first use | NXGL-5 |
| Slow frames with little on screen | GL call overhead: too many draws, state changes or uploads | NXGL-16 to NXGL-19 |
| Every frame waits on the GPU | `glReadPixels`, `glFinish`, re-specified textures | NXGL-9, NXGL-14 |
| Flicker or stale geometry | Writing a buffer the GPU still reads | NXGL-10, NXGL-17 |
| Fine docked, slow handheld | Internal resolution or passes too costly at handheld clocks | NXGL-20 to NXGL-22 |
| Wrong size or crash after docking | Targets not recreated, or recreated mid-frame | NXGL-24 |
| Workers make things slower | Pool sized for a PC; threads pile onto one core | NXTHR-1, NXTHR-4 |
| One thread never runs | Spin-waiting at the same priority on the same core | NXTHR-3 |
| Audio crackle in busy scenes | Mixer blocked on a game lock, or buffer too small | NXTHR-5, NXAUD-2 |
| Tearing on the handheld screen | Swap interval 0 | NXGL-23 |
| Memory drops over a long session | Unbounded caches, leaked GL objects | NXMEM-5 |

---

## 10. Debugging

- **Logs:** `nxlink -s` streams stdout/stderr to the PC in dev builds (`socketInitializeDefault()` + `nxlinkStdio()`). Also keep a log file on the SD card for runs without a PC.
- **Crashes:** Atmosphère writes crash reports to `/atmosphere/crash_reports/`. Subtract the module base from the crash address and look it up with `aarch64-none-elf-addr2line -e eidos.elf <offset>`.
- **Per-stage timing:** log game, compositor, uploads, GL submit and present, once per second, in handheld and docked mode.
- **Emulators:** fine for logic checks. Never judge performance, memory, or GL driver behaviour on them.

---

## 11. Review checklist

- [ ] Refuses applet mode; logs memory and GL strings at startup.
- [ ] No large uncommitted reservations or fixed-address mappings assumed.
- [ ] All shader programs built at startup.
- [ ] Per-frame textures and buffers rotated or orphaned; uploads skipped when unchanged.
- [ ] No synchronous readbacks or `glFinish` in the frame.
- [ ] Draws batched; redundant state changes skipped.
- [ ] Worker pools sized for 3 cores; threads pinned; no spin-waits.
- [ ] Internal resolution follows docked/handheld; targets recreated on docking.
- [ ] Audio at 48 kHz, mixer never blocked by the game.
- [ ] Measured on hardware in handheld mode after the change.

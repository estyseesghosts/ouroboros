# Nintendo Switch: plan

How eidos gets to real Switch hardware as a homebrew NRO, built with
devkitPro's devkitA64 and libnx. Written 2026-11-02 against eidos
`000cf646c8` and the aletheia pin in `aletheia-pin.md`.

This document is written to be worked through by people and by coding
agents, one task at a time. Each task names the files it touches, what it
needs first, and how to tell it is done.

## Decisions

These are fixed.

1. **NRO only.** The output is `eidos.nro`, started from the homebrew menu.
   No NSP, no forwarder.
2. **Title mode only.** The program runs only when started over a game
   (hold R while starting a title), which gives it most of the console's
   memory. In applet mode (started from the album) it shows a message and
   exits. Applet mode is not a goal.
3. **The Switch is a fourth `host/` target.** The game module is the same
   wasm32 module every 64-bit build uses (`lp64.md`); only stage two
   changes. The Switch build follows the Android build's shape:
   `host/` built with the platform's compiler, SDL3 built from source with
   it (`EIDOS_HOST_SDL_SOURCE`), and a `switch/` folder with a `build.py`
   and a `README.md`, as `android/` has.
4. **SDL3 is devkitPro's.** The official SDL3 Switch port is only for
   developers under Nintendo's NDA. devkitPro's fork of SDL has a
   `switch-sdl-3.4` branch with video (EGL over Mesa), audio, gamepads,
   touch and the software keyboard. The build pins one commit of it.
5. **OpenGL ES 3 first.** The host already translates the module's desktop
   OpenGL for OpenGL ES 3.0 (`HOST_GLES`, `tools/wasm_gl_imports.py`).
   The Switch starts on that path. Desktop OpenGL through Mesa is a later
   option, not a requirement.
6. **Nothing changes for the other platforms.** Every change is either
   platform-neutral and tested on the desktop, or guarded by
   `__SWITCH__` (C) or `NINTENDO_SWITCH` (CMake).
7. **Upstream.** The work goes to Project-Holon/eidos as a series of small
   pull requests, one or a few tasks each, in the order below.

## Rules for whoever does a task

These exist because the repository has strong conventions. Follow them
exactly.

- **Read before writing.** Read every file you edit in full, and the
  document that covers it (`runtime.md`, `lp64.md`, `building.md`,
  `android/README.md`). Copy the style of the code around your change.
- **Never edit aletheia.** The game's C comes from the aletheia checkout
  at the pin. If something in it seems to need a change, stop and report
  it.
- **Style.** C99, 4-space indent, `/* */` comments, a file header of the
  form `path -- what it is`, as in `host/*.c`. Comments say why, in plain
  sentences. Documents are plain prose like this one: no marketing words,
  numbers measured rather than guessed.
- **Guards.** Switch-only code sits under `#if defined(__SWITCH__)`. Do not
  widen an existing `__ANDROID__` or `__APPLE__` guard unless the code
  under it is exactly right for the Switch too; then add `__SWITCH__` to
  the condition and say so in the comment.
- **No drive-by changes.** No reformatting, renaming or refactoring outside
  the task.
- **Dependencies are pinned.** Anything fetched is pinned to a commit or
  release and checked against a recorded SHA-256, like `tools/fetch_*.py`.
- **Commits.** One subject line in the repository's form: `area: what
  changed`, a plain sentence (`host: Switch fibers on the arm64 stack
  swap`). The body says why.
- **Desktop stays green.** Before a task is done, the Linux desktop build
  and its tests still pass:

  ```
  cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../aletheia
  cmake --build build && ctest --test-dir build --output-on-failure
  python3 tools/build64.py --build-dir build64 --aletheia-root ../aletheia --fast
  ```

- **Hardware gates.** A task marked **[hardware]** cannot be finished by
  an agent. Do the build, write down exactly what to run and what to look
  for, and stop. A person runs it on a Switch and records the result in
  "Results" below.
- **Report, do not guess.** If a measurement, an API or a library does not
  behave as this plan says, write down what you saw and stop. Do not work
  around it silently.

## The parts that matter

What a task needs to know about the current design.

| What | Where | Why it matters on the Switch |
| --- | --- | --- |
| Stage one: game, port and engine to `eidos_boot.wasm` | `tools/build64.py`, `CMakeLists.txt` | Unchanged. Build it on a PC. |
| Stage two: wasm-opt, wasm2c, `host/`, SDL3 | `host/CMakeLists.txt` | Gets a Switch branch. |
| Module memory: 256 MiB at start, up to 2 GiB | `CMakeLists.txt` (`--initial-memory`, `--max-memory`) | The Switch has no lazy commit. |
| Memory never moves (guard pages) | `host/CMakeLists.txt`, `host/par.c`, `host/sdl.c` | wasm2c's guard pages need `mmap`; libnx has none. |
| Fibers: 64 at most, 16 MiB native stack each | `host/fiber.c` | Reserved, not committed, on other systems. On the Switch it would be committed: 1 GiB. |
| arm64 stack swap in assembly | `host/fiber.c` (`HOST_FIBER_ASM`) | Works on any arm64 with the AAPCS64 ABI. |
| Workers: up to 7, 16 MiB native stack each | `host/par.c` | An application gets 3 CPU cores. |
| Mobile start: arguments and environment from files | `host/main.c` (`HOST_MOBILE`, `mobile_args`) | No command line on the Switch either. |
| ROM dialog | `install/picker.c`, `bw_host_pick_rom` in `sdk/wasm_host.h` | No file dialog on the Switch. |
| Gamepad mapping by position (east is A) | `engine/platform/platform.c` | Already matches the Switch's buttons. |

## Tasks

Do them in order unless a task says otherwise. Ids are for commit bodies
and pull requests.

### S0: measure what the Switch must hold

Needs: nothing. Platform-neutral, desktop only.

Add statistics to the host, printed to stderr at exit when
`EIDOS_HOST_STATS=1` is set:

- the module memory's peak size in pages;
- the most fibers alive at once;
- each fiber's deepest native stack use: fill the stack with a pattern
  when the fiber is made, find the first changed byte when it ends or at
  exit;
- the same for each worker's native stack (`host/par.c`);
- the main thread's deepest stack use, the same way where possible.

Files: `host/fiber.c`, `host/par.c`, `host/main.c`, `docs/runtime.md`
(one paragraph on the switch).

Run a Linux build64 build through: boot to the title; New Game to the
first battle; walking in the first town; saving, quitting and continuing;
a trainer battle. Record the numbers in "Results".

Done when: the numbers are in "Results", with a rounded-up budget for
each: module memory, fiber stack, worker stack. The rest of the plan uses
those budgets, not the 2 GiB and 16 MiB figures above.

### S1: toolchain and SDL3

Needs: nothing.

- `tools/fetch_sdl.py` (or a new `tools/fetch_sdl_switch.py` if that
  script's shape does not fit): fetch devkitPro's SDL at one pinned commit
  of `switch-sdl-3.4`, checked against a SHA-256, and print its folder as
  the last line, as the Android and iOS builds expect.
- `switch/README.md`: what to install. devkitPro's pacman packages
  (`switch-dev`, which brings devkitA64, libnx and switch-tools; and
  `switch-mesa`, `switch-libdrm_nouveau`, `switch-cmake`), with the
  versions used. Python 3.11, CMake, Ninja.

Check first and write down: whether devkitPro's `Switch.cmake` defines
`NINTENDO_SWITCH`, and whether it provides `nx_generate_nacp` and
`nx_create_nro`. The tasks below assume both. If not, report it.

Done when: SDL3 builds alone with `Switch.cmake` into a static library,
and `switch/README.md` lists every package and version.

### S2: the Switch branch of `host/CMakeLists.txt`

Needs: S1.

- An `if(NINTENDO_SWITCH)` branch next to the Android and iOS ones:
  SDL3 static from `EIDOS_HOST_SDL_SOURCE`; `HOST_GLES=1`;
  `-fsigned-char` (plain `char` is unsigned on arm64, as on Android); link
  SDL3, EGL, glapi, drm_nouveau, nx and m, in the order devkitPro's own
  examples use.
- An `.nro` after the link: NACP with name `eidos`, author
  `Project Holon`, version from the release; icon from `assets/` (make a
  256x256 JPEG with `tools/mkicon.py` if it cannot already).
- `switch/build.py`, modelled on `android/build.py`: takes
  `--module <build64>/module/eidos_boot.wasm`, configures `host/` with
  `$DEVKITPRO/cmake/Switch.cmake`, builds, and prints where the `.nro` is.
  Use `--parts 96` or more by default: the generated C files are large and
  the build machine's memory is the limit.

Expect compile errors in `host/` at this point. Fix only what the compiler
reports, each with the smallest `__SWITCH__` guard. Anything that needs
real work belongs to S3 to S7: leave a `#error` with the task id there
instead of a quick fix.

Done when: `switch/build.py` produces `eidos.nro` with every remaining
`#error` gone after S3 to S7, and the desktop build is unchanged.

### S3: module memory that never moves

Needs: S0, S2.

wasm2c's runtime (`wasm-rt-mem-impl.c`) keeps memory fixed by reserving
the whole range with `mmap` and guard pages. libnx has no `mmap`. Read the
runtime at the pinned WABT version before choosing; the expected shape:

- on the Switch, build with `WASM_RT_USE_MMAP=0` and
  `WASM_RT_MEMCHECK_BOUNDS_CHECK=1` (wasm2c then checks every access);
- allocate the S0 budget once, when the module is made, and never move
  it. A grow inside the budget only raises the size. A grow past it fails
  the way a failed `memory.grow` does in wasm, and the program prints
  "module memory budget reached" with the size, then exits;
- if the runtime cannot do this through its own options, add a host
  allocator, not an edit to WABT's files.

Files: `host/CMakeLists.txt`, `host/main.c` or a new `host/mem_switch.c`.

Done when: on the desktop, a build with the same options (bounds checks,
fixed budget) passes the S0 run with the memory's base address unchanged
from start to exit (log it under `EIDOS_HOST_STATS`). That proves the
logic before the Switch exists.

### S4: fibers and threads

Needs: S0, S2.

- `host/fiber.c`: add `__SWITCH__` to the `HOST_FIBER_ASM` condition.
  Stacks from `aligned_alloc` at 16 bytes or more, sized from S0's
  budget, through a `NATIVE_STACK` value for the Switch.
- `host/main.c`: run `host_run` on a thread with an explicit stack sized
  from S0, as the Android path does with `run_on_fiber_stack`. Use
  `pthread_attr_setstacksize` if newlib's pthreads on libnx honour it;
  else libnx's `threadCreate`.
- `host/par.c`: at most 2 workers on the Switch (the third core is the
  game's own thread; the fourth is the system's). Worker stacks from S0.

Done when: the build links, and `tests/test_host_fiber.py`'s probe also
compiles for the Switch (add a cross-compile check that builds it with
devkitA64 when `DEVKITPRO` is set; it cannot run there).

### S5: start-up, logging and crashes

Needs: S2.

In `host/main.c`, under `__SWITCH__`:

- `HOST_MOBILE` on, so `mobile_args` reads `args.txt` and `env.txt`.
  Check what `SDL_GetPrefPath("eidos", "eidos")` returns on the Switch.
  The folder must be `sdmc:/switch/eidos/`; if SDL gives something else,
  use that path directly and say why in a comment;
- before anything else: if `appletGetAppletType()` is not an application
  type, show "eidos needs the whole console: hold R while starting a game
  and choose eidos" with libnx's console or error applet, wait for a
  button, and exit;
- `socketInitializeDefault()`; in a debug build, `nxlinkStdio()` so
  stderr reaches `nxlink -s` on a PC, in place of `stderr.txt`;
- a libnx exception handler that prints the fault, PC, LR and a short
  stack walk as addresses relative to the program's start, to stderr and
  to `sdmc:/switch/eidos/crash.txt`. The wasm2c functions keep their
  names (`-g`), so `aarch64-none-elf-addr2line -f -e eidos.elf` turns
  them into game function names. Write that command in `switch/README.md`.

Done when: it builds. **[hardware]** The NRO started in applet mode shows
the message and exits; started in title mode it prints its first lines
over nxlink.

### S6: paths and the ROM

Needs: S5.

- `install/location.c`: on the Switch, the home is
  `sdmc:/switch/eidos/`.
- `install/picker.c`: on the Switch, no dialog. `bw_host_pick_rom`
  answers with the first `*.nds` file in `sdmc:/switch/eidos/`, sorted by
  name, or fails with "put your Pokemon Black or White ROM in
  sdmc:/switch/eidos/".
- Decide with a maintainer whether the Switch runs the first-run setup
  (`engine/platform/wizard.c`, which identifies and extracts the ROM
  inside the module) or skips it as the phones do. The plan's default:
  run it, since the extractor is already in the module and needs only the
  picker.

Done when: on the desktop, nothing changed; on the Switch, it builds.
**[hardware]** A first start with only a ROM in the folder extracts it and
reaches the title.

### S7: video, input and system events

Needs: S5.

- `host/sdl.c`: the window is the screen: 1280x720 handheld, 1920x1080
  docked. Check that a dock or undock gives a size-change event and that
  the presenter follows it (`engine/platform/plat_gl.c` already handles a
  resized window on the desktop).
- Imports with no meaning on the Switch (clipboard, window position,
  window icon, borders) return their failure value without logging every
  frame.
- Touch: handheld touches arrive as SDL touch events; check that the
  module sees them the way it does on Android (the one-screen interface
  is built for touch). Docked has no touch: the one-screen layout is the
  default on the Switch, as on the phones.
- Text input: `SDL_StartTextInput` opens the software keyboard; check it
  for naming the player, the rival and Pokemon.
- HOME button and sleep: SDL's background and terminate events. Make sure
  the game's save is flushed on the terminate event (closing the program
  from the HOME menu) the same way it is when the desktop window closes.

Done when: it builds. **[hardware]** Milestone M2 below.

### S8: milestones on hardware

Needs: S3 to S7. Each one is **[hardware]**.

| | What | How to check |
| --- | --- | --- |
| M1 | Starts, extracts, reaches the title with `EIDOS_RENDERER=sdl` in `env.txt` | the title screen, the opening's music |
| M2 | The same with the OpenGL presenter and the GPU 3D (no `EIDOS_RENDERER`) | the title, then the first town with 3D |
| M3 | New Game, the first battle, saving, quitting from HOME, continuing | the save is there after a restart |
| M4 | Handheld and docked, switching while playing | the picture fits after each switch; touch works handheld |
| M5 | `--profile` frame times in the field and in a battle, handheld | recorded in "Results" |

### S9: speed

Needs: M5's numbers. Order by what M5 shows; likely candidates:

- **The bounds checks.** Replace S3's fixed allocation with a reserved
  range: `virtmemFindAslr` and `virtmemAddReservation` for the whole wasm
  address space plus a guard, physical memory mapped in as the module
  grows. An access outside then faults, which ends the program as a trap
  does today. Keep S3's path as the fallback under an option.
- **CPU 3D frames.** Frames with fog, the rear-plane bitmap or shadow
  polygons fall back to the CPU rasteriser (`graphics-backends.md`).
  Moving those to the GPU helps every platform; coordinate with that
  work rather than doing it Switch-only.
- **Internal resolution.** Default 1x handheld, 2x docked, if 2x holds
  the frame rate.
- **`--fpcast-emu`.** Every indirect call carries 20 64-bit arguments.
  Measure its share before touching it; any change belongs in `lp64.md`'s
  design, not in a Switch guard.
- **Desktop OpenGL through Mesa**, to skip the ES translation, only if
  the profile shows the translation costs time.

Done when: 59.83 frames a second in the field and in battle, handheld,
at 1x, recorded in "Results".

### S10: documentation, CI and release

Needs: M3. Can start earlier for the documents.

- `switch/README.md`: build, install (`sdmc:/switch/eidos/eidos.nro`),
  folder layout (as `android/README.md` has), title-mode requirement,
  nxlink, crash reports.
- `docs/building.md`: a Switch section pointing at it.
- `README.md`: the Switch under Milestone 9 (Packaging).
- `CHANGELOG.md`: one entry in the next version's section, in its style.
- CI: a job in `.github/workflows/release.yml` in the
  `devkitpro/devkita64` container, at a pinned image digest, that builds
  the module and the NRO and uploads `eidos.nro`, alongside the existing
  platforms. It runs no tests (there is no Switch to run them on).

Done when: a tag builds `eidos.nro` in CI.

## Risks

- **Memory.** If S0's module peak is near the title-mode limit after the
  program, Mesa and the GPU's buffers, the plan needs a smaller budget or
  memory work in the port. S0 decides this first, before any Switch code.
- **Speed.** The Switch's CPU cores are much slower than a PC's, and the
  game runs on one of them. S9 may need more than this plan lists.
- **devkitPro's SDL3.** A branch of a fork, not a release. If a bug blocks
  a milestone, fix it in a pinned fork and offer the fix upstream to
  devkitPro.
- **Mesa on the Switch.** The OpenGL ES 3.0 features the translation uses
  are expected to be there; integer render targets (edge marking) and
  multisampling are the ones to check first, at M2.

## Results

Filled in as tasks finish. Each entry: date, eidos commit, task, numbers
or outcome.

| Date | Commit | Task | Result |
| --- | --- | --- | --- |
| 2026-11-02 | eidos unknown; devkitPro Mesa 20.1.0-rc3 | logs/00: GL presenter, old Mesa | GL `OpenGL NV120 (OpenGL ES 3.2 Mesa 20.1.0-rc3)`, presenter OpenGL 3.3. No GPU3D feature line and no guard-page line (older build or other env; env not stored). 31 fps samples: min 4 / median 7 / max 41. 21–41 for the first 5 s, then collapses to 4–14 and settles at 7–8. Errors: `nvram: cannot write the firmware flash` ×6. |
| 2026-11-02 | eidos unknown; devkitPro Mesa 20.1.0-rc3 | logs/01: GL presenter, old Mesa (A5 baseline c) | GL `NV120 (OpenGL ES 3.2 Mesa 20.1.0-rc3)`, GPU 3D features 0x3, integer samples 8, unsupported 0x0. 47 samples: min 4 / median 10 / max 59. 56–59 for the first 6 s (title), then collapses at sample 7 to 6–28, mostly 7–10. Errors: `svcMapMemory(268435456) failed` → guard pages unavailable, fixed block; nvram ×6. |
| 2026-11-02 | eidos unknown; devkitPro Mesa 20.1.0-rc3 | logs/02: GL presenter, old Mesa (A5 baseline c) | Same GL string, features 0x3, integer samples 8, unsupported 0x0. 23 samples: min 6 / median 11 / max 61. 58–61 for the first 4 s, then collapses at sample 6 to 6–20, mostly 7–11. Errors: guard pages unavailable; nvram ×6. |
| 2026-11-02 | eidos unknown; no GL context | logs/03: SDL renderer control | `presenter SDL renderer`, window flags 0x2020 (no GL). No GPU3D lines. 60 samples: min 4 / median 22 / max 60. 57–60 for the first 7 s, then 4–24 (mostly 21–24) for ~35 s, back to 52–60 for 16 s, then down to 34, 12 at the end. Errors: guard pages unavailable; nvram ×6. |
| 2026-11-02 | unknown | logs/04: crash at start-up | `boot.txt` ends at `host_run` (no `stderr redirected` stage). `stderr.txt` ends at `eidos_boot: identity`. No GL string, no fps. Cause not in the log. |
| 2026-11-02 | unknown | logs/05: EGL init failure | `SDL_GL_LoadLibrary: Could not initialize EGL`, then `SDL_CreateWindow -> null (EGL not initialized)` → `no window; running headless`. No GL string, no fps. nvram ×6. Read as transient after the 04 crash (fix-ts §0); not re-verified by a later identical run with the same NRO. |
| 2026-11-02 | eidos unknown; mesa-switch 0ff52079 | logs/06: mesa-switch, edge on (field black per A5 record) | GL `OpenGL NV12B (OpenGL ES 3.2 Mesa 26.2.4 (git-0ff52079ca))`, features 0x3, integer samples 8, unsupported 0x0. nouveau/switch telemetry on. 57 samples: min 1 / median 8 / max 20. Never reaches 59; 11–20 for the first 21 s, collapses to 1–6 for ~20 s, then 7–12. Errors: nvram ×6. Env not stored. |
| 2026-11-02 | eidos unknown; mesa-switch 0ff52079 | logs/07: mesa-switch, edge on; screenshot | Same GL string (0ff52079), features 0x3, integer samples 8, unsupported 0x0. 59 samples: min 5 / median 9 / max 59. 41–59 for the first 6 s, then collapses to 5–22, mostly 7–9. `cannot replace sdmc:/switch/eidos/profile/settings.ini` after sample 43 (an in-game setting change did not persist), nvram ×6. Screenshot: Mistralton City HUD over a blank, near-white 3D area (not black). |
| 2026-11-02 | eidos unknown; mesa-switch 16732f6 | logs/08: mesa-switch, edge on | GL `OpenGL NV12B (OpenGL ES 3.2 Mesa 26.2.4 (git-16732f6142))`, features 0x3, integer samples 8, unsupported 0x0. 51 samples: min 3 / median 13 / max 59. 56–59 for the first 6 s, collapses to 3–24 for ~14 s, then flat 11–14. Errors: nvram ×6. Env not stored. |
| 2026-11-02 | eidos unknown; mesa-switch 16732f6 | logs/09: mesa-switch, outlines off (world renders, per A5 record) | Same GL string (16732f6), features 0x3, integer samples 8, unsupported 0x0. 143 samples: min 0 / median 6 / max 28. Never reaches 59: 13–28 for the first 11 s, 2–5 for ~12 s, 14–18 for 8 s, then `cannot replace …/settings.ini` after sample 31 (an in-game setting change, possibly the outlines toggle, mid-run), then 2–13, settling at 4–7 to the end. Outlines-off comes from the A5 record; env not stored, so whether it was set by env or in game is not verifiable from the log. nvram ×6. |
| 2026-11-02 | unknown | logs/10: `--profile 300` on one `args.txt` line | Died at argument parsing: `stderr.txt` ends after `eidos_boot: settings` with the `usage:` text, and `boot.txt` ends at `host_run: start`. No GL string, no fps. The cause is `args.txt` taking one argv per line (`host/main.c:467-475`). |
| 2026-11-02 | e229250 + mesa-switch 16732f6 (out/eidos.nro, per maintainer) | logs/11: `EIDOS_NOWAIT=1 EIDOS_EDGE=0 --profile`, stdout not captured | GL `NV12B (OpenGL ES 3.2 Mesa 26.2.4 (git-16732f6142))`, features 0x3, integer samples 8, unsupported 0x0. No nouveau/switch telemetry lines (unlike 06–09). 164 samples: min 0 / median 48 / max 82. 47–82 for the first 6 s, steady 47–55 (median 49) for ~82 s, then a **step drop** at sample 89 to 13–44 (median 24) for ~17 s, then 23–29 (median 25) to the end. No `prof frame` lines (stdout not captured), so no bucket data. nvram ×6. |
| 2026-11-02 | eidos unknown; mesa-switch 0ff52079 (logs/06–07) and 16732f6 (logs/08–09) | A5 | **A5 — P0 FAIL**: title renders, field black with outlines on (NVC0); zink run (b) never done; baseline (c) = logs/00–02. |
| 2026-11-02 | e229250 + mesa-switch 16732f6 (out/eidos.nro) | logs/12: A5b batch #1 run 1, `EIDOS_EDGE=0`, Mistralton gym bench spot | VALID (title mode, NV12B git-16732f6142, unsupported 0x0). The gym world renders. Field ≈7 fps steady over 120 s (no NOWAIT). nvram ×6. |
| 2026-11-02 | same | logs/13: run 2, outlines on (default, no `EIDOS_EDGE`) | VALID. Field BLACK under the Mistralton HUD for the full 120 s. ≈4 fps. |
| 2026-11-03 | same | logs/14–20: runs 3–9, `EIDOS_EDGE=1..7` | All VALID (title mode, git-16732f6142, 0x0). All seven variants: field BLACK below the HUD at t0 and t120 (screenshot pixel max 0 below y=40 where measured; maintainer confirmed black on the console). fps: EDGE=1 4 (= run 13, so default = variant 1 as assumed); EDGE=2..7 steady 12–14. **fix-ts 1a done: all black ⇒ the bug is in edge pass setup, not a `k_fse` branch.** Run 15 (EDGE=2) launched and navigated by the maintainer by hand; no title.jpg. Run 19 t0 caught the season card (field window ≈115 s). |
| 2026-11-03 | same | logs/21: run 10, `MESA_SWITCH_GL_DRIVER=zink` (A5 owed run b) | VOID for the Zink question: **Zink not available in this build.** `SDL_CreateWindow -> null (Could not initialize EGL)`; no `unknown MESA_SWITCH_GL_DRIVER` line. The SDL-renderer fallback window also fails (`EGL not initialized`) → `no window; running headless`, screen black, eidos never shows anything (F1 evidence). |
| 2026-11-03 | same | logs/22: run 11, attribution (`EIDOS_NXLINK=1 EIDOS_NOWAIT=1 EIDOS_EDGE=0`, `--profile`/`300`) | VALID; world renders. Sent with nxlink from the mesa-switch-build-amd64 image to the **hbmenu** netloader (Sphaira's NXlink server failed twice: broken pipe, then `Invalid response`). 10 `prof frame` lines. Field: **62 ms/frame wall (≈16 fps)** — render 39.9, main2d 9.4, geom 8.4, g3d 6.0, audio 4.1, present 4.0, step 3.4, boot3d 1.9, gldraw 1.1, snap 0.9, texdec 0.4, glread 0, unprem 0, wait 0 (avg ms). fps series: 32–50 on title, then 15–16 steady in the field. |
| 2026-11-03 | same | Frame-pacing finding (logs/09, 12, 22) | Without `EIDOS_NOWAIT` the game runs ≈7 fps in this field and 2–3 fps on 2D menus (title/Continue); with NOWAIT the same spot is 15–16 fps (logs/22). So the pacing/vsync wait roughly halves throughput, separately from the edge-pass bug → C-phase pacing item. logs/11's ≈49 fps (NOWAIT, spot unrecorded) is NOT reproduced at the bench spot; the dominant field cost on logs/22 is `render` (≈40 ms), and `glread`/`unprem` are already 0, contradicting fix-ts §0's sync-readback claim for this path. |
| 2026-11-03 | same | A5b hardware batch #1 | **PASS** per checklist §6 (maintainer confirmed visuals): controls hold (run 1 renders, run 2 black), 1a = all-black, Zink recorded (void, reason), attribution recorded. A5b itself stays open until 1b/1c fix + confirmation run. |
| 2026-11-03 | switch/mesa-a5b 42ad775 (out/eidos-a5b-diag.nro) | logs/23: edge diag, outlines on | Field black. pedge links, uniforms all found, FBOs complete, no error from the pass itself; GL_INVALID_OPERATION pending at edge_pass entry on edge frames 1–3. |
| 2026-11-03 | switch/mesa-a5b 3e7356c (out/eidos-a5b-diag2.nro) | logs/24: edge diag + ~30 labelled GL error probes, outlines on, paced, `--profile 300` | Field black with **no GL error anywhere** (run 23's 0x0502 did not recur) ⇒ the edge pass writes black silently. Profile: 49 ms/frame wall (≈20 game fps) while the swap counter reads 4 fps ⇒ **the fps counter counts swaps; paced runs skip presents, so the "≈7 fps paced" figures (runs 12–20, frame-pacing row above) understate game speed.** The frame-pacing finding is superseded by this. |

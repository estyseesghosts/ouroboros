# eidos on the Nintendo Switch: port notes

Written 2026-11-02 against eidos `4b7469d`/`505f6a8`, the aletheia pin in
`docs/aletheia-pin.md`, and devkitPro's `switch-sdl-3.4` fork. This file is
the working record: what was built, what broke, what was tried, and what is
still open. `switch/README.md` is the short build-and-run guide.

The port is a homebrew NRO. It runs in title mode only. It is not an NSP
and has no forwarder.

## Current state

What works:

- The NRO builds with devkitA64, libnx, devkitPro's SDL3, EGL/Mesa and the
  wasm2c output of the normal wasm32 module.
- It reaches the title screen and, with an extracted data folder, boots the
  game world and menus.
- First-run extraction now works: the setup window opens, the ROM in
  `sdmc:/switch/eidos/` is found, and `firstrun done` is reached.
- The save folder and profile layout are in place.
- The GL presenter is created on Mesa's `NV120` (OpenGL ES 3.2), and the
  CPU-only SDL presenter also runs.
- Start-up tracing, FPS logging, 3D feature reporting and a crash-report
  path are in place.

What does not work:

- **The GL/OpenGL ES 3D presenter is black and slow.** The game reaches the
  world but shows black/white frames at about 6–11 fps. The GPU path reports
  `GPU 3D features 0x3, integer samples 8` (edge marking and fog shaders
  compiled) and `3D features the GPU cannot draw: 0x0`: it is not a CPU 3D
  fallback, yet the picture is wrong. Turning GPU 2D off
  (`EIDOS_G2D_GPU=0`) does not fix it.
- **The SDL presenter is playable but slow**: about 22–24 fps in the field
  and 60 fps in menus, with CPU 3D. It is useful as a control and for
  isolating the GL bug, but it is not the intended default.
- **Saves and firmware do not persist.** The game prints
  `eidos: nvram: cannot write the firmware flash (sdmc:/switch/eidos/profile/saves/firmware.bin); it stays in memory`.
  The same failure prevents the game's save file from being written.
- **The guard-page memory path cannot map its first range**:
  `eidos: svcMapMemory(268435456) failed`, then
  `guard pages unavailable; using the fixed block`. The fallback works, but
  the build's generated code has no bounds checks, so it is not the safe
  build either.
- 30 fps in the field has not been reached. The measured CPU-only path is
  about 22–24 fps; the GL path is worse, not better.

The current tree also contains an **uncommitted experiment** in
`host/main.c` that sets `EIDOS_RENDERER=sdl` when it is unset. That is not
an accepted decision; it must be removed/reverted before the next build.
The tests in "Performance measurements" used an explicit
`EIDOS_RENDERER=sdl` in `env.txt`, not a default.

## The shape of the port

The game is unchanged. Stage one is the normal wasm32 module
(`eidos_boot.wasm`); stage two is `host/`, compiled with devkitA64 for
aarch64 and linked against libnx and SDL3. The Switch is a fourth `host/`
target beside Windows, macOS, Linux, Android and iOS.

Files added or changed for the port:

| path | what |
| --- | --- |
| `switch/build.py` | configures `host/` with `Switch.cmake`, builds SDL3 and the NRO |
| `switch/README.md` | build, install and run instructions |
| `switch/icon.jpg` | 256x256 JPEG for the NRO |
| `tools/fetch_sdl_switch.py` | devkitPro SDL3 `switch-sdl-3.4` at a pinned commit and SHA-256 |
| `host/CMakeLists.txt` | `NINTENDO_SWITCH` branch, SDL/EGL link line, NACP/NRO |
| `host/main.c` | 1.5 GiB libnx heap, title-mode check, mobile arguments, 8 MiB game thread, start-up stages |
| `host/fiber.c` | arm64 stack-switch fibers, 2 MiB native stacks |
| `host/par.c` | two compositor workers, 2 MiB native stacks |
| `host/mem_switch.c` | fixed 1 GiB module-memory block, bounds-check build |
| `host/mem_switch_guard.c` | `virtmem`/`svcMapMemory` guard-page experiment, currently unusable |
| `host/switch_include/sys/mman.h` | empty shim: wasm2c includes it unconditionally |
| `host/switch_include/eidos_switch.h` | `sigsetjmp`/`siglongjmp` mapped to `setjmp`/`longjmp` |
| `host/wasi.c` | preserves `sdmc:` device paths, including `/sdmc:` from the module |
| `host/sdl.c` | loads EGL before any window, screen-sized window, FPS/SDL/GL reports |
| `install/location.c` | Switch home and absolute paths |
| `install/picker.c` | first `*.nds` in `sdmc:/switch/eidos/` |
| `sdk/plat_save.c` | Switch save folder and a tolerant `fsync` attempt |
| `sdk/plat_crash.c` | no-op crash reporter for libnx (no POSIX signals) |
| `demo/eidos_boot.c` | module start-up stages and early constructor stage |
| `engine/g3d/hw3d.c` | optional report of unsupported GPU features |
| `engine/platform/plat_gl3d.c` | GPU feature report and `EIDOS_GPU3D_FORCE` experiment |
| `.gitignore` | ignores `switch/eidos.nro` and `switch/eidos.elf` |

## Entry point and host startup

The Switch build does **not** use SDL's main. `host/main.c` keeps
`SDL_MAIN_HANDLED`, defines the process's `main`, and calls
`SDL_SetMainReady()` and `SDL_Init(0)` before creating the game thread and
the heap mutex. libnx calls `main` directly.

At start-up the host:

1. writes the first `boot.txt` stage;
2. calls `socketInitializeDefault()` (needed by SDL/network and `nxlink`);
3. refuses a non-`AppletType_Application` start with libnx's console;
4. reads `args.txt` and `env.txt` from `sdmc:/switch/eidos/`;
5. calls `nxlinkStdio()` when `EIDOS_NXLINK=1`;
6. runs `host_run` on an 8 MiB SDL thread.

`SDL_CreateWindow` on Switch is forced to 1280x720 in handheld and
1920x1080 in docked mode; the window is the screen. The module passes
`--resolution 1` by default (the in-game setting can override it), and
`host/par.c` uses two compositor workers.

## Build environment

The working build used the existing `hgss-p1` Docker container (Ubuntu
24.04, x86_64), not a devkitPro image. Packages installed there with
devkitPro's pacman:

| package | version used |
| --- | --- |
| `switch-dev` (group) | devkitA64 r30, devkita64-gcc 16.1.0, libnx 4.12.0, switch-tools 1.13.1 |
| `switch-mesa` | 20.1.0-5 |
| `switch-libdrm_nouveau` | 1.0.1-2 |
| `switch-cmake` | 1.5.2-1 |
| `switch-pkg-config` | 0.28-4 |

The container also needed ordinary Ubuntu `cmake` 3.28.3 and
`ninja-build` 1.11.1. `tools/fetch_wasm.py` fetched wasi-sdk 25, binaryen
123 and wabt 1.0.37 for Linux in the container's cache. The SDL source is
devkitPro's `switch-sdl-3.4` at commit
`c329016c0e338ab4397ad930f83330c9fe058348`
(archive SHA-256 `6047e4787f105ab4a0c6293ee3090ce5d1ebad112fcbe2a3e5a8874b0cc6f709`).

`Switch.cmake` defines `NINTENDO_SWITCH` and `-D__SWITCH__`, provides
`nx_generate_nacp` and `nx_create_nro`, and supplies
`-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft
-ftls-model=local-exec`. Do not pass `-DCMAKE_C_FLAGS` over it: doing so
replaces the toolchain flags and silently removes `__SWITCH__` (this
happened; SDL's dynapi then failed with `#error Please define your
platform` and the host's `#if defined(__SWITCH__)` branches disappeared).

## Building

### Stage one: the module

The module must be rebuilt whenever anything in stage one changes
(`demo/`, `engine/`, `sdk/`, `install/`, `stubs/`, `CMakeLists.txt`). The
Switch build consumes a prebuilt `eidos_boot.wasm`; it does not build it.

On the Mac checkout used here:

```
cmake --build build-macos/module --target eidos_boot -j 8
```

This leaves `build-macos/module/eidos_boot.wasm`. Equivalently, on a
machine with the wasm toolchain:

```
python3 tools/build64.py --build-dir build64 --aletheia-root ../aletheia --fast
```

Copy the module into the container at `~/eidos/eidos_boot.wasm` (or pass
its path to `switch/build.py`).

### Stage two: the NRO

Inside the container, with the source at `~/eidos`:

```
cd ~/eidos
python3 switch/build.py \
  --module ~/eidos/eidos_boot.wasm \
  --build-dir ~/build-switch \
  --sdl-source ~/sdl-switch \
  --jobs 8
```

`switch/build.py` runs CMake with `${DEVKITPRO}/cmake/Switch.cmake`,
builds SDL3 through `host/CMakeLists.txt`, and runs `nx_create_nro`. The
output is `~/build-switch/eidos.nro`; copy it out with `docker cp`.

Useful options:

- `--sdl-source DIR`: a devkitPro SDL checkout; otherwise
  `tools/fetch_sdl_switch.py` fetches the pinned commit.
- `--parts 96`: how many C files wasm2c writes. The default is 96 because
  `game_0.c` alone is very large and the compiler's memory is the limit.
- `--guard-pages`: the experimental `virtmem`/`svcMapMemory` memory path.
  It currently fails at `svcMapMemory` and falls back to the fixed block.
- `--build-dir`: keep separate bounds and guard builds, e.g.
  `build-switch` and `build-switch-guard`.

A full host rebuild of the 96 generated parts takes roughly 20–30 minutes
in this container at `-j 8`; `game_0.c` is the long pole. Rebuilding only
`host/*.c` after a host change is a few minutes.

### If building in the container

The source tree is at `~/eidos`, not the host path. Copy changed source
with:

```
tar -cf - host demo engine sdk install switch tools | \
  docker exec -i hgss-p1 sh -c 'tar -xf - -C ~/eidos'
```

Do not copy `build-macos/` into the container; it is a macOS build tree.

## Running

Copy the NRO to:

```
sdmc:/switch/eidos/eidos.nro
```

Put a legally obtained Pokémon Black or White `.nds` in the same folder.
Start the homebrew menu with **R** held, choose eidos, and keep R held
until the title starts. A plain album start is applet mode; the process
asks for a 1.5 GiB heap and can be closed before it can display the
title-mode message.

Folder layout:

| path | what |
| --- | --- |
| `eidos.nro` | the program |
| `*.nds` | the ROM; the first by name is chosen |
| `extract/` | extracted game data |
| `profile/` | settings, mods and saves |
| `profile/saves/pokeblack.sav` | the game save (slot 1) |
| `profile/saves/firmware.bin` | firmware/flash image |
| `args.txt` | one argument per line |
| `env.txt` | `NAME=value` environment lines |
| `stderr.txt` | host and module messages |
| `boot.txt` | start-up stage trace |

The first start with only a ROM should show the setup window, find the
ROM, and extract to `extract/`. If `extract/` already exists, the setup is
skipped. A copied desktop `extract/` works.

## Debugging and reports

### Start-up trace

`boot.txt` is written by the host and has these stages, in order:

```
main
title mode
stderr redirected
arguments
host_run
host_run: SDL_Init
host_run: mutex
host_run: wasm_rt_init
host_run: wasi_init
host_run: fibers_init
host_run: instantiate
host_run: start
sdl: Init
sdl: Init done
sdl: CreateWindow
sdl: CreateWindow done
sdl: GL_CreateContext
sdl: GL_CreateContext done
sdl: InitSubSystem
sdl: InitSubSystem done
```

A crash before `main` means the libnx heap request failed (usually applet
mode). A stop at `host_run: start` with no module output means the module
has not reached its own `main` (or the module trace is stale — rebuild
stage one).

The module adds its own lines to `stderr.txt`:

```
eidos_boot: constructors
eidos_boot: main
eidos_boot: identity
eidos_boot: settings
eidos_boot: arguments
eidos_boot: firstrun
eidos_boot: firstrun done
```

The constructor marker is a `constructor(101)` so it runs before the
generated overlay-dispatch constructor. If it is absent, the module is not
built from the current tree.

### Environment switches

Set these in `env.txt` while testing:

| name | meaning |
| --- | --- |
| `EIDOS_FPS_LOG=1` | one `eidos: N fps` line a second, from either presenter (N counts the swaps); and one `eidos: M game frames/s, K presents skipped/s, J GPU 2D screens/s` line a second from the game (M is the game's frames, K the presents the lag skipped) |
| `EIDOS_RENDERER_REPORT=1` | prints the presenter (`OpenGL 3.3` or `SDL renderer`) |
| `EIDOS_GPU3D_REPORT=1` | prints GPU 3D features and the unsupported mask |
| `EIDOS_GPU3D_FORCE=1` | lets the GPU draw features it marked unsupported (diagnostic; visual effects may be missing) |
| `EIDOS_G2D_GPU=0` | forces CPU 2D composition |
| `EIDOS_RENDERER=sdl` | forces the SDL presenter (no GL context, CPU 3D) |
| `EIDOS_NOWAIT=1` | no frame pacing; measures raw frame cost |
| `EIDOS_SWITCH_MEM_MB=N` | module-memory budget in MiB (default 1024) |
| `EIDOS_WASI_TRACE=1` | prints failed module file opens |
| `EIDOS_NXLINK=1` | sends stderr to `nxlink -s` instead of `stderr.txt` |
| `EIDOS_PAR_WORKERS=N` | compositor workers (Switch default is 2) |

The host always prints `eidos: SDL_Init`, window and GL context results in
the Switch build, and the GL renderer line once a context is made. The FPS
line is opt-in.

### Console logs and crash reports

Atmosphère writes crash reports under:

```
sdmc:/atmosphere/crash_reports/
```

The newest report has the fault address, PC, LR and registers. With the
ELF:

```
aarch64-none-elf-addr2line -f -e switch/eidos.elf <address>
```

The ELF is built as `build-switch/eidos.elf` and is not stripped. The
generated C keeps the game's function names (wasm-opt `-g`).

## Issues encountered

### 1. Instant close: 1 GiB block on libnx's 512 MiB heap

The first NRO closed before writing anything. libnx's default heap is
about 512 MiB (its `__libnx_initheap` picks the smaller of 512 MiB and the
memory the applet leaves). The module's fixed block was 1 GiB. The
allocation failed, wasm2c received a NULL memory pointer, and the console
showed its generic error.

`host/main.c` now defines:

```c
size_t __nx_heap_size = (size_t)1536 * 1024 * 1024;
```

The fixed allocator allocates the whole 1 GiB block at the first module
allocation and exits with a message if that fails. `boot.txt` reaching
`host_run: instantiate` proves the heap and block exist.

### 2. The module trace never existed in early NROs

`switch/build.py` consumes a prebuilt `eidos_boot.wasm`. Editing
`demo/eidos_boot.c` on the host without rebuilding stage one left the old
module in the container. The host trace looked fine, but the module trace
was absent, so "empty stderr" proved nothing. Always rebuild stage one and
copy the new wasm before rebuilding the NRO. `strings eidos.nro` should
contain `eidos_boot: constructors` and `eidos_boot: main`.

### 3. `sdmc:` paths became `/sdmc:` and the module could not read anything

The WASM path layer rebuilt every module path from the root `/` and then
prepended another `/`, so `sdmc:/switch/eidos/...` became
`/sdmc:/switch/eidos/...`. libnx's `FindDevice` reads the device name
before the colon; `/sdmc` is not a device, and every open/stat failed.
The setup wizard therefore could not see `extract/`, settings, the ROM or
saves.

Two fixes:

- `host/wasi.c` now recognises a device path (`colon` before any `/`) and
  keeps it, including a leading `/` left by the module's
  `bw_os_abspath`.
- `install/location.c` returns `sdmc:` paths unchanged in the wasm module.

The host's own `fopen("sdmc:/...")` calls already worked; only the module's
WASI calls were affected.

### 4. The setup window failed with `EGL not initialized`

devkitPro's Switch video driver requires `_this->egl_data` before it will
create **any** window, including the non-OpenGL window the setup wizard
uses. The host only loaded EGL for the game window (`SDL_WINDOW_OPENGL`).
The wizard failed with:

```
eidos: SDL_CreateWindow -> null (EGL not initialized)
eidos: no window for setup: EGL not initialized
eidos: no playable game data ...
```

`host/sdl.c` now calls `SDL_GL_LoadLibrary(NULL)` once before creating a
window without `SDL_WINDOW_OPENGL`. With that change the log reaches
`eidos_boot: firstrun done` and extraction works.

### 5. Missing `timegm`

newlib on libnx has no `timegm`. `host/main.c` stubs the time-zone offset
to 0 on Switch; the console clock is UTC. The game's date/time may need a
real implementation later.

### 6. Missing `sigsetjmp`/`siglongjmp`

wasm2c's `wasm-rt.h` uses `sigsetjmp`/`siglongjmp` on every non-Windows
host, but newlib only has `setjmp`/`longjmp`. `host/switch_include/eidos_switch.h`
is force-included into every Switch source and maps the sig-prefixed pair
to the plain pair.

### 7. Missing `sys/mman.h`

wasm2c includes `<sys/mman.h>` unconditionally even when
`WASM_RT_USE_MMAP=0`. libnx has no such header. The empty shim at
`host/switch_include/sys/mman.h` satisfies the include; the fixed allocator
does the real work.

### 8. SDL dynapi and `__SWITCH__`

Passing `-DCMAKE_C_FLAGS=-O2` from `switch/build.py` replaced the
toolchain's initialized flags. The compile line then lacked
`-D__SWITCH__` and the arch flags, and SDL's `SDL_dynapi.c` failed with
`#error Please define your platform`. Removing the `CMAKE_C_FLAGS` override
fixed the flags. SDL's dynamic API is also disabled for the static Switch
build (`SDL_DYNAMIC_API OFF`).

### 9. `htons`/`htonl` implicit

`host/net.c` relied on glibc's transitive declarations. newlib wants
`<arpa/inet.h>`; including it fixed the build.

### 10. Main-thread stack

The module's first `NitroMain` runs before any fiber switch. The libnx
loader's main-thread stack is not sized by the port. `host/main.c` now
runs `host_run` on an 8 MiB SDL thread; if that fails it falls back to the
loader stack. This mirrors the Android path.

### 11. Fibers and workers

`host/fiber.c` uses the arm64 stack switch for Switch (`HOST_FIBER_ASM`),
not `ucontext`. Each fiber has a 2 MiB native stack. `host/par.c` uses two
workers and 2 MiB native stacks. These are still assumptions from
`switch-plan.md` S0, not measured high-water marks.

### 12. Fixed memory, bounds checks and the failed guard pages

The first working memory path is `host/mem_switch.c`: one 1 GiB block
(`EIDOS_SWITCH_MEM_MB` overrides), allocated once and never moved;
`realloc` returns the same block; growth beyond the budget fails like
`memory.grow`. wasm2c is compiled with `WASM_RT_USE_MMAP=0` and
`WASM_RT_MEMCHECK_BOUNDS_CHECK=1`.

A guard-page experiment (`--guard-pages`,
`host/mem_switch_guard.c`) reserves the address space with libnx's
`virtmem` and maps physical pages with `svcMapMemory`. On the console the
first map fails:

```
eidos: svcMapMemory(268435456) failed
eidos: guard pages unavailable; using the fixed block
```

The fallback uses the fixed block, but the code was compiled for guard
pages (no bounds checks), so it is not a safe or a faster build. The
reason `svcMapMemory` rejects the range is unresolved. The likely issue is
that the source is a newlib heap range and its pages cannot be borrowed
the way the syscall expects; a dedicated physical allocation/mapping path
is needed.

Measurements in the guard build were not better than the bounds build, so
bounds checks are not the main reason the game is slow.

### 13. The GL presenter is black and slower than the CPU path

The GL presenter creates a context:

```
eidos: OpenGL NV120 (OpenGL ES 3.2 Mesa 20.1.0-rc3)
eidos: GPU 3D features 0x3, integer samples 8
eidos: presenter OpenGL 3.3 (video driver Switch)
eidos: 3D features the GPU cannot draw: 0x0
```

`0x3` means the edge-marking and fog shaders compiled; `0x0` means the
snapshot thinks the GPU can draw every feature, so the CPU 3D rasteriser is
not the fallback. Nevertheless the world is black/white and the field is
about 6–11 fps. Turning GPU 2D off (`EIDOS_G2D_GPU=0`) does not change it.
The GL path is therefore failing inside Mesa/GLES or in the 3D presentation
step, not in the CPU fallback decision.

With `EIDOS_RENDERER=sdl` (no GL context; CPU 3D and SDL rendering) the
world is visible and the field is about 22–24 fps, menus about 60. That is
the only playable configuration so far, but it is not the intended final
presenter and must not be made the default. The direct comparison shows
the GL path is currently both wrong and slower; that is the main rendering
bug to fix next.

`EIDOS_GPU3D_FORCE=1` is available to let the GPU draw unsupported features
anyway, and `EIDOS_GPU3D_REPORT=1` prints the masks. It has not yet been
tested on hardware in the final build.

### 14. Saves and firmware do not write

Every run prints:

```
eidos: nvram: cannot write the firmware flash (sdmc:/switch/eidos/profile/saves/firmware.bin); it stays in memory
```

The same path is used for save writes. The first fix attempt made
`bw_plat_sync_file` tolerate `EINVAL`/`ENOSYS`/`ENOTSUP` from `fsync`, but
the messages remain, so the failure is in an earlier step: `bw_plat_mkdirs`,
`fopen("....tmp","wb")`, `fwrite`, `fsync`, `fclose`, or
`bw_plat_replace_file`/`rename`. libnx's FAT layer's `fsync`/`rename`
behaviour is the prime suspect. The host's `wasi.path_rename` and
`fd_sync` need to be instrumented, or `bw_save_write_atomic` needs a
Switch-specific fallback that writes directly to the target and tolerates
the missing sync/link semantics.

### 15. `bw_ds_ptr: VRAM 0x06898000 has no bank mapped`

The logs contain:

```
eidos: bw_ds_ptr: VRAM 0x06898000 has no bank mapped; accesses there go nowhere, as on the DS
```

followed by `ds call: ... -> overlay code` lines. This is the VRAM window
that overlay 95 (the battle/side-effect overlay) uses. It is not yet known
whether this is a normal transient while the game maps LCDC bank H or
another cause of black frames. The Light Screen crash earlier in this
branch was also an ov095 VRAM code-pointer issue (see `docs/armdata.md`),
so investigate this message before concluding the GL path alone is at
fault.

### 16. Guard build and bounds build are separate artifacts

`build-switch` (fixed block, bounds checks) and `build-switch-guard`
(guard code, failed mapping) have different generated C. Do not mix their
objects. `switch/eidos-bounds.nro` and the guard NRO were kept separate
while testing. The current `switch/eidos.nro` name has been overwritten
several times; always record the SHA-256 with a test.

## Performance measurements

The measurements below are from the guard build on the console, 1280x720
handheld, 1x 3D, with `EIDOS_FPS_LOG=1`.

| run | presenter | menus | field | notes |
| --- | --- | --- | --- | --- |
| 01 | OpenGL (NV120) | 44–59 fps | 6–22 fps | black/white world; `EIDOS_GPU3D_REPORT` says unsupported `0x0` |
| 02 | OpenGL, `EIDOS_G2D_GPU=0` | 48–61 fps | 7–20 fps | still black/white; GPU 2D is not the cause |
| 03 | SDL (`EIDOS_RENDERER=sdl`) | ~60 fps | 22–24 fps | playable; CPU 3D |

All runs report `svcMapMemory(268435456) failed` and therefore use the
fixed block. The guard-page code is not in effect.

Overclocking the CPU and GPU does not help the GL path, which fits a
rendering/present problem rather than a clock problem. The SDL path is
CPU-bound in 3D.

Next performance work, in order:

1. Find why the GL path draws black and why it is slower than CPU 3D.
   Start with the display-capture/readback path (`DISPCAPCNT`), the GPU 2D
   composition, and `glReadPixels`/PBO use. `EIDOS_GPU3D_REPORT=1` says the
   feature mask is complete, so check the actual frame/readback path, not
   the missing shaders.
2. Fix the save/firmware write path (`fsync`/`rename` on libnx FAT).
3. With GL working, measure again; only then decide whether the remaining
   cost is wasm2c bounds checks, indirect calls or CPU 3D.
4. If CPU 3D remains the path, optimize it: `-O3`, worker parallelism, and
   the `switch-plan.md` S9 items. Do not treat overclocking as a solution.

## Reproducing the current diagnostic build

Container source: `~/eidos`; SDL source: `~/sdl-switch`; module:
`~/eidos/eidos_boot.wasm`.

```
cd ~/eidos
python3 switch/build.py \
  --module ~/eidos/eidos_boot.wasm \
  --build-dir ~/build-switch-guard \
  --sdl-source ~/sdl-switch \
  --guard-pages \
  --jobs 8
```

The output is `~/build-switch-guard/eidos.nro`. Copy it out:

```
docker cp hgss-p1:/home/hgss/build-switch-guard/eidos.nro switch/eidos.nro
docker cp hgss-p1:/home/hgss/build-switch-guard/eidos.elf switch/eidos.elf
```

For the bounds build, omit `--guard-pages` and use `build-switch`.

## Open questions

- Why does `svcMapMemory` reject a 256 MiB range taken from the newlib heap?
  The guard-page path needs a physical backing allocation that the kernel
  will map, not a borrowed heap range.
- Why is the Mesa GLES 3D path black when its feature mask is complete, and
  why is it slower than the CPU rasteriser? Is the display-capture readback
  (`eidos_platform_read3d_1x`) running every frame? Is the GPU 2D
  composition reading/writing the 3D target incorrectly?
- Why does `bw_save_write_atomic` fail on libnx FAT? Instrument `fopen`,
  `fwrite`, `fflush`, `fsync`, `fclose`, `rename` separately.
- Does the game's audio work on Switch? It was not reached in these tests.
- Do touch, gamepad mapping, dock/undock and the software keyboard behave
  as `switch-plan.md` S7 expects? Not tested.
- S0's real stack/memory numbers are still missing; 1 GiB, 2 MiB fibers and
  two workers are budgets, not measurements.
- The applet-mode message is unreachable with a 1.5 GiB heap request; if
  applet support matters, the heap request must be smaller or made after
  the applet check.
- The example `switch/eidos.nro`/`eidos.elf` files in the working tree are
  build artifacts, ignored by git; they are not part of the source.

## Commits and tree state

Committed on `switch/nro`:

- `4b7469d` — `switch: a devkitA64 NRO for title mode`
- `505f6a8` — `switch: a title-mode heap and a start-up trace`

The working tree at the time of this note has uncommitted work for the
diagnostic build: `demo/eidos_boot.c` start-up stages, `host/wasi.c` path
fixes, `host/sdl.c` EGL/FPS/window changes, `host/main.c` thread and
stages, `host/par.c` workers, `engine/g3d/hw3d.c` and
`engine/platform/plat_gl3d.c` GPU reports, `sdk/plat_save.c` `fsync`
tolerance, `install/location.c` device paths, `host/mem_switch_guard.c`,
and the `switch/` documentation. The default-SDL block in `host/main.c` is
an unaccepted experiment and must be removed before committing a default
presenter decision.

# eidos on the Nintendo Switch

A homebrew NRO, started from the homebrew menu in **title mode**. The game
is the same wasm32 module as every 64-bit build; `host/` is built with
devkitA64 and libnx, SDL3 is devkitPro's `switch-sdl-3.4` fork, and
`host/CMakeLists.txt` produces `eidos.nro`.

The detailed working record — what works, what does not, every failure and
the performance numbers — is in `switch/PORT-NOTES.md`.

Title mode only. The program asks libnx for a 1.5 GiB heap, which an
applet (a start from the album) does not have: an album start is closed by
the console before the program can print its title-mode message.

## Current status

The NRO boots, extracts a ROM and reaches the game world and menus. The
OpenGL presenter is currently **black and slow** (about 6–11 fps in the
field); `EIDOS_RENDERER=sdl` gives a playable but slow 22–24 fps and is a
control, not the intended default. Saves and the firmware flash do not
write yet. See `switch/PORT-NOTES.md` for the measurements and the open
bugs.

Hold R while starting a game and choose eidos.

## Building

Inside the devkitPro container or on a machine with devkitPro installed:

```
python3 switch/build.py --module build64/module/eidos_boot.wasm
```

The module comes from `tools/build64.py`; it is the same one the desktop,
Windows, macOS, Linux and phone builds use. `switch/build.py` does **not**
build it: rebuild stage one first whenever `demo/`, `engine/`, `sdk/`,
`install/` or the top-level CMake files change, then pass the new wasm:

```
cmake --build build64/module --target eidos_boot -j 8
```

`switch/build.py` configures `host/` with
`${DEVKITPRO}/cmake/Switch.cmake`, builds SDL3 through
`host/CMakeLists.txt`, and leaves `build-switch/eidos.nro`. A build from
the container tree is at `~/eidos`; copy the NRO out with `docker cp`.

Packages used (devkitPro pacman, 2026-11-02):

| Package | Version |
| --- | --- |
| `switch-dev` (devkitA64, libnx, switch-tools) | devkitA64 r30, libnx 4.12.0, switch-tools 1.13.1 |
| `switch-mesa` | 20.1.0-5 |
| `switch-libdrm_nouveau` | 1.0.1-2 |
| `switch-cmake` | 1.5.2-1 |
| `switch-pkg-config` | 0.28-4 |

Also CMake 3.28, Ninja 1.11 and Python 3.12.

devkitPro's SDL3 is fetched by `tools/fetch_sdl_switch.py` at commit
`c329016c0e338ab4397ad930f83330c9fe058348` of `switch-sdl-3.4`, checked
against its SHA-256. `--sdl-source DIR` uses a checkout instead.

EGL/GLES come from mesa-switch at commit
`0ff52079ca9e9e837ed27eb95a8d239f439a1864`, built once with
`python3 tools/fetch_mesa_switch.py --build` (see
`switch-mesa/sdk-build.md`; cache `~/.cache/eidos/mesa-switch`, override
with `EIDOS_MESA_SWITCH_CACHE`). `switch/build.py` uses the cached prefix
unless `--mesa-prefix DIR` names one.

## Installing

Copy `eidos.nro` to:

```
sdmc:/switch/eidos/eidos.nro
```

Put your own Pokémon Black or White ROM (`.nds`) in the same folder. The
first start finds it there, identifies and extracts it next to the NRO, and
asks nothing. The folder after setup:

| path | what |
| --- | --- |
| `eidos.nro` | the program |
| `eidos.nds` (or any `*.nds`) | your ROM; the first by name is used |
| `extract/` | the extracted game data |
| `profile/` | settings and saves |
| `args.txt` | extra arguments, one per line (`--press`, `--screenshot`, ...) |
| `env.txt` | `NAME=value` lines for the environment (`EIDOS_*`) |
| `stderr.txt` | the program's messages |
| `boot.txt` | how far start-up got, for a console with no nxlink |

The game runs from `sdmc:/switch/eidos/`. Saves are in
`sdmc:/switch/eidos/profile/saves/`.

On the homebrew menu, hold **R** while starting a game, then choose eidos.
A plain start from the album is applet mode and can be closed before the
title-mode message; the 1.5 GiB heap request is title-mode sized.

## Memory

The module's linear memory is one 1 GiB block allocated at start and never
moved; `EIDOS_SWITCH_MEM_MB` in `env.txt` changes the budget for a report.
libnx is asked for a 1.5 GiB heap for that block and SDL3/Mesa; the title
mode limit is the reason an album start is not supported. A `memory.grow`
past the budget fails the way wasm's does, and the program prints
`module memory budget reached`. Each game fiber has a 2 MiB native stack,
and two compositor workers have 2 MiB each. These are the limits the plan's
S0 probe should replace with measured numbers.

## Performance

The Switch build starts at 1x 3D resolution, uses the screen's own size
(1280x720 handheld, 1920x1080 docked), and gives the compositor two workers.
The in-game settings still override the resolution.

The current build maps the module's memory with explicit bounds checks
(`host/mem_switch.c`). The guard-page experiment
(`python3 switch/build.py --guard-pages ...`) reserves the address space
with libnx's `virtmem` and maps physical pages with `svcMapMemory`. The
reservation comes from the stack region (`virtmemFindStack`), the only
region `svcMapMemory` maps into; an earlier ASLR-region reservation made
the first 256 MiB map fail (`svcMapMemory failed`) and fall back to the
fixed block. Bounds checks
were not the measured bottleneck.

Useful `env.txt` switches while measuring:

| name | effect |
| --- | --- |
| `EIDOS_FPS_LOG=1` | one frame-rate line a second on stderr |
| `EIDOS_RENDERER_REPORT=1` | names the presenter (`OpenGL 3.3` or `SDL renderer`) |
| `EIDOS_GPU3D_REPORT=1` | GPU 3D features and unsupported mask |
| `EIDOS_GPU3D_FORCE=1` | let the GPU draw unsupported features (diagnostic) |
| `EIDOS_G2D_GPU=0` | force CPU 2D composition |
| `EIDOS_NOWAIT=1` | no frame pacing: measures raw frame cost |
| `EIDOS_RENDERER=sdl` | forces the SDL renderer, for comparison |
| `EIDOS_SWITCH_MEM_MB=N` | module-memory budget, default 1024 MiB |
| `EIDOS_WASI_TRACE=1` | log failed module file opens |

## Messages and crashes

`stderr.txt` has the program's messages. With `EIDOS_NXLINK=1` in
`env.txt`, they go to `nxlink -s` on a PC instead (devkitPro's
`switch-tools` has `nxlink`). Saves currently report
`nvram: cannot write the firmware flash` and do not persist; see
`switch/PORT-NOTES.md`.

If the console closes with its generic error and no message, read
`boot.txt`: its last line says how far start-up got. No file at all means
libnx could not get the 1.5 GiB heap, which is what an applet start (no R)
looks like.

A wasm trap prints `eidos: the game stopped (wasm trap: ...)` and exits.
The generated C keeps the game's function names, so a crash report's
addresses, when present, turn into function names with:

```
aarch64-none-elf-addr2line -f -e build-switch/eidos.elf <address>
```

The NRO is a homebrew program and is not signed; title mode is the
console's own way of giving it an application's memory.

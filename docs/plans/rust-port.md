# eidos in Rust: implementation plan

How eidos's own port code — `install/`, `engine/`, the SDK layer above the
DS machine, and `host/` — moves from C to Rust in place, one module at a
time. Written 2026-11-02 against eidos `000cf646c8`, the aletheia pin in
`docs/aletheia-pin.md`, and the build described in `docs/lp64.md`.

This is not a rewrite of the game. The matched C that aletheia verifies
against the DS image stays C. The DS machine layer stays C. What moves is
the code above them: the installer, the renderers, the file and save layer,
the mod layer, the interface, and the 64-bit host. The plan is a companion
to `switch-plan.md`, not a replacement: nothing here is required for the
Switch milestones, and the Switch build is the last target it touches.

The method is incremental. Every module has a C implementation and a Rust
implementation for a while; the existing tests and oracles decide when the
C one is deleted. That is the only way to land this in the repository's
one-PR-per-change process without freezing feature work.

## Decisions

These are fixed. Changing one needs a written reason in `docs/rust.md` and
a decision recorded in "Results".

1. **The game stays C.** Nothing under aletheia's `src/matched/` or
   `src/game/`, and nothing in `stubs/`, is ported, rewritten or wrapped in
   Rust. Those files are byte-verified C and their ABI is the DS's. Rust
   replaces port code only.

2. **The DS machine stays C.** The 32-bit address space and translation
   (`sdk/dsmem.c`, `sdk/host_vm.c`), the OS thread, interrupt, alarm and
   tick layer (`sdk/os_thread.c`, `os_irq.c`, `os_interrupt.c`,
   `os_alarm.c`, `os_tick.c`, `os_system.c`, `os_argument.c`), the heap and
   arena (`sdk/os_alloc.c`, `sdk/os_arena.c`), the CPU-side hardware shims
   (`sdk/cp.c`, `mi.c`, `mi_dma.c`, `mi_gx.c`, `gx.c`, `g3_fifo.c`), PXI and
   the ARM7 stand-in (`sdk/pxi.c`, `sdk/arm7/` except the sound core),
   overlays and the ARM9 image (`sdk/overlay.c`, `overlay_image.c`,
   `armdata.c`), the platform primitives (`sdk/plat_thread.c`,
   `plat_crash.c`, `wasm_heap.c`), and the diagnostics and symbol glue
   (`sdk/panic.c`, `game_globals.c`, `aliases.c`, `port_reset.c`, `tcbl.c`,
   `wasm_layout.h`). This is the unsafe core. Rust would make its `unsafe`
   explicit without making the machine safer, and porting it would put the
   game's exact memory model at risk. The ARM7 sound core
   (`sdk/arm7/snd_driver.c`, `snd_seq.c`, `snd_hw.c`) is engine, not
   machine, and is in scope with `engine/audio/`; the PXI transport
   (`sdk/arm7/snd.c`, `arm7.c`) stays C.

3. **In scope.** All of `install/` except the platform picker and its macOS
   dialog; all of `engine/` except the GL binding files; the SDK files
   listed in Appendix A; and `host/` except the fiber assembly, the worker
   pool and the Switch memory allocator. The engine's hardware models
   (`g2d/hwcompose`, `g3d/hw3d`, the ARM7 sound core) are engine and are in
   scope; the machine layer in decision 2 is not. The wasm32 module
   contains almost all of this, so module crates are built for wasm32 as
   well as i686, armv7 and aarch64; `host/` alone is a native program.

4. **The build stays wasm32 + wasm2c.** The module is still compiled by
   wasi-sdk and translated by wasm2c; the host still uses SDL3 and the
   generated GL imports. No wasmtime, no JIT, no second runtime. The
   equivalent on the Switch is still the fixed-budget wasm2c path in
   `host/mem_switch.c`.

5. **One workspace, one FFI facade.** Rust lives under `rust/`, as a Cargo
   workspace of thin crates. `rust/eidos-ffi` is the only module crate that
   exports `#[no_mangle]` C symbols; every other module library is used by
   it. `eidos-host` exports the wasm2c import functions, and `eidos-tools`
   is a CLI. The C headers remain the ABI contract; the FFI facade
   implements it.

6. **No behaviour change.** A port must produce the same bytes, pixels,
   audio samples, files, and the logs that matter, and the same exit codes
   as the C it replaces. The existing C tests, the Python oracles and the
   extracted data are the gate.

7. **No big-bang.** The C file stays in the tree until the Rust version
   passes the old tests and one release has shipped with both paths
   selectable. Removal is a separate PR.

8. **Small modules are part of the work.** Splitting a 5,000-line file into
   modules is a deliverable, not a side effect. The size rules below apply
   to C during the split and to Rust after the port.

9. **Unsafe is a budget.** `unsafe` is allowed only in `eidos-guest`,
   `eidos-ffi`, `eidos-io`'s OS backends, `eidos-sync`'s primitive, the
   native and wasm mod loaders, the platform's SDL/GL bindings, and
   `eidos-host`'s wasm2c/SDL modules. Everything else is
   `#![forbid(unsafe_code)]`. The budget is checked in CI and recorded in
   `docs/rust.md`.

10. **CMake remains the build driver.** Cargo is a sub-build invoked by
    CMake, with a committed `Cargo.lock`, `--locked`, and no network access
    at build time. `tools/check.py` and the release workflow keep working.

11. **Tests and oracles are the gate.** `ctest`, `tools/check.py`,
    `tools/build64.py`, the pixel and audio oracles, and the extraction
    differential tests must pass for every change. A module that cannot be
    proved equivalent stays C.

12. **Switch last.** Desktop and the wasm build come first. Android and
    iOS follow when their targets are green. The Switch build is attempted
    only after R10, and may stay entirely C if the tier-3 Rust target
    cannot be made to work cleanly.

13. **Dependencies are pinned and licensed.** Every crate is in
    `Cargo.lock`, checked by `cargo-deny` for licence and advisories, and
    vendored or cached for offline CI. No crate is added for a one-line
    helper.

14. **The repository rules win.** `src/eidos/AGENTS.md` applies: one PR
    per coherent piece, a topic branch per PR, docs and `CHANGELOG.md` in
    the same PR, every commit authored and committed by
    `terezi <jules@foxtails.me>`, no agent attribution. No drive-by
    changes outside the task.

## Rules for whoever does a task

Read before writing: `AGENTS.md`, `docs/architecture.md`, `docs/lp64.md`,
`docs/runtime.md`, `docs/building.md`, the C file and its header in full,
and the test that covers it. Run the test before changing anything.

- **Split before porting.** A file over 600 lines, or with more than one
  responsibility, is split in C first. The split is behaviour-preserving
  and lands as its own PR. This is also the answer to "the files are too
  big": it does not depend on the Rust port finishing.
- **Size limits.** Rust files target 200–400 lines, hard limit 600;
  functions target 60 lines, hard limit 100; at most 6 arguments; at most
  4 levels of nesting. `clippy.toml` sets `too-many-lines-threshold = 100`,
  `too-many-arguments-threshold = 6`, `type-complexity-threshold = 250`.
  `tools/rust_limits.py` fails CI on a file over 600 lines.
- **One concept per file.** A module owns one thing and exports it. `mod.rs`
  only declares submodules and re-exports the narrow public facade. No glob
  imports, no `pub use *`.
- **Split by role.** A module is one of `model` (data and invariants),
  `pure` (parsing, evaluation, layout), `io` (files, sockets, SDL), or `ffi`
  (the C ABI). A 2,000-line file is usually a model function that also does
  I/O and drawing; separating the roles is what makes it small.
- **Newtypes at boundaries.** `DsAddr`, `DsRange`, `GuestPtr`, `Fx32`,
  `ScreenPos`, `CanvasPos`, `Frame`, `Tick`, `PathUtf8`. Do not pass `u32`
  where a DS address and a length are both possible.
- **No transmute of guest bytes.** Read little-endian explicitly with
  `u16::from_le_bytes` / `to_le_bytes`. This is safe, endian-explicit and
  avoids alignment undefined behaviour.
- **No `&mut [u8]` into guest memory escapes a call.** The game's C can
  write through its own pointers at any time. Use `eidos-guest`'s
  `read`/`write`/`copy` or a `with_bytes` closure documented "do not call C
  here", or return raw pointers only at the C ABI.
- **No `std::thread`, no `std::sync::Mutex` in the module.** The module's
  threads are host fibers under one run token. Use `eidos-sync`.
- **No `unwrap`, `expect` or `panic!` in production code.** Return an enum
  error and let the FFI layer map it to `-1`/`NULL` with the `err` buffer.
  `debug_assert!` is for invariants only. A Rust panic aborts.
- **Panic is abort.** `panic = "abort"`, a `#[panic_handler]` that calls
  `bw_panic` with file and line, and no unwinding across C. No Rust
  `catch_unwind`, no `longjmp` interaction.
- **FFI is thin.** An `extern "C"` function converts arguments, calls safe
  code, and converts the result. It contains no game logic. `#[no_mangle]`
  appears only in `rust/eidos-ffi` for the module, and in `eidos-host` for
  the wasm2c imports.
- **Layout is proved.** Every `#[repr(C)]` type crossing the boundary has
  a `const` size/offset assertion and a C test that prints the C side.
  Bitfields use explicit masks and accessors, never Rust bitfields.
- **Float determinism.** Audio calls the same C libm entry points as the C
  code; the audio oracle is byte-exact. The rest may use Rust `f32`, with
  no fast-math.
- **Test the old way and the new way.** The C test suite runs against the
  Rust module. Pure functions get Rust tests and fuzzing; stateful modules
  get a snapshot differential test.
- **Benchmark hot paths.** Rasteriser, mixer, compositor, UI frame and
  extraction are measured before and after. A module more than 2% slower
  on the frame path, or 10% slower on extraction, is profiled or kept C.
- **Docs in the same PR.** `docs/rust.md` records the module, its tests,
  its `unsafe` surface and its measured numbers. `docs/building.md` and
  `CHANGELOG.md` change when the build or behaviour does.
- **Report, do not guess.** If the linker, an ABI, a crate or a target does
  not behave as this plan says, write down what was seen and stop. Do not
  work around it silently.

## The parts that matter

### The current build

Stage one is the game, the port and the engine as one wasm32 module,
compiled by wasi-sdk (`tools/build64.py`). It contains `sdk/`, `engine/`
(including `engine/platform` and `engine/ui`), `install/` and
`demo/eidos_boot.c`; SDL3 is compiled against headers only and its calls
become imports. Stage two is `host/`: wasm-opt, wasm2c, the generated C,
`host/`, and SDL3. The native 32-bit builds compile the same stage-one
sources directly.

Consequences that shape every crate:

- A module crate must build for wasm32, i686 and armv7, and later aarch64.
  It is not a normal native library.
- `install/` and `engine/platform/` are in the module, so they are not
  native-only. `std::fs` and `std::thread` are not automatically available.
- The host is a separate native program. It does not contain the port.
- The module's SDL3 and GL calls are imports the host answers. Rust code
  must declare those imports itself or call the existing C shims.

### Sizes

Measured from the tree at `000cf646c8`:

| Part | Lines | Notes |
|---|---:|---|
| aletheia `src/matched` | ~241k | 33,572 files; unchanged |
| aletheia `src/game` | ~924k | unchanged |
| eidos `sdk/` (all `.c`) | ~70.9k | 48.7k in scope, 22.3k machine |
| eidos `engine/` | ~63.7k | includes `platform` 16.1k, `ui` 24.1k |
| eidos `install/` | ~4.2k | 3.6k in scope |
| eidos `host/` | ~3.4k | 2.7k in scope |
| eidos `stubs/` | ~8.9k | unchanged |
| launcher (already Rust) | ~3.5k | not part of this plan |

The in-scope total is roughly 118,000 lines before subtracting the
out-of-scope GL bindings, pickers and host primitives. That is the size of
the project, not an estimate of a sprint.

### Cycles to break before crates

The current includes have four cycles that a crate graph cannot express:

- `engine/g2d/par.c` and `engine/frame/prof.c` use `plat_thread`.
- `sdk/os_tick.c` uses `engine/frame`.
- `sdk/mi_gx.c` and `sdk/g3_fifo.c` use `engine/g3d/hw3d`.
- `engine/world/bmap_*` uses `port.h` path resolution.

The fixes are architectural, not Rust-specific: move the thread primitive
below the engine (`eidos-sync`), move the tick counter into
`eidos-types`/`eidos-time` (with a two-function C ABI in `eidos-ffi`, or a
small C `tick.c`, so the C `os_tick.c` can call it), make `hw3d` part of
`eidos-hw` and let the SDK depend on it, and move path resolution into
`eidos-io`.

### Commands

The existing gates, which every task must keep green:

```sh
cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=ON -DEIDOS_OVERLAYS=ALL
cmake --build build
ctest --test-dir build --output-on-failure

python3 tools/build64.py --build-dir build64 --aletheia-root ../aletheia --fast
python3 tools/check.py
```

The new switch, off by default until R12:

```sh
cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=ON -DEIDOS_RUST=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Target architecture

### Crates

```
eidos-types      u8..s64, fx32, fixed-point, endian readers, newtypes, layout
eidos-io         Read/Seek/Write, files, dirs, paths, save folder, atomic rename, space
eidos-sync       mutex/condvar/thread/token over plat_thread; RunToken
eidos-guest      typed access to the C address space; no ownership of it
eidos-hw         g2d, g3d, world, audio, frame
eidos-os         fs, vfs, narc, save, firmware, input, langmap
eidos-mod        api, manifest, events, state, rules, loader, net, mp
eidos-ui         canvas, kit, recipes
eidos-platform   settings, wizard, presenter, SDL/GL externs
eidos-install    sha1, blz, rom identify/extract, setup
eidos-ffi        the only #[no_mangle] facade; links the C ABI
eidos-host       stage two: wasm2c FFI, WASI, fibers, imports (std)
eidos-tools      eidos_extract CLI, development tools (std)
```

Dependency direction:

```
eidos-types
    |
eidos-io     eidos-sync
    \          /
     eidos-guest
          |
      eidos-hw
          |
      eidos-os
          |
      eidos-mod
          |
      eidos-ui
          |
    eidos-platform
          |
     eidos-ffi
```

`eidos-install` depends on `eidos-types` and `eidos-io` only.
`eidos-host` depends on `eidos-types`, `eidos-io`, `eidos-install` and the
C wasm2c runtime; it does not depend on the module crates (`eidos-hw`,
`eidos-os`, `eidos-mod`, `eidos-ui`, `eidos-platform`). Its binary is built
by `host/CMakeLists.txt`, which supplies the generated C objects; its pure
modules are tested with a feature that does not link wasm2c.

`eidos-guest` does not replace `sdk/dsmem.c`. It is a safe, typed wrapper
over the C functions (`bw_ds_ptr`, `bw_ds_addr_of`, `bw_io_read*`,
`bw_io_write*`, `bw_vram_*`, the region constants). The machine stays C;
Rust gets a disciplined way to use it.

### Features

- `std`: tests, `eidos-host`, `eidos-tools`. Not used by module crates in
  their normal build.
- `wasi` / `native`: `eidos-io`, `eidos-sync` and `eidos-platform` backend
  selection. Prefer `cfg(target_arch = "wasm32")` where it is enough.
- `switch`: the Switch host and its fixed-budget behaviour.
- `sdl`, `gl`: platform externs.
- `tls`, `wamr`: optional dependencies, as the C build has them.
- `module`: the wasm/native 32-bit configuration. It is the default for
  the module crates and excludes host-only code.

Keep the feature set small. A feature that only exists to compile a test
belongs in `cfg(test)`.

### The FFI boundary

- `rust/eidos-ffi` depends on every module crate and defines the C symbols
  the rest of the build expects. No other module crate uses `#[no_mangle]`;
  `eidos-host` is the separate export surface for the wasm2c imports.
- Function signatures match the C headers exactly. `u32`, `i32`, `u16`,
  `u8`, `i8` are written explicitly; `c_ulong` is used only where the C
  declares `unsigned long` (the matched SDK's `bw_sdk_u32`).
- Every `#[repr(C)]` type that crosses the boundary has a `const` assertion
  for size and each field offset, plus a C test (`test_rust_layout.c`)
  that prints the C numbers. The test is run on wasm32, i686 Linux, i686
  Windows and armv7, not only on the development host.
- Function pointers are only passed where the C prototype is the same
  type. The build uses `--fpcast-emu` because matched code calls through
  pointers of other prototypes; that glue stays in C. A Rust function is
  never placed in a slot that C calls through a mismatched type.
- Weak symbols, aliases, variadic functions and `setjmp`/`longjmp` stay in
  C. Rust does not try to reproduce the linker's weak-symbol rules.
- `char` is not shared as `c_char`. The C build uses `-fsigned-char`; Rust
  uses `i8`/`u8` explicitly in shared structs.
- Bitfields are not `#[repr(C)]` bitfields. The C layout is exposed through
  explicit masks and accessor functions in `eidos-ffi` or `eidos-guest`.
- Packed structs are read and written with `read_unaligned` /
  `write_unaligned`. No reference is taken to a packed field.

### The layout trap

The C build deliberately changes struct layout on 32-bit hosts:

- i686 Linux's System V ABI aligns a `u64`/`double` to 4 in a struct.
  `docs/lp64.md` recommends `-malign-double` to match the DS's ARM EABI for
  the matched sources, and records that 12 records still differ today.
- i686 Windows and wasm32 are compiled with `-mno-align-double` /
  `wasm_layout.h`'s `#pragma pack(4)`, so the port's own structs are
  4-aligned, except the native-layout files below.
- Rust's `repr(C)` follows the platform C ABI: 4 on i686 Linux, 8 on i686
  Windows, 8 on wasm32. The C side is deliberately not the same on all
  three; do not assume Rust matches without measuring.

The wasm32 build adds a second wrinkle: `wasm_layout.h`'s `#pragma pack(4)`
applies to the game and the port sources, but not to the files that hand
structs to the C library (`engine/platform/*.c`, `install/*.c`,
`sdk/plat_*.c`, `sdk/mod_wasm.c`), which keep the native layout. A struct
shared between those files and the packed code already has two layouts. A
Rust port must match the layout at each boundary, or avoid sharing that
struct entirely.

The policy is therefore:

1. Do not share a struct with a `u64`, `double` or a pointer-sized field
   across the Rust/C boundary unless it has been measured on every target.
2. Prefer scalar and byte-buffer FFI (`u32` addresses, `&[u8]`, explicit
   `read`/`write`) for anything with 64-bit members.
3. Where a struct must cross, use `#[repr(C)]` only after
   `test_rust_layout.c` proves it on all targets. If it cannot be made to
   match, use a byte-level accessor or keep that module C.
4. `BwSha1`, settings structs, `BwRomInfo`, the mod payloads and every
   `u64`-bearing record are the first to check.

Every port task that introduces a shared struct owes a layout test in the
same PR.

### The unsafe core

| Crate | Allowed unsafe | What it wraps |
|---|---|---|
| `eidos-guest` | yes | `dsmem`, address translation, I/O hooks, VRAM |
| `eidos-io` | only in `os/` backends | `FILE*`, UTF-8/UTF-16 paths, `statvfs`, rename |
| `eidos-sync` | yes, one primitive | `plat_thread`, atomics, the run token |
| `eidos-ffi` | yes | the `extern "C"` surface |
| `eidos-mod` | loaders only | `dlopen`/`LoadLibrary`, WAMR, function pointers |
| `eidos-platform` | `sys` module only | SDL3, GL, Cocoa/Win32 dialogs |
| `eidos-host` | yes | wasm2c, SDL3, GL, fibers, OS calls |
| everything else | no | `#![forbid(unsafe_code)]` |

`eidos-guest` exposes `read_u8/16/32`, `write_u8/16/32`, `copy_from`,
`copy_to`, `with_bytes`, and `as_ptr` for the FFI only. It does not hand
out long-lived slices. `with_bytes` is documented "the closure must not
call back into C".

### Concurrency and guest memory

The module's threads are cooperative fibers under one run token; only the
token holder touches the machine. Mirror that in Rust:

```rust
pub struct RunToken { /* !Send, !Sync */ }

impl Guest {
    pub fn acquire(&self, _: &RunToken) -> GuestRef<'_> { /* ... */ }
}

pub struct GuestRef<'a> { /* !Send, !Sync, borrows the Guest */ }
```

`GuestRef` is the only way to read or write guest memory. It is `!Send` and
`!Sync`, so the borrow checker enforces "one thread at a time" wherever it
can. Static machine state lives in a `Guest` behind `UnsafeCell` with a
checked initialisation, never in `static mut`. A C callback that re-enters
Rust must not hold a `GuestRef`; the rule is documented at the callback and
checked by review.

### Allocation, panic and math

- **Allocator.** `#[global_allocator]` calls the module's C `malloc`,
  `free`, `realloc` and `posix_memalign` (all already wrapped by
  `sdk/wasm_heap.c`). Rust and C share one heap. No second arena.
- **OOM.** `handle_alloc_error` calls `bw_panic("rust allocation", 0,
  "out of memory")` and aborts.
- **Switch budget.** `switch-plan.md`'s S0 statistics must include Rust's
  allocations and code size. The fixed budget is not "C plus Rust"; it is
  the measured total.
- **Panic.** `panic = "abort"`; the handler prints file and line and calls
  `bw_panic`. No formatting machinery beyond what the handler needs.
- **Code size.** `opt-level = 2`, `lto = "fat"`, `codegen-units = 1`,
  `panic = "abort"`, `strip` in release except the debug-name needs of
  crash reports. Measure the module before and after every large port.
- **Math.** Audio calls the same C libm functions as the C code, because
  the oracle is byte-exact. Fixed-point is integer arithmetic with
  `wrapping_*`. The 3D engine may use Rust `f32`; no fast-math. On i686,
  the C audio build already forces SSE2; Rust's i686 targets do too, but
  confirm with the oracle.

## Module map

This is the target decomposition. A file listed here is split in the C
phase and ported in the Rust phase. Files not listed stay C (Appendix A).
Where a row names one module that is still over 600 lines, that module is
split further by the sections it already has; the row names the boundary
and the owner, not the final file count. The C split in R1 follows the same
rule.

### install

| C now | Rust modules |
|---|---|
| `install/sha1.c` | `install/sha1.rs` |
| `install/blz.c` | `install/blz.rs` |
| `install/ndsrom.c` | `install/rom/{header,identify,verdict,fat,fnt,plan,extract,banner,names}.rs` |
| `install/fsutil.c` | `eidos-io/src/fs/{file,dir,path,atomic,space}.rs` |
| `install/setup.c` | `install/setup/{home,config,check,args}.rs` |
| `install/location.c` | `install/setup/location.rs` |
| `install/extract_main.c` | `eidos-tools/src/bin/eidos-extract.rs` |
| `install/picker.c` | stays C (platform dialog) |

`ndsrom.c` splits along its existing sections: helpers, name mangling,
image reading, identify, the plan, the FNT walk, writing, the output
directory, extract. The plan is built and checked before anything is
written; that property stays.

### engine/g2d

| C now | Rust modules |
|---|---|
| `lzss.c` | `g2d/lzss.rs` |
| `resources.c` | `g2d/{ncgr,nclr,ncer,nanr,nmcr,nscr,palette}.rs` |
| `affine.c` | `g2d/affine.rs` |
| `compose.c` | `g2d/obj.rs`, `g2d/compose.rs` |
| `rig.c` | `g2d/rig.rs` |
| `bg.c` | `g2d/bg/{map,raster,index}.rs` |
| `bg_affine.c` | `g2d/bg_affine.rs` |
| `window.c` | `g2d/window.rs` |
| `hwcompose.c` | `g2d/hwcompose/{screens,layers,compose}.rs` |
| `par.c` | `g2d/par.rs` (uses `eidos-sync`) |

`g2d.h` splits into one header/module per container plus a small facade.
`resources.c` is a dispatch table today; each container becomes its own
parser with a shared `Resource` trait for the common header checks.

### engine/g3d

| C now | Rust modules |
|---|---|
| `mtx.c` | `g3d/mtx.rs` (fx32 matrices, stack, frustum) |
| `texture.c` | `g3d/texture.rs` (seven formats) |
| `nsbmd.c` | `g3d/nsbmd/{header,dict,model,texture,mesh}.rs` |
| `gx.c` | `g3d/gx/{parser,commands,state}.rs` |
| `raster.c` | `g3d/raster/{clip,span,depth,blend,texture}.rs` |
| `light.c` | `g3d/light.rs` |
| `hw3d.c` | `g3d/hw/{status,command,texture,swap,execute,protocol}.rs` |

`hw3d.c` is the largest file after the UI recipes. Its sections are
already the module boundaries: status registers, running one command, the
tests, textures out of VRAM, `SWAP_BUFFERS`, executing a command, the
access protocol, and the public calls.

### engine/world

| C now | Rust modules |
|---|---|
| `bmap_data.c` | `world/bmap/{zone,chunk,mesh,decode,build,texture}.rs` |
| `bmap_place.c` | `world/place/{parse,probe,spots,plan}.rs` |
| `bmap_gimmick.c` | `world/gimmick.rs` |
| `bmap_npc.c` | `world/npc.rs` |
| `bmap_spots.c` | `world/spots/{parse,table}.rs` |
| `bcam_sim.c` | `world/bcam/{sim,probe,path,matrix}.rs` |
| `bmap_stub.c` | removed when the world port is complete |

### engine/audio

| C now | Rust modules |
|---|---|
| `sdat.c` | `audio/sdat/{symb,info,fat,file}.rs` |
| `sbnk.c` | `audio/sbnk.rs` |
| `swav.c` | `audio/swav/{war,wave,adpcm}.rs` |
| `sseq.c` | `audio/sseq/{parser,vm}.rs` |
| `mixer.c` | `audio/mixer/{voice,envelope,psg,resample}.rs` |
| `player.c` | `audio/player/{sequence,stream}.rs` |
| `sdk/arm7/snd_driver.c` | `audio/arm7/driver/{state,commands}.rs` |
| `sdk/arm7/snd_seq.c` | `audio/arm7/seq/{parser,vm}.rs` |
| `sdk/arm7/snd_hw.c` | `audio/arm7/hw/{regs,channels}.rs` |

The mixer, player and ARM7 core are one exactness domain. They move
together or not at all; a half-Rust audio path is not worth the risk.

### engine/frame

`frame.c` → `frame/loop.rs`, `frame/clock.rs`; `prof.c` →
`frame/prof.rs`. The tick counter moves to `eidos-types::time` (or a small
`eidos-time`) so `sdk/os_tick.c` and the frame loop no longer include each
other.

### engine/ui

| C now | Rust modules |
|---|---|
| `ui.c` | `ui/{canvas,view,mouse,frame,hit,intent,game_queue,scene}.rs` |
| `kit.c` | `ui/kit/{draw,text,panel,row,icon,sprite,blend}.rs` |
| `assets.c`, `assets_icons.c`, `assets_sprites.c` | `ui/assets/{load,icons,sprites,atlas}.rs` |
| `ui_lang.c` | `ui/lang.rs` |
| `padglyph.c` | `ui/padglyph/{glyph,table}.rs` |
| `recipe_battle.c` | `ui/recipes/battle/{state,text,layout,action,move,target,bag,party,prompt,level_up,shared,group}.rs` |
| `recipe_pc.c` | `ui/recipes/pc/{state,party,boxes,hand,items,layout,draw,input}.rs` |
| `recipe_menus.c` | `ui/recipes/menus/{options,start,trainer_card,badge_case}.rs` |
| `recipe_party.c` | `ui/recipes/party/{model,layout,draw,input}.rs` |
| `recipe_bag.c` | `ui/recipes/bag/{model,layout,draw,input}.rs` |
| `recipe_dex.c` | `ui/recipes/dex/{model,layout,draw,input}.rs` |
| `recipe_field.c` | `ui/recipes/field/{model,layout,draw,input}.rs` |
| `recipe_title.c` | `ui/recipes/title/{model,layout,draw,input}.rs` |
| `recipe_shop.c` | `ui/recipes/shop/{model,layout,draw,input}.rs` |
| `recipe_settings.c` | `ui/recipes/settings.rs` |
| `recipe_input.c` | `ui/recipes/input/{model,layout,draw,input}.rs` |
| `recipe_starter.c` | `ui/recipes/starter.rs` |
| `recipes.c` | `ui/recipes.rs` (the registry) |

The recipe vtable becomes a Rust trait plus a registry:

```rust
pub trait Recipe {
    fn name(&self) -> &'static str;
    fn matches(&self) -> bool;
    fn run(&mut self, ctx: &mut Ctx, view: &mut View, input: &Frame);
    fn reset(&mut self) {}
}
```

The C `eidos_ui_frame` entry point stays and drives the registry. A recipe
never replaces game logic; it reads game state and queues taps/presses, as
today.

### engine/platform

| C now | Rust modules |
|---|---|
| `settings.c` | `platform/settings/{model,file,apply,bindings,network,session,panel}.rs` |
| `settings_ui.c` | `platform/settings_ui/{page,row,edit,question,draw}.rs` |
| `platform.c` | `platform/{window,input,pacer,audio,presenter,panel,layout}.rs` |
| `wizard.c` | `platform/wizard/{state,draw,input}.rs` |
| `mod_options_proc.c` | `platform/mod_options/{table,proc,draw}.rs` |
| `mod_panel_draw.c` | `platform/mod_panel.rs` |
| `geometry.c` | `platform/geometry.rs` |
| `font.c` | `platform/font.rs` |
| `text_instant.c` | `platform/text_instant.rs` |
| `plat_replace.c` | `platform/replace.rs` |
| `plat_embed.c` | `platform/embed.rs` |
| `plat_gl*.c` | stays C; called through `platform-sys` |
| `icon_rgba.h` | generated asset (`include_bytes!`), not a translated header |

The SDL3 entry points are declared `extern "C"` in `platform/sys.rs` or
kept in a small C shim. The window, input, device and pacer state machines
move to Rust; the GL call sequences do not. This is the "wrap, do not
rewrite" line: the value is in the state machine, not in retyping GL calls.

### sdk (in scope)

| C now | Rust modules |
|---|---|
| `fs.c` | `os/fs/{file,archive,open,read}.rs` |
| `vfs.c` | `os/vfs/{root,resolve,mods}.rs` |
| `vfs_narc.c` | `os/vfs/narc/{read,merge}.rs` |
| `vfs_msg.c` | `os/vfs/msg.rs` |
| `vfs_sdat.c` | `os/vfs/sdat/{read,merge}.rs` |
| `narc.c` | `os/archive/narc.rs` |
| `inflate.c` | `os/archive/inflate.rs` |
| `save.c` | `os/save/{folder,atomic}.rs` |
| `plat_save.c` | `eidos-io/src/fs/save.rs` |
| `plat_dir.c` | `eidos-io/src/fs/dir.rs` |
| `save_slots.c` | `os/save/slots.rs` |
| `firmware.c` | `os/system/firmware.rs` |
| `langmap.c` | `os/system/langmap/{map,bytes}.rs` |
| `input.c` | `os/input/{state,script,touch}.rs` |
| `battle_view.c` | `mod/views/battle/{state,view,draw}.rs` |
| `field_fade.c` | `os/game/field_fade.rs` |
| `battle_fixed.c` | `os/game/battle_fixed.rs` |
| `net_link.c` | `mod/net/link.rs` |
| `mod_rule.c` | `mod/rules/{limits,value,tables,compiled,lexer,parser,check,eval,cells,registry,file,errors}.rs` |
| `mod_manifest.c` | `mod/manifest/{record,parse,apply,types,namespace,errors}.rs` |
| `mod_state.c` | `mod/state/{table,handles,battle,save,accessors}.rs` |
| `mod_context.c` | `mod/state/context.rs` |
| `mod_event.c` | `mod/events/{dispatch,binding,scope}.rs` |
| `mod_event_session.c` | `mod/events/session.rs` |
| `mod_event_items.c` | `mod/events/items/{items,money,heal}.rs` |
| `mod_event_mon.c` | `mod/events/mon/{party,box,battle,fields,shared}.rs` |
| `mod_event_field.c` | `mod/events/field/{world,encounters,shared}.rs` |
| `mod_event_battle.c` | `mod/events/battle/{state,events,fields,shared}.rs` |
| `mod_loader.c` | `mod/loader/{native,table,bind,overrides,stop}.rs` |
| `mod_wasm.c` | `mod/loader/wasm.rs` |
| `mod_table.c`, `mod_slot.c`, `mod_hooks.c` | `mod/api/{table,slot,hooks}.rs` |
| `mod_savedata.c` | `mod/state/savedata/{file,envelope,versions,migrate}.rs` |
| `mod_export.c` | `mod/state/export/{read,write}.rs` |
| `mod_bw.c` | `mod/packages/bw/{archive,unpack}.rs` |
| `mod_net.c` | `mod/net/{protocol,socket,tls,queue,session,control}.rs` |
| `mp.c` | `mod/mp/{host,joiner,shared,events,positions,avatars,calls}.rs` |
| `tls_roots.c` | `mod/net/tls_roots.rs` |
| `mod_frame.c` | `mod/frame.rs` |
| `mod_avatar.c` | `mod/avatar/{state,render,net}.rs` |
| `mod_restart.c` | `mod/restart.rs` |
| `mod_handoff.c` | `mod/handoff/{state,transfer}.rs` |
| `mod_menu.c` | `mod/menu/{model,page,draw,input}.rs` |
| `mod_panel.c` | `mod/panel.rs` |
| `mod_input.c` | `mod/input.rs` |
| `mod_text.c` | `mod/text.rs` |
| `mod_script.c` | `mod/script.rs` |
| `mod_title.c` | `mod/title.rs` |
| `mod_message.c` | `mod/message.rs` |
| `mod_timer.c` | `mod/timer/{clock,random,splits}.rs` |
| `bw_limits.c` | `mod/api/limits.rs` |

`mod_rule.c` is the best example of why the split matters: it is a
limits/value/tables/compiled-form/lexer/parser/checker/evaluator/registry
stack in one file. Each of those is independently testable. The parser
and evaluator get fuzz targets; the checker gets table-driven tests.

### host

| C now | Rust modules |
|---|---|
| `main.c` | `host/{main,args,paths,module,run}.rs` |
| `wasi.c` | `host/wasi/{args,env,clock,random,fs,fd,path}.rs` |
| `sdl.c` | `host/sdl/{window,events,audio,gl}.rs` |
| `net.c` | `host/net.rs` |
| `fiber.c` | stays C (asm stack swap) |
| `par.c` | stays C until a Rust replacement is proved |
| `mem_switch.c` | stays C (fixed-budget wasm2c allocator) |
| `gl_imports.c` | generated C; stays C |

The host and the tools crate may use `std` freely. The host links the
wasm2c-generated C and exposes its imports. Keep the wasm2c ABI in a small
`host/module.rs`; the rest is ordinary Rust.

## The porting recipe

Use this for every module. It is the same sequence in every task below.

1. **Read and run.** Read the C file, its header, the test that covers it,
   the doc it cites, and the callers. Run the test.
2. **Split.** If the file is over 600 lines or has more than one
   responsibility, split it in C along the module map. Keep the symbols and
   behaviour. `ctest` green. Land the split as its own PR.
3. **Mirror.** Create the Rust module with the same public API and the
   same doc comment (provenance included). Keep the C file compiled.
4. **Implement.** Use `eidos-types`, `eidos-guest`, `eidos-io` and
   `eidos-sync`. No raw pointers outside `eidos-guest`; no `unsafe` outside
   the allowed crates.
5. **Bind.** Add the C symbols to `eidos-ffi`, with `#[repr(C)]` mirrors
   and layout assertions if a struct crosses. Add the headers' declarations
   to the symbol-parity check.
6. **Prove.** Port the test vectors, or add a differential test. For pure
   functions, fuzz both implementations on the same inputs and compare
   outputs. For stateful code, snapshot the relevant guest memory and the
   outputs before and after.
7. **Switch.** Add the module to `EIDOS_RUST_MODULES` and remove the C
   source from the target. Run the full C test suite. Keep the C path
   selectable (`EIDOS_RUST=OFF` or a per-module override).
8. **Measure.** Run the benchmark or frame-time harness. Record the number
   in `docs/rust.md`. If it regresses past the threshold, profile or keep
   C; do not ship the regression.
9. **Remove.** After one release with both paths, delete the C file and
   its header in a separate PR. Update `docs/rust.md`, the module lists,
   `CHANGELOG.md` and the symbol manifest.

## The first ten pull requests

Each is one PR, in this order, and each is small enough to review on its
own:

1. R0.1: `docs/rust.md`, `docs/building.md`, `README.md`, `AGENTS.md`.
2. R0.2: the link spike and `-DEIDOS_RUST=ON`; no ported module yet.
3. R0.3: CI, `rust_limits.py`, `rust_symbols.py`, `cargo-deny`.
4. R1.1: split `install/install.h` and `install/ndsrom.c`, C only.
5. R1.2: split `sdk/port.h` and the SDK files that are ported first, C only.
6. R1.3: split `g2d.h`/`g3d.h` and `hw3d.c`, C only.
7. R1.5: split `ui.h` and `ui.c`, C only.
8. R1.6: split `settings.c`, `settings_ui.c` and `platform.c`, C only.
9. R2.1: port SHA-1 and BLZ to Rust, with the differential test.
10. R2.2: port ROM identify and verdict to Rust.

The order after that is the task list. Do not start a port before its C
split is merged and its C tests pass.

## Tasks

Do them in order unless a task says otherwise. The order is chosen so each
task's dependencies are already Rust or already have a proved boundary.

### R0: decisions, docs and the link spike

Needs: nothing. This is the gate for the rest.

**R0.1 — the documents.** Add `docs/rust.md` with this plan's architecture,
crate map, unsafe policy, size rules, build switch and verification. Update
`docs/building.md` (Rust toolchain and `EIDOS_RUST`), `README.md`
(requirements), and `AGENTS.md` (the Rust conventions in "Rules for whoever
does a task"). Copy this plan's decisions and task list into `docs/rust.md';
`rust-port.md` stays the working tracker outside the repository.

Done when: the documents are merged and `docs/building.md` states the
minimum Rust version and the targets.

**R0.2 — the link spike.** Build `rust/eidos-ffi` as a staticlib with
`no_std` + `alloc` for `wasm32-unknown-unknown`, and for the native 32-bit
target the machine's toolchain can build (`i686-unknown-linux-gnu` or
`i686-pc-windows-gnu`; CI covers both). Link it into the module's CMake
build (`eidos_boot`) and the native 32-bit build. The stage-two host is a
separate program and is not linked against the module crates. Add
`cmake/rust.cmake` and `-DEIDOS_RUST=ON`.

The spike implements four probe functions:

- `bw_rust_probe(u32) -> u32`: called from C, calls C, returns.
- `bw_rust_alloc_probe`: allocates and frees `Vec`/`String`, proves the
  global allocator uses the module's C heap.
- `bw_rust_panic_probe`: proves a Rust panic reaches `bw_panic` and aborts.
- `bw_rust_math_probe`: proves C libm and Rust float calls agree, and that
  a Rust function pointer survives `--fpcast-emu` when called indirectly.

Also try `wasm32-wasip1` + `std` as a separate branch and record whether the
link is clean. Do not adopt it in this task; just record the result.

Done when: `-DEIDOS_RUST=ON` builds the wasm module and the native build,
the probe test passes, and `docs/rust.md` records the module size, the
link command, the allocator behaviour, and the `no_std` vs `wasip1` result.
If the link cannot be made to work, stop and report.

**R0.3 — CI and guardrails.** Add to `tools/check.py` the configurations
`rust-lp64`, `rust-ilp32` and `rust-wasm`; add a CI job that runs
`cargo fmt --check`, `cargo clippy`, `cargo test --workspace`, `cargo deny
check`, `tools/rust_limits.py`, and `tools/rust_symbols.py`. Add
`clippy.toml`, `rustfmt.toml`, `rust/deny.toml`, and commit
`rust/Cargo.lock`. CI builds with `--locked` and no network. Once
`eidos-host` exists, exclude it from the workspace test and test it with
its own CMake target; its pure modules get a `test` feature.

Done when: a PR with a 700-line Rust file, a new `unsafe` in a safe crate,
or a missing symbol fails CI; a normal PR is green on Linux, macOS and
Windows.

### R1: split the C into the target modules

Needs: R0.1. Do each area immediately before its port, not all at once.
Every task here is behaviour-preserving C only.

**R1.1 — install.** Split `install/install.h` into `sha1.h`, `blz.h`,
`rom.h`, `extract.h`, `fs.h`, and keep an umbrella `install.h` that
includes them so callers do not change. Split `ndsrom.c` into `rom/` as in
the module map; `fsutil.c` into `io/`; `setup.c` into `setup/`. Keep the
public names until the port.

**R1.2 — sdk in scope.** Split `port.h` into `paths.h`, `arm9.h`,
`archive.h`, `heap.h`, `diagnostics.h`, `machine.h`; update includes. Split
`mod_rule.c` into `rules/`; `mod_net.c` into `net/`; `mp.c` into `mp/`;
`mod_manifest.c`, `mod_state.c`, `mod_loader.c`, `fs.c`, `vfs.c`,
`save.c`, `input.c`, `firmware.c` and `langmap.c` along their existing
sections. Do the same for every other in-scope SDK file in the module map.
Do not touch the machine files.

**R1.3 — g2d and g3d.** Split `g2d.h` into per-container headers and
`resources.c` into the parsers. Split `g3d.h` into `mtx`, `texture`,
`nsbmd`, `gx`, `raster`, `light`, `hw3d`. Split `hw3d.c` into `hw/`, and
every other g2d/g3d file over 600 lines along its sections.

**R1.4 — world, audio, frame.** Split `bmap_data.c`, `bmap_place.c`,
`bcam_sim.c`, the audio containers, and `frame.c`/`prof.c` along the module
map.

**R1.5 — ui.** Split `ui.h` into canvas/view/mouse/frame/actions/recipes;
split `kit.c`; split each `recipe_*.c` along its sections. Do one recipe
per PR.

**R1.6 — platform.** Split `settings.c`, `settings_ui.c`, `platform.c`,
`wizard.c` and `mod_options_proc.c` along the module map. Split every other
in-scope platform file over 600 lines the same way. Keep the SDL calls in
the same C files until R10.

**R1.7 — host.** Split `main.c`, `wasi.c`, `sdl.c` and `net.c` along the
module map. Keep `fiber.c`, `par.c` and `mem_switch.c` untouched.

Done when (for the whole R1): every in-scope C file is at most 600 lines,
every hand-written in-scope header at most 400 lines, `ctest` is green, and
`tools/check.py` is green on the configurations the machine can run. The
generated headers (`sdk/mod_api.h`, `sdk/langmap_bytes.h`,
`engine/ui/ui_lang_table.h`, `engine/platform/icon_rgba.h`) and the
published mod ABI headers (`sdk/mod_*.h`) are excluded and handled in
Appendix C. No behaviour changed; the split PRs contain no Rust.

### R2: install

Needs: R0.2, R1.1.

**R2.1 — hash and BLZ.** Create `eidos-install` and the minimal
`eidos-types`/`eidos-io` it needs (R3 completes them). Port `sha1.c` and
`blz.c` with the same C symbols. Add Rust unit tests from the C vectors.
Fuzz both with the same inputs and compare outputs against the C
implementation.

**R2.2 — identify.** Port the header parse, CRC, size/trim logic, known
SHA-1 table, game code decode, `BwRomInfo` and `bw_rom_verdict`. Keep the
exact messages. Run `test_extract` and `test_dataroot` against the Rust
symbols.

**R2.3 — FAT, FNT and the plan.** Port the FAT/FNT walk, the sanitised
names and the region plan. Fuzz with malformed images. The plan is still
checked completely before any write.

**R2.4 — extract.** Port the two-pass writer, the staging directory, the
atomic rename, `arm9_dec.bin`, `overlays_dec/` and the manifest. Run the
synthetic-cartridge differential (`test_extract`) and the real-ROM test
where a ROM is present. Compare the output tree file for file.

**R2.5 — setup and the CLI.** Port `setup.c`, `location.c` and
`extract_main.c`. Keep `picker.c` and `picker_mac.m` C. The
`eidos_extract` CLI keeps its arguments, messages and exit codes.

Done when: `test_extract`, `test_dataroot`, `test_picker` (C picker path),
the synthetic ROM differential and the real-ROM extraction pass with the
Rust installer; the output tree is byte-identical; malformed images are
refused with the same messages; `cargo fuzz` runs on header/FAT/FNT/BLZ.

### R3: types, I/O, sync, guest and the FFI facade

Needs: R0.2.

**R3.1 — `eidos-types`.** Mirror `sdk/types.h`'s scalar vocabulary, endian
readers, fixed-point helpers and the `BOOL`/`fx32` types as newtypes and
free functions. The C header stays for the C sources; Rust gets its own
version. Add the `DsAddr`, `Frame`, `Tick`, `Fx32` newtypes. The types are
`#[repr(transparent)]` where they cross FFI; otherwise Rust types.

**R3.2 — `eidos-io`.** Define `Read`, `Seek`, `Write`, `File`, `Dir`,
`PathUtf8`, `rename`, `remove_tree`, `free_space`. Backends: `cfile`
(wraps `FILE*`, works on wasm and native) and `std` (tests and host). The
UTF-8/UTF-16 Windows conversion stays in the OS backend and is tested for
`C:\Users\...\Téléchargements`-style paths.

**R3.3 — `eidos-sync`.** Wrap `plat_thread`'s mutex, condition variable,
thread start and run token. Define `RunToken`. On wasm, the thread start
is the host fiber; do not introduce `std::thread`.

**R3.4 — `eidos-guest`.** Wrap `dsmem`'s C API: `bw_ds_ptr`,
`bw_ds_try_ptr`, `bw_ds_addr_of`, `bw_ds_wire_addr`, `bw_ds_register`,
`bw_ds_unregister`, the I/O read/write/hook functions, the VRAM functions
and the region constants. Do not port `dsmem.c`. Type the accessors; keep
the raw pointer only in `as_ptr` for the FFI. Add `GuestRef` and the
`acquire` rule. Run `test_dsmem` unchanged.

**R3.5 — `eidos-ffi` and layout tests.** Make `eidos-ffi` the only module
crate with `#[no_mangle]`. Add `tests/test_rust_layout.c` and a Rust
`const` assertion for every shared type, including `BwSha1`, `BwRomInfo`,
`BwArcFile`, the mod payloads and the settings records. Add
`tools/rust_symbols.py` to compare the staticlib's exported symbols with
the C headers' expectations.

Done when: the core crates build for wasm32 and i686; `test_dsmem` passes;
the layout test passes on wasm32, i686 Linux, i686 Windows and armv7; every
`unsafe` is in an allowed crate; `cargo miri test` passes for
`eidos-types` and the pure `eidos-install` parsers.

### R4: engine leaves

Needs: R3. These are the containers and decoders with no game state.

**R4.1 — g2d containers.** Port `lzss`, `ncgr`, `nclr`, `ncer`, `nanr`,
`nmcr`, `nscr`, `palette`, `rig` and the shared resource checks. Run
`test_g2d`, `test_bg`, `test_bg_affine` and the asset oracles. Fuzz the
container parsers.

**R4.2 — g3d containers.** Port `mtx`, `texture`, `nsbmd` and the dictionary
loading. Run `test_g3d`, `test_g3dhw` and the OBJ oracle. Fuzz NSBMD/NSBTX.

**R4.3 — audio containers.** Port `sdat`, `sbnk` and `swav`. Run
`test_audio` and `test_snd` against the Python oracle. Fuzz the containers.

**R4.4 — world data.** Port `bmap_data`'s zone/chunk/mesh construction,
`bmap_place`'s parsing and probes, `bmap_spots`, `bmap_gimmick` and
`bmap_npc`. Run `test_bmap`, `test_bmap_spots` and the spot editor's
fixtures.

Done when: the named tests and oracles pass with the Rust modules; the
frame path is unchanged (these are not hot yet); the fuzzers run clean for
a recorded corpus.

### R5: engine core

Needs: R4. These are the hot paths; measure each one.

**R5.1 — g2d composition.** Port `bg`, `bg_affine`, `window`, `obj`/
`compose`, `hwcompose` and `par`. Run the 2D oracle, `test_hwcompose`,
`test_hires` and `test_gpu3d` as applicable. Record both-screen compose
time before and after.

**R5.2 — g3d pipeline and rasteriser.** Port `gx`'s display-list interpreter
and `raster`'s clip/span/depth/blend/texture stages, then `hw3d`'s command
handling, status, textures from VRAM and swap. Run `test_g3d`,
`test_g3dhw`, `test_gpu3d` and the frame-time harness. The software
rasteriser is the riskiest performance port; keep `raster.c` C if the Rust
version cannot stay within 2%.

**R5.3 — audio engine.** Port `sseq`, `mixer`, `player` and the ARM7 sound
core together. Run `test_audio`, `test_snd`, `test_snd_matched` and the
oracle. The output must be byte-identical, not merely close.

**R5.4 — frame.** Port `frame.c` and `prof.c`; move the tick counter to
`eidos-time` (`eidos-types` is the fallback if the crate is too small to
justify). The C `sdk/os_tick.c` calls it through the small C ABI. Run
`test_frame` and the 32-frame/280095-tick assertions.

Done when: the oracles pass exactly; the frame-time harness shows no more
than 2% regression on the 2D compose, 3D raster and audio paths; the
module size is recorded.

### R6: OS and file logic

Needs: R3.

**R6.1 — files and archives.** Port `fs`, `vfs`, `vfs_narc`, `vfs_msg`,
`vfs_sdat`, `narc` and `inflate`. Run `test_fs`, `test_vfs`,
`test_vfs_members`, `test_vfs_msg`, `test_vfs_sdat`, `test_dataroot` and
`test_arcid`. Fuzz path resolution and NARC parsing.

**R6.2 — saves.** Port `save` and `save_slots` in `eidos-os`, and
`plat_save`/`plat_dir` in `eidos-io` (the host needs those two without the
rest of the module). Run `test_backup`, the save-slot tests and the
atomic-write tests. The whole-file write, sync and replace behaviour is the
contract; do not simplify it.

**R6.3 — firmware and language.** Port `firmware` and `langmap`; convert
`sdk/langmap_bytes.h` to a generated binary asset used by
`include_bytes!`. Run `test_firmware` and `test_armdata_real` where the
player's data is available.

**R6.4 — input.** Port `input`'s button, hinge, stylus and script state.
Run `test_input`, `test_tp`, `test_ui_input` and the replay tests.

**R6.5 — game-side helpers.** Port `battle_view`, `field_fade`,
`battle_fixed` and `net_link`. Run `test_net_link`, the mod event/state
tests that read battle and field state, and the boot smoke test that walks
the field and enters a battle.

Done when: the named tests pass; the path and archive fuzzers run clean;
the save files produced are byte-identical to the C build on the same
inputs.

### R7: the mod layer

Needs: R3, R6.

**R7.1 — rules.** Port `mod_rule` in its split modules: limits, values,
name tables, compiled form, lexer, parser, checker, evaluator, cells,
registry, file and errors. Run `test_mod_rule`, the language fixtures and
the step budget tests. Add a fuzzer for the parser/checker and a
differential for the evaluator.

**R7.2 — manifest and API tables.** Port `mod_manifest`, `mod_table`,
`mod_slot`, `mod_hooks`, `bw_limits`, the namespace rules and the reserved
device names. Run `test_mod_declare`, `test_mod_table`, `test_api_layout`
and `test_mod_manifest`.

**R7.3 — state and events.** Port `mod_state`, `mod_context`,
`mod_event`, the five event families, the payload marshalling and the
state accessors. Run `test_mod_state`, `test_mod_event`,
`test_mod_surface` and the payload tests. Every accessor that reads or
writes game memory goes through `eidos-guest`.

**R7.4 — saved data, export and packages.** Port `mod_savedata`,
`mod_export`, `mod_bw`, `mod_package` and the envelope/version rules. Run
`test_mod_savedata`, `test_mod_export`, `test_mod_bw`.

**R7.5 — loaders.** Port `mod_loader`'s table binding, override
installation, stop order and logging. The native `dlopen`/`LoadLibrary`
call is a small `unsafe` module; the wasm loader wraps WAMR through the
existing C API or a Rust binding, whichever proves smaller. Keep the C
symbols `eidos_mod_start`/`eidos_mod_stop` and the host-table layout exactly.
Run `test_mod_loader`, `test_mod_endtoend` and `test_mod_wasm`.

**R7.6 — the rest.** Port `mod_frame`, `mod_avatar`, `mod_restart`,
`mod_handoff`, `mod_menu`, `mod_panel`, `mod_input`, `mod_text`,
`mod_script`, `mod_title`, `mod_message` and `mod_timer`. Run
`test_mod_avatar`, `test_mod_restart`, `test_mod_handoff`,
`test_mod_menu`, `test_mod_panel`, `test_mod_input`, `test_mod_text`,
`test_mod_script`, `test_mod_messages` and `test_mod_timer`.

Done when: every `test_mod_*` passes; `test_api_layout` proves the ABI on
all targets; the rule and manifest fuzzers run clean; native and wasm mods
both load and unload in the end-to-end test.

### R8: networking and multiplayer

Needs: R7.

**R8.1 — peer sessions.** Port `mod_net`'s protocol, socket layer, TLS,
queue, session state, external control and frame tick. Keep the socket
calls in a small `sys` module; the protocol and state are safe Rust. Run
`test_mod_net`, the relay protocol tests and the loopback tests.

**R8.2 — multiplayer.** Port `mp`'s host, joiner, shared table, events,
positions, avatars and calls. Run `test_mp`, `test_net_link` and the
two-process loopback test.

Done when: the loopback session completes a host/join/start/pause/save
cycle; the wire bytes are identical to the C build for the same sequence;
TLS still verifies the pinned roots; no new blocking call is introduced on
the frame thread.

### R9: the interface

Needs: R5, R6, R7.

**R9.1 — foundation.** Port `ui.c` into canvas, view, mouse, frame, hit,
intent, game queue and scene detection. The C `eidos_ui_frame` entry point
stays and calls the registry.

**R9.2 — kit and assets.** Port `kit.c`, `assets*.c`, `ui_lang.c` and
`padglyph.c`. Convert `ui_lang_table.h` to a generated Rust/binary asset
from `tools/ui_lang.py`; the generator writes the C header as well until
the last C user is gone. Run `test_ui_kit`.

**R9.3 — recipes.** One PR per recipe, in this order: title, field, menus,
party, bag, battle, pc, dex, shop, settings, input, starter. Each PR moves
one `recipe_*.c` and its tests. Run `test_ui_input`, `test_ui_pc`,
`test_one_layout` and the recipe's own tests.

Done when: every recipe is Rust; the one-screen layout, touches, hit
regions, text input and screenshots match the C build; the UI frame time
is within 2%.

### R10: platform and settings

Needs: R9.

**R10.1 — settings model.** Port `settings.c` into model, file, apply,
bindings, network, session and panel. Run `test_settings`. The
`settings.ini` bytes and key names must not change.

**R10.2 — settings UI.** Port `settings_ui.c` into page, row, edit,
question and draw. Run `test_settings_ui`. The screen is compared against
the C build's screenshots.

**R10.3 — platform.** Port `platform.c`'s window, input, pacer, audio and
presenter state to Rust; declare the SDL3 calls `extern "C"` in
`platform/sys.rs` or keep them in a small C shim. Port `geometry.c`,
`font.c`, `text_instant.c`, `plat_replace.c` and `plat_embed.c`. Run
`test_platform_input`, `test_gpu3d`, `test_view` and the frame-time harness.

**R10.4 — wizard and mod panel.** Port `wizard.c`, `mod_options_proc.c` and
`mod_panel_draw.c`. `picker.c` and `picker_mac.m` stay C. Run
`test_wizard`, `test_mod_panel`.

Done when: the SDL/GL surface is unchanged; `plat_gl*.c` is still C and
still linked; settings, wizard and input tests pass; the frame time is
within 2%; the wasm module still imports exactly the SDL/GL symbols the
host answers.

### R11: the host

Needs: R1.7, R0.2.

**R11.1 — main and module.** Port `host/main.c`'s argument/env/path
handling and the wasm2c instance setup to `eidos-host` (std Rust). Update
`host/CMakeLists.txt` to link `eidos-install` and `eidos-io` in place of
the ported `install/location.c`, `sdk/plat_save.c` and `sdk/plat_dir.c`;
keep `install/picker.c` C. Keep `fiber.c`, `par.c` and `mem_switch.c` C.

**R11.2 — WASI.** Port `host/wasi.c` into args, env, clock, random, fs,
fd and path modules. Run `test_host_wasi` and `test_host_wasi.py`.

**R11.3 — SDL imports.** Port `host/sdl.c` into window, events, audio and
GL modules. The `env` import functions the wasm2c-generated C calls keep
their exact names and signatures with `#[no_mangle]` in `eidos-host`. Keep
`gl_imports.c` generated C. The module's imports must resolve exactly as
before. If the SDL ABI wrappers dominate the file, keep `sdl.c` C and port
only the pure parts; record the decision in `docs/rust.md`.

**R11.4 — net.** Port `host/net.c`. Run the host network tests.

Done when: `tools/build64.py` builds and the desktop game starts, plays a
frame, takes a screenshot and exits; the wasm2c ABI and the generated
imports are unchanged; the host's memory/fiber behaviour matches the C
host on the boot test.

### R12: removal, defaults and release

Needs: R2–R11.

- Delete the in-scope C files and headers that have a Rust replacement.
  Keep the machine files, `stubs/`, the GL bindings, the pickers, the
  fibers and the Switch allocator.
- Make `EIDOS_RUST=ON` the default; keep `EIDOS_RUST=OFF` for one release
  as a fallback.
- Update `docs/rust.md` with the final crate map, the unsafe budget, the
  measured numbers and the module list.
- Update `docs/building.md`, `README.md`, `CHANGELOG.md`, `AGENTS.md` and
  the release workflow.
- Add the Rust configurations to `tools/check.py` and CI.
- Record module size, frame time, extraction time, memory and audio
  exactness in "Results".

Done when: `ctest` and `tools/check.py` are green with Rust only; the
module and host build on Linux, macOS, Windows, Android and iOS; the
Switch build is attempted and its result recorded; the `unsafe` budget is
met; one release has shipped with the fallback and the next removes it.

## Verification

### Per module

- The existing C tests pass with the Rust symbol.
- New Rust unit tests cover the module's contract and errors.
- Pure functions have a differential test and a fuzz target.
- Stateful functions have a before/after snapshot of guest memory and
  output.
- Shared types have a layout test on wasm32, i686 Linux, i686 Windows and
  armv7.
- Hot paths have a benchmark or a frame-time trace.
- `cargo clippy` has no new warning; `unsafe` stayed in the allowed crates.

### Whole project

```sh
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace --exclude eidos-host --all-features
cargo test -p eidos-host --features test
cargo miri test -p eidos-types -p eidos-install
cargo build -p eidos-ffi --target wasm32-unknown-unknown --release
cargo build -p eidos-ffi --target i686-unknown-linux-gnu --release
cargo deny check
tools/rust_limits.py
tools/rust_symbols.py

cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../aletheia -DEIDOS_BOOT=ON -DEIDOS_RUST=ON
cmake --build build
ctest --test-dir build --output-on-failure

python3 tools/build64.py --build-dir build64 --aletheia-root ../aletheia --fast
python3 tools/check.py
```

Miri cannot execute the FFI boundaries; it is run on the pure crates
(`eidos-types`, the `install` parsers, the rule parser) only. The C tests
and the oracles cover the boundaries.

### Measurements to record

- Wasm module size before/after each large module.
- 2D compose, 3D raster and audio render time, before/after.
- UI frame time, before/after.
- ROM identify and extract time, before/after.
- Rust heap high-water mark, and the Switch budget including it.
- Unsafe line count per crate (from `cargo geiger` or a recorded grep).
- The largest file and function after each phase.

## Risks

1. **Rust and wasi-sdk do not link cleanly.** Duplicate libc,
   compiler-builtins, `__wasm_call_ctors`, or the staticlib's own
   allocator. Mitigation: R0.2 is a gate; try `no_std` and `wasip1` std;
   if neither links, stop and report before any port work.
2. **32-bit struct layout differs.** `u64`/`double` alignment, `char`
   signedness, bitfields, packed structs. Mitigation: the layout policy and
   `test_rust_layout.c` on every target; keep `u64`-bearing structs on the
   byte-buffer side of the FFI.
3. **Performance regression.** The rasteriser, compositor, mixer and UI
   are hot. Mitigation: measure every port; keep `raster.c`,
   `hwcompose.c`, `mixer.c` or `platform.c` C if Rust cannot stay within
   the threshold.
4. **Float determinism.** Audio must be byte-exact. Mitigation: call the
   same C libm; run the oracle on every target; do not "improve" the
   approximation.
5. **`no_std` friction.** No `HashMap`, `std::fs`, `std::thread`. Mitigation:
   `eidos-io`/`eidos-sync`; `hashbrown` with a fixed hasher or sorted
   arrays; keep the platform backends small.
6. **Allocator and memory budget.** Rust allocations share the module heap;
   the Switch fixed budget must include them. Mitigation: one global
   allocator over C malloc; S0/R0.2 measurements; no hidden arenas.
7. **Code size.** `core::fmt`, panic formatting and `alloc` can grow the
   module. Mitigation: `panic=abort`, minimal panic handler, `lto`, measure
   size per module; keep formatting out of hot paths.
8. **Switch target.** `aarch64-nintendo-switch-freestanding` is tier 3 and
   needs a custom build. Mitigation: attempt only after R10; keep the
   Switch on the C path if it does not work; do not block desktop.
9. **Build complexity.** Cargo + CMake + wasm2c + devkitPro + Android/iOS.
   Mitigation: one `cmake/rust.cmake`, `--locked`, `tools/check.py`
   configurations, and a single documented target matrix.
10. **Partial migration drift.** Two implementations diverge while both
    exist. Mitigation: the C path is only a fallback, not a second
    development target; every behaviour change lands in both or waits; the
    C file is deleted one release after the Rust one ships.
11. **Native mods and `dlopen`.** The loader is inherently unsafe.
    Mitigation: keep it a small FFI module, keep the wasm mod path the
    default, and test load/unload/abort.
12. **Supply chain.** New crates add licence and security surface.
    Mitigation: `cargo-deny`, `Cargo.lock`, no network at build time,
    vendoring for CI.
13. **Time.** 118k lines is a multi-release effort. Mitigation: the module
    split lands independently; every phase is shippable; the plan can stop
    after any phase with the tree better than it started.

## Definition of done

The project is done when:

- `EIDOS_RUST=ON` is the default and builds the module and host on Linux,
  macOS, Windows, Android and iOS.
- Every in-scope C file is gone; only the machine layer, `stubs/`, the GL
  bindings, the pickers, the fibers, the Switch allocator and the remaining
  out-of-scope files in Appendix A remain C.
- `ctest`, `tools/check.py`, `tools/build64.py`, the pixel/audio oracles
  and the extraction differential are green.
- The frame path is within 2% of the C build; extraction within 10%;
  audio is byte-identical.
- The module's size and the Switch memory budget are recorded and within
  the release budget.
- `unsafe` exists only in the allowed crates, and the budget is recorded in
  `docs/rust.md`.
- The documents, `CHANGELOG.md` and `AGENTS.md` describe the Rust build and
  conventions.
- One release has shipped with `EIDOS_RUST=OFF` still working, and the
  next removes the fallback.

## Results

Filled in as tasks finish. Each entry: date, eidos commit, task, numbers or
outcome.

| Date | Commit | Task | Result |
| --- | --- | --- | --- |
| | | R0.1 docs | |
| | | R0.2 link spike | |
| | | R0.3 CI | |
| | | R1 split | |
| | | R2 install | |
| | | R3 core | |
| | | R4 engine leaves | |
| | | R5 engine core | |
| | | R6 OS/FS | |
| | | R7 mod | |
| | | R8 net/mp | |
| | | R9 UI | |
| | | R10 platform | |
| | | R11 host | |
| | | R12 removal | |

## Appendix A: in scope and out of scope

### In scope

- `install/`: `sha1.c`, `blz.c`, `ndsrom.c`, `fsutil.c`, `setup.c`,
  `location.c`, `extract_main.c`.
- `engine/g2d/*`, `engine/g3d/*`, `engine/world/*`, `engine/audio/*`,
  `engine/frame/*`, `engine/ui/*`.
- `engine/platform/`: `settings.c`, `settings_ui.c`,
  `mod_options_proc.c`, `mod_panel_draw.c`, `wizard.c`, `geometry.c`,
  `font.c`, `text_instant.c`, `plat_replace.c`, `plat_embed.c`, and the
  state machine in `platform.c`.
- `sdk/`: `fs.c`, `vfs.c`, `vfs_narc.c`, `vfs_msg.c`, `vfs_sdat.c`,
  `narc.c`, `inflate.c`, `save.c`, `plat_save.c`, `plat_dir.c`,
  `save_slots.c`, `firmware.c`, `langmap.c`, `input.c`, `battle_view.c`,
  `field_fade.c`, `battle_fixed.c`, `net_link.c`, all `mod_*.c`, `mp.c`,
  `tls_roots.c`, `bw_limits.c`, and the ARM7 sound core
  (`sdk/arm7/snd_driver.c`, `snd_seq.c`, `snd_hw.c`).
- `host/`: `main.c`, `wasi.c`, `sdl.c`, `net.c`.

### Out of scope

- aletheia `src/matched/`, `src/game/`, and every `stubs/` file.
- `sdk/dsmem.c`, `host_vm.c`, `os_thread.c`, `os_irq.c`, `os_interrupt.c`,
  `os_alarm.c`, `os_tick.c`, `os_system.c`, `os_argument.c`, `os_alloc.c`,
  `os_arena.c`, `cp.c`, `mi.c`, `mi_dma.c`, `mi_gx.c`, `gx.c`, `g3_fifo.c`,
  `pxi.c`, `overlay.c`, `overlay_image.c`, `armdata.c`, `plat_thread.c`,
  `plat_crash.c`, `wasm_heap.c`, `panic.c`, `game_globals.c`, `aliases.c`,
  `port_reset.c`, `tcbl.c`, `wasm_layout.h`.
- `sdk/arm7/` except the sound core: `arm7.c`, `card.c`, `ctrdg.c`, `fs.c`,
  `mic.c`, `nvram.c`, `pad.c`, `pm.c`, `rtc.c`, `snd.c`, `tp.c`.
- `engine/platform/plat_gl.c`, `plat_gl2d.c`, `plat_gl3d.c`,
  `plat_gl_view.c`, `plat_gl_int.h`.
- `install/picker.c`, `install/picker_mac.m`.
- `host/fiber.c`, `host/par.c`, `host/mem_switch.c`,
  `host/switch_include/`, `host/gl_imports.c` (generated).
- `demo/`, `tools/spot-editor/`, `android/`, `ios/`, `switch/`, `cmake/`,
  and the Python tools.

## Appendix B: the FFI checklist

For every new symbol in `eidos-ffi`:

- [ ] The C header still declares it, with the same types.
- [ ] The Rust signature uses explicit `u8`/`i8`/`u16`/`i32`/`u32`/`i64`
      (`c_ulong` only where the C uses `unsigned long`).
- [ ] Every `#[repr(C)]` type has a size and offset assertion.
- [ ] `test_rust_layout.c` prints and checks the C side on wasm32, i686
      Linux, i686 Windows and armv7.
- [ ] The symbol appears in `tools/rust_symbols.py`'s manifest.
- [ ] No Rust function is called through a mismatched C function pointer.
- [ ] No weak symbol, alias, variadic function or `setjmp` is implemented in
      Rust.
- [ ] The function is thin: arguments in, safe call, result out.
- [ ] Errors map to the C convention (`-1`, `NULL`, `err` buffer), not to a
      Rust panic.
- [ ] `cargo geiger`/the unsafe budget shows no new `unsafe` outside the
      allowed crate.

## Appendix C: generated data

These headers are generated; do not translate them by hand into Rust. Extend
the generator to write a binary or a Rust module and use `include_bytes!` or
the generated type:

| Generated file | Generator |
|---|---|
| `sdk/langmap_bytes.h` | `tools/langmap_bytes.py` |
| `engine/ui/ui_lang_table.h` | `tools/ui_lang.py` |
| `engine/platform/icon_rgba.h` | `tools/mkicon.py` |
| `engine/world/battle_spots_tsv.inc` | `tools/battle_spots.py` |
| `sdk/mod_wasm_natives.inc` | the mod API generator |
| `sdk/api/v5/*` headers and import table | the API generator |

The generator remains the source of truth. A generated artifact is checked
in, and CI fails when it differs from the generator's output. During the
migration a generator may write both the C and Rust forms; remove the C
artifact only when no C user remains. This is the same rule the mod API
already follows.

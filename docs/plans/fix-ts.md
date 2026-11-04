# fix-ts.md — Switch handheld: correct world, then 2x @ 720p @ 60 fps

Goal: the game renders the correct world at 2x internal resolution, 720p
output, handheld, locked at 60 fps. Nothing in this plan changes Mesa or
devkitPro packages. Eidos-side engine/host work only, one slice at a time,
each slice measured on hardware before the next begins.

## 0. Ground truth (all verified, not guessed)

- The NRO in `out/eidos.nro` links mesa-switch `16732f6` with no duplicate
  EGL definitions and no `drm_nouveau` symbols (`nm`: `drm_nouveau` = 0;
  the 2 remaining `drm_` hits are mesa's own in-tree `nouveau_drm_del/new`).
  EGL/GL/GLESv2/glapi all resolve to the prefix.
- On retail title mode, EGL initializes: `OpenGL NV12B (OpenGL ES 3.2 Mesa
  26.2.4)`, integer samples 8. The 05 `Could not initialize EGL` was
  transient (stale GPU state after the 04 crash), not a driver defect.
- The black world is the **edge pass**. Outlines off renders the world
  (logs/09); `unsupported` stays `0x0` throughout; MSAA is off by default
  on Switch (`msaa_wanted()` returns 0, `engine/platform/plat_gl3d.c:1534`)
  and the supersample fallback requires it too — the MSAA path is not
  implicated. Fog renders correctly with outlines off. Rear-bitmap/shadow
  cannot be the cause: `0x0` proves their bits are absent, and even forced
  they skip or flat-clear, never whole-frame black.
- `--profile` cannot crash the game. It touches ~280 bytes of BSS and
  prints one line per N frames to **stdout**, which on-device is discarded
  unless `nxlink -s` captures it. A missing profile table in `stderr.txt`
  means the capture was wrong, not that profiling failed. `args.txt` takes
  exactly one argv entry per line: `--profile` and `300` on separate lines.
- On the current tree a field frame is **1 scene draw + 1 synchronous
  blocking readback** (`glReadPixels` 192 KiB + `malloc` + full-frame
  `unpremultiply`, PBO ring unused on this path). The `3f84f9f` single-draw
  fix is verified in effect.
- Telemetry forensics across logs/06/08/09: every fps collapse begins as a
  **CPU flush storm** (logical submits 2.1 → 6-20 per native kickoff) while
  the GPU keeps retiring (+129/window) and total throttle-block time stays
  0-3 ms for entire runs. The collapse is CPU-bound; the GPU is draining.
  Queue-at-cap with flat retire happened once (old Mesa, logs/06 only).
- Per-frame CPU budget on the GPU path (all code-cited): vertex
  expansion + `glBufferData` re-spec (~1.25 MB @2000 quads), double CPU
  clear (~0.9 MB writes, 2 full scans), per-texture `malloc` + copy + FNV
  hash + `free` **even on cache hit**, per-frame `malloc` x2 + `qsort` in
  `g3d_geom_sort` (runs before the GPU-hit skip), ~400 KiB snapshot
  memcpy, sync readback ~0.6 MB moved + `malloc`/`free`. Every malloc takes
  the shared module heap lock across game thread + 2 workers.

## 1. Measurement protocol (applies to every run below)

- Capture `stdout` over `nxlink -s` whenever `--profile` is used; `stderr`
  alone is not evidence of anything profile-related.
- Standard env: `EIDOS_NXLINK=1 EIDOS_FPS_LOG=1 EIDOS_RENDERER_REPORT=1
  EIDOS_GPU3D_REPORT=1 EIDOS_RENDERER=gl` + `EIDOS_EDGE=0` until Phase 2 is
  done (the in-game outlines setting does not persist —
  `cannot replace settings.ini` — so do not rely on it; see L1).
- Attribution run: `args.txt` = `--profile` / `300` on two lines, env as
  above + `EIDOS_NOWAIT=1`, 60 s in the same field spot. Send back the
  full profile bucket line + fps series. Buckets: `step g3d main2d sub2d
  present audio wait geom raster snap texdec gldraw glup render boot3d
  glread unprem`.
- Store each run under `logs/<NN>/` with `boot.txt`, `stderr.txt`,
  title + town screenshots, and the exact `env.txt`/`args.txt` used.
- `0x0` unsupported mask + title-mode boot are entry conditions. If a run
  fails them, it is not a data point.

## 2. Phase 1 — the edge pass writes black on NVC0 (correctness gate)

No other phase starts until the world renders with outlines ON.

### 1a. Variant bisection (no rebuild)

- Do: run the field with `EIDOS_EDGE=1` through `7` (variants documented
  at `engine/platform/plat_gl3d.c:174-189`), one run each, same spot.
- Deliver: per-variant result (black/correct/partial).
- Done when: the variants split (some render) or all 7 are black.
- Reading: split ⇒ the bug is inside a variant shader path
  (`k_fse`); all black ⇒ the bug is in pass setup (FBO/`post_fbo`,
  ID-texture bind, VAO, `u_dep`/`u_id` samplers, `glClearBufferuiv` of
  `R8UI`).

### 1b. Root cause

- Do: from the 1a split, reduce to the failing construct. Prime suspects
  in order: (1) `usampler2D` + `texelFetch` of the `R8UI` ID target on
  GM20B; (2) the `gl_VertexID` fullscreen triangle in `k_vse` under ES
  3.2-translated shaders; (3) `DrawBuffers`/reattach order around the
  `color`/`color2` swap in `edge_pass` (`plat_gl3d.c:1408-1464`).
  Confirm with a minimal change only (e.g. output a constant color from
  `k_fse`, then reintroduce sampling piece by piece). One variable at a
  time; each needs at most a host-only rebuild.
- No-go: no rewriting the pass blind, no disabling edge globally to hit a
  number.
- Done when: the exact construct is named and a minimal edit makes
  outlines render correctly on hardware.

### 1c. Fix + regression test

- Do: fix the construct; extend `tests/test_gpu3d.c` edge coverage
  (`test_edge_marking`, `plat_gl3d.c:436+`) with a case that fails without
  the fix (desktop GL keeps passing). Keep `EIDOS_EDGE=n` variants working.
- Done when: desktop tests pass and hardware renders the field correctly
  with outlines ON (in-game setting), at any fps.

## 3. Phase 2 — kill the synchronous field readback

- Do first: verify whether the field capture actually consumes the 1x
  frame. `cap3d` is computed at `demo/eidos_boot.c:1406`; the read runs
  with `async=0` at `:1415-1416` because `main3d && graphics`. If the
  readback result is unused when capture is off/unchanged, skip it
  (biggest single win available). Otherwise route the field path through
  the existing PBO ring (`read_fb_start`/`read_fb_finish`,
  `plat_gl3d.c:2169-2214`, ring of 3) instead of `malloc` +
  `glReadPixels` + `unpremultiply` (`:2143-2161`).
- No-go: no change to capture semantics or the 1-frame-latency contract
  of the async path; no reading uninitialized PBOs on frame 0.
- Deliver: one code path, `glread`+`unprem` buckets near zero on the
  attribution run, identical pixels (VRAM capture + display both correct).
- Done when: field fps rises with no visual diff vs the sync path.

## 4. Phase 3 — per-frame CPU waste on the GPU path

Do in order; each is its own commit with a before/after attribution run.
All sizes are at 1x; every item scales with scene complexity today:

1. **Texture hits must be zero-copy.** `hw3d.c:626-708` mallocs texel +
   palette and copies + hashes full buffers even on store hit. Reuse
   scratch buffers; skip copy+hash when the key inputs are unchanged.
2. **`g3d_geom_sort` must not run when the GPU draws.** It mallocs x2,
   scans, and `qsort`s per frame (`raster.c:780-829`) before the GPU-hit
   skip. Gate it on the CPU path.
3. **CPU clear planes must not run when the GPU draws.** `clear_planes`
   + `g3d_target_clear` write ~0.9 MB over 2 full scans per frame;
   keep them for the CPU fallback only.
4. **Snapshot copies.** `hw3d.c:1025-1036` memcpy per frame; pool and
   version the snapshot buffers instead of regrowing.
5. **Vertex streaming.** Orphan (`glBufferData(NULL)` + `glBufferSubData`)
   or `INVALIDATE_RANGE` ring instead of per-frame re-spec
   (`plat_gl3d.c:1924-1927`); move `glGetError` at `:2004,2032` behind
   `EIDOS_GL_CHECK` (debug default on, release off).

- Done when: `geom`+`raster`+`snap`+`texdec` buckets are flat across
  menu/field up to polygon count, per-frame mallocs are gone from the
  field path, and the attribution fps is up with zero visual diff.

## 5. Phase 4 — upload path and threading (only if Phase 3 leaves a gap)

- `glTexImage2D` re-spec on texture change → `glTexSubImage2D` into pooled
  slots keyed by (w,h,format) with LRU cap.
- Heap-lock contention: only if `step`/`geom` still show cross-thread
  blocking after Phase 3. Measure first; do not shard preemptively.
- `unpremultiply` stays on CPU only if its bucket is noise; otherwise GPU
  shader pass where the consumer accepts it.

## 6. Phase 5 — 2x @ 720p @ 60 fps handheld (ship gate)

- Flip handheld default to `--resolution 2` only after P1 holds at 1x.
- Acceptance runs: P1 (1x) and P2 (2x) field + battle, avg ≥ 59.8 fps,
  p99 frame ≤ 16.7 ms, plus a 30-minute 2x field soak with no decay and
  no audio underruns. 5-minute mid-battle and HOME-resume checks.
- Done when: P1 and P2 pass on back-to-back runs. Otherwise loop back to
  the phase owning the biggest remaining bucket.

## 7. Lurking issues (tracked, not in the critical path)

- **L1. Settings don't persist** (`cannot replace settings.ini`). Until
  fixed, every hardware run must set `EIDOS_EDGE=0` in env — never trust
  the in-game toggle across reboots. Fix the save path; verify a reboot
  keeps outlines off.
- **L2. `tests/test_mod_loader.c` doesn't compile** on the pinned
  wasi-sdk 25.0 (witx `static_assert` alignment failures). Pre-existing,
  unrelated to perf work, but it red-dens the full stage-one build and
  blocks `ctest`. Fix the toolchain pin or the test; do not let it ride.
- **L3. `switch/diag-fog-toggle`** (one-line `EIDOS_FOG=0`) exists as a
  built-NRO diagnostic. Fog is exonerated; keep the branch for future
  triage or delete it — do not merge it.
- **L4. NRO hygiene.** Only `out/eidos.nro` (16732f6 + perf @ e229250) is
  current. Older `nro-out/eidos-*.nro` files are A/B controls only.
- **L5. Profile capture.** Any future `--profile` run is void unless
  `stdout` is captured (nxlink). Document this in `switch/README.md` next
  to the env table.
- **L6. Guard pages.** The `virtmem` experiment and `--guard-pages` stay
  as-is until the frame budget says otherwise; bounds checks were not the
  measured bottleneck.

## Rules for all work

- Small commits, `area:` prefix, terezi authorship, no agent credits.
- Desktop build + `tests/test_gpu3d.c` green before and after every C
  change; never land on a red `ctest`.
- One hardware variable per run. No claiming a win without a same-scene
  before/after pair.
- No Mesa, toolchain, or devkitPro package changes. No `glFinish`, no
  per-frame `glGetError` in release, no `--allow-multiple-definition`,
  no new synchronous readbacks.

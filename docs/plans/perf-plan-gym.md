# perf-plan-gym.md — Mistralton gym field to full speed

Base: `a7acd55` (`switch/fix-final`, worktree `src/wt/final`).
Target: gym, walking, handheld 1x, EDGE=0 then on. Game steps stay tied to
pictures, so wall must go under 16.7ms. No Mesa. No aletheia. One test
build at the end. No hardware runs inside streams.

Guide: `switch-mesa-guide.md`. Cite NX IDs in commits.

## Global rules (every stream)

- 5 worktrees from base: `src/wt/g1menu g2draw g3thr g4audio g5metrics`,
  branches `switch/fix-g1` … `switch/fix-g5`.
- Exclusive files per stream below. A touch outside fails review.
- Shared engine keeps desktop identical. Switch-only behind `__SWITCH__`
  or portable try-first-fallback. Small diffs. No reformat, no renames.
- Commits `fix:` prefix, author `terezi <jules@foxtails.me>`, no agent
  lines. `git config user.name terezi` + `user.email jules@foxtails.me`
  in each worktree. Verify with `git log -1 --format='%an <%ae> %cn <%ce> %s'`.
- Streams write code only. No cmake, no ninja, no NRO. `py_compile` only
  if a python file is touched (none expected).
- No `glFinish`, no per-frame `glGetError`/status in release, no new
  sync readback, no wasm ABI / lp64 / layout change, no feature disable,
  no push/PR/hardware run. Never land on red desktop.

## S1 — menus (owns CPU canvas cost)

Files: `engine/ui/ui.c` canvas/scale/gate hunks only
(`eidos_ui_scale`, `eidos_ui_frame` w/h + `ui_begin` call site,
redraw gate + `hold_canvas` path), `engine/ui/kit.c` (`ui_begin` +
draw entry only), `engine/platform/plat_gl.c`
`eidos_gl_one_canvas` upload block only.

Do:
1. Keep `eidos_ui_scale` math but render canvas at DS size, upscale in
   presenter (sharp-bilinear already there). No quality call here, size
   only, comment why.
2. Move static check before `ui_begin` clear. Static frame reuses last
   picture via `hold_canvas`, never memsets + repaints + rehashes.
   Force redraw on: input this frame, scene change, animation tick,
   gap end, first frame after show.
3. Upload: `TexImage` on size change only, `SubImage` dirty rect only,
   skip identical bytes (hash compare before bind). Reset version on
   context loss.
Ref: NXGL-9, NXGL-12, NXGL-22.

Deliver: diff + note (canvas bytes before/after, redraws skipped on
static menu, animated frame still full).
No-go: no matcher/threshold change, no recipe logic change, no
swap/present change, no canvas-size quality change without comment.
Done when: static menu paints ~0 bytes, live menu paints once.

## S2 — draw (owns copies + texture growth + state)

Files: `engine/g3d/hw3d.c` snapshot-pool hunks only
(`snap_grow`, `g_snap_pool`/`g_snap_seen`, vert/idx/poly memcpy block),
`engine/platform/plat_gl3d.c` texture-slot + upload hunks only
(`gtex`/`gtex_key`/`gtex_w`/`gtex_h` growth, upload 1794-1816,
per-run bind/draw), `engine/platform/plat_gl_int.h` pool fields only.

Do:
1. Reuse snapshot buffers grown-once, zero per-frame malloc/free.
   Remove `free` in per-frame loop (`:1348-1352` shape). Version slots
   by VRAM map gen, not per-poly content hash.
2. Cap `gtex` with LRU. Shrink to steady need on snapshot-serial
   change, delete above high-water, recreate lazy. Worst case is one
   re-upload, never a missing texture.
3. Allocate once, `SubImage` steady, `TexImage` size-change only.
   Never re-spec an in-use texture mid-frame (NXGL-9, NXGL-10).
4. Track bound program/texture/blend/depth on CPU, skip redundant
   calls (NXGL-18). Keep run-batching (NXGL-16), orphan-once +
   `SubData` (NXGL-17).
Ref: NXMEM-5, NXCPU-6, NXGL-9/10/12/16/17/18.

Deliver: diff + table (pool high-water before/after 10-min gym walk,
per-frame allocs 0, respec count 0 steady).
No-go: no exec/sort/key/order change, no raster-pixel change, no
decode change, no new sync readback.
Done when: high-water flat after warmup, zero >50ms `glup` outside
first transition window.

## S3 — threads (owns pin, nothing else)

Files: `host/par.c` only.

Do:
1. Keep pool at 2 on Switch (already capped). Assert `n<=2`, fail
   closed if env overrides higher.
2. Pin workers to cores 1,2 via `svcSetThreadCoreMask` in `worker_main`.
   Game stays on 0 (`host/main.c:582` owns that, do not edit).
   Comment core map. No spin-waits (cond vars stay).
Ref: NXTHR-1, NXTHR-4, NXTHR-3.

Deliver: diff + startup log line (workers 2, cores 1,2).
No-go: no pool resize, no game-thread change, no audio change.
Done when: log proves 2 workers pinned, no pile-on.

## S4 — audio (owns rate only)

Files: `engine/platform/platform.c` audio open + `queue_audio`
resample hunks only (no pacer/present/pump hunks).

Do:
1. Open at 48000 (Switch native, NXAUD-1), resample DS 32768 mix once
   in `queue_audio`. Remove 32768-then-44100 double convert.
2. Buffer 2-3 frames (NXAUD-2). Mixer never waits on game lock
   (NXAUD-3). Keep latency bounds, keep linear fallback only.
Ref: NXAUD-1/2/3, NXTHR-5.

Deliver: diff + open log (stream 48000 device 48000 no convert).
No-go: no pacer/swap/timebase change (`59.8261Hz` stays), no
callback-shape change.
Done when: log shows matching rates, zero convert on all paths.

## S5 — metrics (owns measurement only)

Files: `engine/frame/prof.*`, `demo/eidos_boot.c` probe/legend hunks
only (no render logic), `tools-local/session.sh` (new),
`logs/next/checklist.txt`.

Do:
1. Freeze golden buckets + one-line legend (RENDER contains G3D/UI,
   BOOT3D contains gldraw/glup, PRESENT is compose+render+swap, never
   summed). Keep: wall step g3d main2d present audio geom g2dread
   read3d + preup predraw preswap prefin unacc paint_n
   upbytes_canvas/screen upcalls want aqueue upskip swapgap pbos
   skip_n gxcmds verts polys pboq ngtex texup texrespec read_async.
2. Add with census pattern only (no per-frame malloc/hash/scan):
   ui_redraw vs ui_skip, pool high-water, free mem, scene name,
   fps + presents-skipped. Autopsy line on wall>500ms: phase + top-3
   buckets, one line per window max.
3. `session.sh NN`: prep from `logs/NN` env/args, `nxlink -s` stdout
   mandatory, wall>=120s gym walk (moving allowed, parked not
   required), start/end shots, SoC temp start/end, want=3 diagnostic
   first (present must fall, g2dread stays 0, else STOP), outlines-on
   repeat, clean NXEXIT=0 + bytes + prof-count + shots or refuse close.
4. Validator: steady drift check, scene-attributed windows (no
   loader/menu/black shots), binary sha + `-O2` proof + NRO size/sha
   + Mesa strings + `drm_nouveau` 0. Refuse citation on FAIL.

Deliver: script + checklist + legend + validator PASS/FAIL on old
run (showing non-comparable void where scene mismatched).
No-go: no render/behavior change, no bucket removal, no
values-asserted flaky test.
Done when: a bad run cannot be cited, a gym walk produces
scene-tagged windows even while moving.

## Merge + one test build (implementer only, after all 5 return)

1. Order: G5 metrics-legend first (probes only), G3 pin, G4 audio,
   G2 draw, G1 menu last (upload consumer). Disjoint files by design,
   verify `git diff --name-only` has no overlap before each merge.
   Resolve only documented seams.
2. Desktop: `cmake -S . -B build -G Ninja` + `cmake --build` +
   `ctest --output-on-failure` + `tests/test_gpu3d.c` GL 3.3. Green or
   stop. Known fails (`test_audio`, `test_mod_wasm`) must match base
   exactly.
3. Rebuild FIXED module (full `tools/build64.py:34` flags, `grep -q
   -O0 && exit 1`, wasm cap 18MB) then NRO native with cached mesa
   `16732f6` `--parts 96`. Record wasm+NRO bytes+sha, `strings` Mesa
   x2, `drm_nouveau` 0, `readelf -S` iff NRO delta >1MB.
4. Write `logs/next/` env (`gl`, reports, EDGE=0 then on) / args
   (`--profile` + `300` two lines) / checklist (gym walk 120s+,
   want=3 first, outlines-on, temp, shots, stdout). Stop, ask human.
   Claim nothing here.

## Risks

- S1 gate freezing live blink/fade: force list is the safety case,
  when in doubt draw.
- S2 shrink deleting a needed texture: shrink to proven need only,
  recreate lazy costs one upload.
- Gym moves so static skip rarely hits: S1 still wins menus, S2 wins
  every frame. Do not judge S1 on gym wall alone, judge on upskip +
  ui_redraw counters.
- If S5 validator stays red, stop. No fps claim without PASS.

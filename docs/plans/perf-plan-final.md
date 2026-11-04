# perf-plan-final.md — one shot, fixed build, steady field to 60fps

Base: run 30 fixed the build (`-O2` full flags, wasm ~14.5MB, NRO ~52MB,
0 `-O0`, `readelf` clean). Runs 40-44 are the new evidence. Title windows
now 9-10ms (100fps), field windows 16-27ms (37-60fps) with periodic
500-3880ms hitches. g2dread holds 0, present holds 1-3ms steady, audio is
44100 native with no convert. Do not compare anything to runs 26/28/29
(`-O0` vs `-O2` voids them). This plan finishes the job in one merge.

## What logs 30-44 tell us (reread 2026-11-04)

Useful and keep:

- Run 30 (`notes.txt:3-6`): wall FLAT 44.93/50.73/44.59 after `-O2` — the
  46→217 growth was the `-O0` module, not game code. present 36/15/21,
  main2d 1-10, g2dread 0, ui 6-12. Navigated, not parked — no claim, but the
  slope is gone. This validates Phase 0 permanently.
- Run 42 (best steady): w300 9.90ms title, w600 18.07, w900 18.41, w1200
  16.47 field. present 1.2-1.4, g2dread 0, main2d 8-10 (biggest steady),
  ui 3-5, game 1.6-2.7, geom 0. Steady field at ~60fps except main2d.
- Run 43 (best buckets): title 10.63ms, then 41.78 / 21.91 / 26.12 field
  with NEW columns — `preup/predraw/preswap/prefin`, `unacc`, `paint_n`,
  `upbytes_canvas/screen`, `upcalls`, `want`, `aqueue`, `upskip`,
  `swapgap`, `pbos`, `skip_n`, `gxcmds/verts/polys`, `pboq`, `ngtex`,
  `texup/texupn`, `read_async`. This is the format every future run keeps.
  Steady present 2-3ms, hitches isolated: w1200 `glup 4.19/498`,
  `boot3d 5.89/500`, `render 12/505`, `preswap 1.71/142`,
  `prefin 0.56/133`. `texup 0.5-1.3MB`, `ngtex 15-45`, `read_async 0-88`.
- Runs 40/41/44: same shape — steady 40-60ms title-transition, field
  24-44ms, plus 1-1.4s maxima in `glup/boot3d/present` (`40/w900
  glup 4.8/1441 boot3d 6.75/1454 present 110/1185`; `41/w1200
  glup 53/3878 boot3d 58/3880 render 64/3886`). Hitches, not slope.

Not useful, fix in this plan:

- No bench-spot hold anywhere after 26. 30 navigated; 40-44 navigated
  transition/field mix (`game max 452-465ms` load hitches in w600s).
  Shots are loader/menu only (`watch0.jpg` NetLoader, `watch_end.jpg`
  Sphaira) — no `field_start/field_end` framing, no HUD check, no scene
  hash. Every plan's "park 120s+" box stays unchecked.
- want=3 diagnostic never run (`want=0` all windows in 43). The PBO proof
  (`perf-plan-round2.md:137-139`) is still open.
- Outlines-on pair never run (`EDGE=0` everywhere). Edge correctness
  unproven on hardware despite code fix.
- SoC temp only in run 30 (`28C`). Growth-vs-heat undecidable elsewhere.
- `swapgap` (13-32M), `prefin/preswap` maxima, `pboq=3`, `skip_n 0-226`,
  `aqueue 2400-2670` have no legend — counters without meaning.
- `nxlink-stderr.txt` 31B `recv reset` both runs is capture noise, not data.

Log fix owned by Stream G below: keep run-43 bucket set as the golden
format, add legend + scene proof + park enforcement + want=3 + outlines-on
+ temp, reject loader/menu/black shots, refuse analysis without clean exit.

## Global rules

- Base is the fixed `-O2` tree (run-30 flags). Any branch not carrying full
  `MODULE_FLAGS` (`tools/build64.py:34`) fails review before code read.
- Exclusive files per stream (list below). A touch outside fails review.
- Shared engine keeps desktop identical; Switch-only behind `__SWITCH__`
  or portable try-first-fallback. No Mesa, no aletheia, no ABI/lp64/layout
  change. No feature disable. No `glFinish`, no per-frame `glGetError` /
  status in release, no new sync readback. No push/PR/hardware inside
  streams. Commits `terezi <jules@foxtails.me>`, `fix:` prefix.
- No spend-work-to-save-work: no new hash/scan/summary/cache whose check
  rivals the saving. Generation/dirty-bit/pointer compare only.
- Desktop `cmake --build + ctest + test_gpu3d.c` green; pre-existing fails
  (`test_audio`, `test_mod_wasm` at base) must match exactly.
- Rule IDs from `switch-mesa-guide.md` (`NX` prefix) are cited in commit
  bodies and reviews wherever a guide rule is applied or deliberately
  left out; verified-already items (worker pools pinned per NXTHR-1,
  applet refusal per NXSYS-1, memory/GL logging per NXSYS-2) need no
  stream and no change.

## Stream A — build lock (implementer-seeded, subagent hardens)

Files: `tools/build64.py`, `switch/build.py`, `.github/workflows/*`
(read-only elsewhere).
Do: (1) single-source flags — workflows extract from `build64.py:34`,
never hand copies; stage-two appends aliasing/wrapv flags (NXCPU-1);
(2) assert in build: `grep` every MODULE_FLAGS token in `build.ninja`/
`CMakeCache`, `grep -q -O0 && exit 1`, wasm cap 18MB / NRO cap 40MB with
bytes+sha print (round-3 deliverable format); (3) `readelf -S` iff NRO
delta >1MB vs fixed baseline (ref logs/30: 37 sections,
`.text 0x1d403b0`); (4) keep `glGetError`-free release (NXGL-4) — no new
per-frame queries anywhere in the assert path.
Deliver: workflow+script diff, failing-build demo (drop one token → red).
No-go: no flag-value change, no `-O3` experiment, no host/Switch type change.
Done when: hand-`cmake -DCMAKE_C_FLAGS=-D_GNU_SOURCE` fails closed.

## Stream B — texture upload hitches (owns the 500-3880ms spikes)

Files: `engine/platform/plat_gl3d.c` texture section (`:1794-1816`
upload, `:1615-1633` key, `:1556-1609` MSAA target), `plat_gl_int.h`
pool fields only.
Evidence: 43/w1200 `texup 1177600 texupn 45 glup 4.19/498 boot3d 5.89/500`;
40/w900 `glup 4.80/1441 boot3d 6.75/1454`; 41/w1200 `glup 53/3878`.
Steady `texupn 0` costs 0; transition streams 15-45 new textures.
Guide: NXGL-5 (hitch-first-scene = shader compiled on first use),
NXGL-6 (no per-scene variants), NXGL-9/10/12 (re-spec/store/skip rules).
Do: (1) persistent pool keyed by (w,h,format) with LRU cap — `TexImage`
once, `SubImage` on same-size change/new-data, `TexImage` only on size
change; (2) never re-spec an in-use texture mid-frame (defer size-change
to frame top); (3) shader-startup audit: enumerate every
`glCompileProgram`/`glLinkProgram` call site, verify each runs at
init/boot behind loading (never first-use mid-game); hoist any lazy
compile to init; confirm no per-scene/per-material variants (uniforms
only); (4) cold-sync readback audit (NXGL-14/15): prove the remaining
sync `glReadPixels` cannot fire per-frame; if it can, gate it behind
ring-warmth with CPU-rasteriser fallback, else document the call sites;
(5) count `texup/texupn/ngtex` already present — keep, add `texrespec`
counter for size-change path; prove steady windows show `texrescup 0`.
Deliver: diff + bench-pair windows (transition texup down, steady 0 respec).
No-go: no decode/hash change (G-stream owns store), no MSAA default change,
no size/format semantic change.
Done when: 1200-frame field run shows zero >50ms `glup`/`boot3d` maxima
except first transition window.

## Stream C — main2d prep skip (owns the 8-10ms steady)

Files: `engine/g2d/hwcompose.c` + `engine/g2d/g2d_gpu.h` ONLY.
Evidence: run 42 field `main2d 8.45-9.87` biggest steady bucket;
`g2dread 0`, GPU path on. Prep (gather/builds/copies) runs before the
skip at `:1493-1497`.
Guide: NXCPU-5 (skip unchanged screens), NXTHR-1 (pools sized for the
3 app cores — host/par.c already pins 2 workers to cores 1-2; verify
and pin this in a comment, do not resize a correct pool).
Do: compute skip-eligibility from registers BEFORE `alloc_engine`
(hold/need6/capture/dmode/BG0-3D/zero-request); on skip do headers/params
only, skip `gather/build_raster/build_objs/emit_poses` + big `memcpy`
(`:1301,:1362,:1386`); extend `bgv_gen`-style hash to rasters/OBJ/3D with
generation counters (no new full-buffer hash — version integers only);
gate third behind-compose on capture/blend/`hide_2d`.
Deliver: diff + `main2d` steady before/after on bench pair.
No-go: no pixel-mix change (`g2d_gpu.h:76-84` contract), no boot/UI edit,
no new per-frame hash over pixels.
Done when: bench field `main2d` flat under 4ms with zero visual diff
(`EIDOS_G2D_GPU_CHECK` clean).

## Stream D — clear gate + sort hoist (owns frame assembly)

Files: `engine/g3d/hw3d.c` `:1150-1370` + `engine/g3d/raster.c` `:774-830`.
Evidence: field `geom 4-5` steady with `raster 0` (early return `:1331`
taken); clear `~0.9MB` + sort 2 mallocs run before it.
Do: move skip test above clear pixel loops (keep register/toon/wbuffer
capture); KEEP sort call (GPU draws snapshot order) but hoist buffers to
grown-once reuse with `any` early-out (NXCPU-6: no per-frame malloc;
NXGL-16: batched draws, order kept); return hash via out-param (kill
`g_lookup_hash` global if still present).
Deliver: diff + `geom` steady + mallocs/frame (0 on skip).
No-go: no sort deletion, no key/order change, no texture-store edit
(Stream E owns `:573-770` — already landed scratch, do not touch).
Done when: GPU-hit frames do zero clear writes + zero mallocs.

## Stream E — present hitches (owns present split)

Files: `engine/platform/plat_gl.c` present/upload (`:748-798,:824-827,
:1060-1130,:1500-1579`) + `engine/platform/platform.c` `:2055-2110`
(show/upload/fallback) ONLY.
Evidence: steady present 1-3ms, maxima 100-1445ms in `prefin/preswap`
(43/w900 `prefin 1.76/518 preswap 0.78/36`; w1200 `preswap 1.71/142
prefin 0.56/133`; 40/w900 `present 110/1185`). `swapgap 13-32M` unexplained.
Do: (1) find what `prefin` waits on (Finish? fence? swap?) — remove or
fence-gate it, never block steady path (NXGL-4: no per-frame CPU waits
on the GPU); (2) keep SubImage steady, never TexImage (freeze `tex_n`
so `frame_alloc` never reruns) (NXGL-9); version screens, SubImage on
change only (NXGL-12); keep shadow-skip + compose-fail fresh-copy;
(3) document `swapgap/prefin/preswap` in legend (Stream G owns text, E owns
behavior); prove 1200-frame maxima <50ms.
Deliver: diff + present avg/max before/after + legend definitions.
No-go: no swap-interval change (stays 0 + pacer), no timebase change
(`platform.h:568-573` 59.8261Hz), no `platform.c` audio/pacer hunks.
Done when: field maxima flat, no 500ms+ spikes outside transition.

## Stream F — edge target (owns correctness gate)

Files: `engine/platform/plat_gl3d.c` edge/fog (`:1346-1470,:1481-1518`)
+ `plat_gl_int.h` VAO fields ONLY (texture/vertex sections owned by B —
coordinate hunks, do not overlap).
Evidence: outlines-on never run on hardware; magenta-still-black (run 25)
+ missing `DrawBuffers` on `post_fbo` + `vao3` 4-attrib mismatch.
Do: `DrawBuffers(1,{C0})` after binding `post_fbo` in edge+fog; dedicated
empty VAO (gen once) instead of `vao3`; pin raster state at top of both;
keep fail-safe (fault leaves unmarked) + diag dump (alpha-1 probe).
Deliver: diff + desktop edge tests + outlines-on hardware pair (world
correct at any fps — P0 gate).
No-go: no texture/vertex logic change, no shader-version change, no diag
removal, no disabling edge to hit a number.
Done when: field renders correctly with outlines ON on hardware.

## Stream G — logs + session (owns measurement)

Files: `engine/frame/prof.*`, `demo/eidos_boot.c` probe/legend hunks ONLY
(no render logic), `tools-local/session.sh` (new), `logs/next/checklist.txt`.
Legend text lives with the run (checklist), never inside `mesa-switch/`.
Evidence: run-43 bucket set is golden (keep ALL of
`step g3d main2d present audio geom ... g2dread read3d` PLUS
`preup/predraw/preswap/prefin unacc paint_n upbytes_canvas/screen upcalls
want aqueue upskip swapgap pbos skip_n gxcmds/verts/polys pboq ngtex
texup/texupn read_async`); missing: legend, scene proof, park, want=3,
outlines-on, temp.
Do: (1) freeze golden format + one-line `⊃` legend (RENDER⊃G3D⊃UI/READ3D,
BOOT3D⊃gldraw/glup, PRESENT⊃compose+render+swap; nested never summed);
(2) `session.sh NN`: prep from `logs/NN` env/args (assert EDGE=0 first),
mandatory `nxlink -s | tee stdout`, wall≥120s + 4×`prof frame` before exit,
start/end shots with black/menu/loader rejection (size+variance check),
want=3 diagnostic first (present must fall, g2dread stays 0, else stop),
outlines-on repeat, SoC temp start/end, clean `NXEXIT=0` + bytes + prof-count
+ shots or refuse close (ticked boxes only with run id); (3) validator:
steady detector (<10% wall / <15% present drift, zero 0-fps) + scene hash
vs bench reference + binary sha + `-O2` proof + NRO size/sha + Mesa strings
+ `drm_nouveau` 0.
Deliver: script + checklist + legend + validator output on runs 42/43
(showing non-parked void + hitch list).
No-go: no render/behavior change, no bucket removal, no values-asserted
flaky timing test (set contractual, values informational).
Done when: a new run cannot be cited without validator PASS.

## Merge + test build (implementer only)

Order: A (flags) → B,F (gl3d disjoint hunks: B texture, F edge — verify no
overlap) → C → D → E → G (probes/legend last). Resolve only documented
seams. `py_compile` bridge (untouched, sanity). Desktop green + `test_gpu3d`
GL 3.3 + known-fails match base exactly. Rebuild FIXED module (Phase-0
flags — stale module drops everything), NRO native with cached mesa
`16732f6` (`--parts 96`). Record wasm+NRO bytes+sha, `strings` Mesa,
`drm_nouveau` 0, `readelf -S` iff NRO delta >1MB. Write `logs/next/`
env (`gl`, reports, EDGE=0 then on) / args (`--profile`+`300` two lines) /
checklist (bench parked pair + want=3 first + outlines-on + temp + shots +
stdout). Stop, ask human. Claim nothing here.

## Risks

- Phase-0 invalidates 26/28/29 — new baseline only.
- B/F share `plat_gl3d.c`: disjoint hunks; land B, rebase F by hand on
  collision.
- C/D share the frame: C skips 2D prep, D skips 3D clear — independent,
  both need `EIDOS_G2D_GPU_CHECK` / `test_gpu3d` clean.
- E maxima need transition vs steady split — first window excluded.
- F unproven until outlines-on pair; P1 gate stays until then.
- G refuses analysis without PASS — that is the point.
- Not in this plan (guide-tagged future work, no stream): `-O3`/LTO
  trials with measurement (NXCPU-2, plan no-go stands), redundant
  GL state-change tracking (NXGL-18), texture atlas (NXGL-13),
  swap-interval/vsync pacing (NXGL-23, E owns pacer default only),
  docked-mode resolution paths (NXGL-20/24), shader disk cache
  (NXGL-7). None of these is load-bearing for the bench pair.

# perf-plan-round3.md — certain fixes only, build first

Goal: fix what is proven broken, nothing speculative. Baseline run 26:
61.2ms wall = g2dread 22.5 + main2d 8.8 + ui 6 + boot3d 1.9 (gldraw 1.1)
+ game 10 (geom 8.2 inside) + audio 4.2 + present 4.0 + step 3.4.
No Mesa. No aletheia. One test build + checklist at the end, no hardware
runs inside this plan.

Implementer: you do Phase 0 (build) yourself, then dispatch all Phase 1
streams at once, then merge + build + checklist. Subagents write code only.
Commits authored `terezi <jules@foxtails.me>`, `fix:` prefix, no agent lines.

Global no-go: no Mesa/toolchain change beyond flags below, no aletheia,
no wasm ABI / lp64 / wasm_layout change, no feature disable, no `glFinish`,
no per-frame `glGetError`/status in release, no new sync readback, no push /
PR / hardware run, never land on red desktop. No "spend work to save work":
no new hashes, scans, summaries, or caches whose cost rivals the saving.
Each stream below names its exclusive files — a touch outside fails review.

## Phase 0 — repair the build (implementer only, before any subagent)

Broken: `src/wt/pall/build-macos/module/CMakeCache.txt` =
`CMAKE_C_FLAGS=-D_GNU_SOURCE`, build type empty. The `-O2
-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks` half of
`tools/build64.py:34` MODULE_FLAGS was dropped for the wasi-sdk-25
`getentropy` workaround. Result: pall wasm 27.7MB vs base 15.1MB, NRO
52.9 → 61.9MB, 216 objects with 36 at `-O2`, ~180 at `-O0`.

Do:

1. Reconfigure every module dir with the FULL flags:
   `-DCMAKE_C_FLAGS="-O2 -fno-strict-aliasing -fwrapv
   -fno-delete-null-pointer-checks -D_GNU_SOURCE"`
   (or delete the dir and build via `tools/build64.py`, which carries them).
2. Rebuild the module, verify: wasm back to ~15MB, `build.ninja` shows
   `-O2` on all ~213 objects, desktop `cmake --build + ctest` green.
3. Record wasm bytes+sha256. Every Phase 1 branch rebases onto this fixed
   base. No fps claim until Phase 2 rebuilds the NRO from the fixed module.

Deliver: cache diff + wasm size/sha before/after.
No-go: `-O2` without the three `-fno-*`/`-fwrapv` (game code relies on
them); `-O3` experiments; touching anything else in this phase.
Done when: full `-O2` module builds green at ~15MB.

## Contracts (streams never block)

- `void eidos_gl2d_set_wanted(int mask);` S2 adds in
  `engine/platform/plat_gl2d.c` + `plat_gl.h`, default mask `0x3`. S5-boot
  (S6 below) calls it; S2 never needs the caller to exist.
- `int eidos_ui_screens_wanted(void);` already exists. S2/S4-boot read it
  via `engine/ui/ui.h`. No stream declares it twice.

## S1 — bridge double copy (isolated)

Files: `tools/wasm_gl_imports.py` ONLY.
Do: add `glMapBufferRange` to SPECIAL; map/copy only `[offset,offset+length)`,
extend `g_map` 4→8 with `offset/length`, rewrite `glMapBuffer` as
offset-0 wrapper; reuse scratch across unmaps (keep `addr`+`cap`, realloc on
grow only, keep NULL/zero checks); unmap real mapping before returning 0 on
malloc fail; overflow slot returns 0.
Deliver: script diff + `python3 -m py_compile` pass.
No-go: no engine edit, no generated file checked in, no caller change needed.
Done when: py_compile passes; 2 malloc/free per frame gone on map path.

## S2 — PBO enqueue gate (owns the 169ms growth)

Files: `engine/platform/plat_gl2d.c` (compose `~:627-644`, pixels
`:398-423` read-only), `engine/platform/plat_gl.h` (decl).
Do: add `eidos_gl2d_set_wanted` (static mask, default 3, reset `pending`
for dropped screens); gate orphan + `glReadPixels` + ring-advance on
mask/`EIDOS_G2D_GPU_CHECK` only; unwanted screens still draw +
`glCopyTexSubImage2D` (display unaffected); keep ring 3 + `pending>=2`
collect rule; keep orphan-before-reuse.
Deliver: diff + note (queued vs skipped, stale-reset rule).
No-go: no boot edit (S6 owns the call), no `platform.c` edit, no format or
ring-depth change.
Done when: mask 0 = zero enqueues for that screen; mask 3 = today.

## S3 — present shadow copy (owns present hunks)

Files: `engine/platform/platform.c` `:2055-2110` ONLY (show/upload block).
Do: skip `memcpy(shadow,src)` when `gpu2d_frame[i]` set and `nn==1`;
on `gl2d_compose` failure copy `src` into shadow BEFORE `eidos_gl_upload`
(stale-lock fix, 3 lines). Nothing else in this file.
Deliver: ~6-line diff + note.
No-go: no pacer/audio/pump hunk, no `plat_gl.c` edit, no upload-hash edit.
Done when: GPU 1x path does zero shadow copies; fail path uploads fresh.

## S4 — texture hit path (owns store section)

Files: `engine/g3d/hw3d.c` `:573-770` (struct `TexStore`, scratch,
`decode_vram_texture`) ONLY — render_frame below untouched (S5 owns).
Do: compare `key_tex/key_pal` (+ VRAM generation if present, else keys
alone) BEFORE texel/palette malloc + `copy_slot` + full FNV; reuse scratch
buffers; copy+hash only on new/stale; keep `realloc` rare path; keep pool
semantics untouched here.
Deliver: diff + mallocs/hashes per field frame before/after.
No-go: no cache-key change, no draw-order change, no `plat_gl3d.c` edit,
no render_frame edit.
Done when: steady field does zero texture mallocs + zero full hashes.

## S5 — clear gate + sort hoist (owns frame assembly)

Files: `engine/g3d/hw3d.c` `:1150-1370` (render_frame: clear/sort/snapshot/
skip) + `engine/g3d/raster.c` `:774-830` (sort buffers) ONLY.
Do: move the skip test (`g_skip_raster && g_snap_on && valid &&
unsupported==0`) above the clear pixel loops, keeping register capture /
toon / wbuffer running; keep the sort CALL (GPU draws snapshot order —
deleting it breaks blending) but hoist its two buffers to grown-once reuse,
keeping the `any` early-out; keep snapshot (GPU draws from it — pool/version
only if trivial, else leave for later).
Deliver: diff + note (what still runs pre-skip, buffer reuse rule).
No-go: no sort deletion, no key change, no raster-pixel change, no texture
section edit (S4 owns above).
Done when: GPU-hit frames do zero clear writes + zero per-frame mallocs.

## S6 — boot wiring (owns boot file)

Files: `demo/eidos_boot.c` ONLY.
Do: call `eidos_gl2d_set_wanted(want | (force?3:0))` before the show loop
(force = holding || paused || capture || shot — unify BOTH gates `:1206`
and show branch to this); keep `gpu2d_show` unconditional, pixels gated;
move `g_gpu2d_screens++` to the show branch (counts composes, not reads);
start the `G3D` probe AFTER `boot_ui_frame` (stops `g3d` containing `ui`);
keep chunk 546 / latency 2304 / cap 8192; diagnostic note (no commit):
forcing want=3 must drop present if S2's hypothesis holds.
Deliver: boot-only diff + note.
No-go: no UI/GL/compose edit, no threshold change, no summary logic (already
landed — do not touch).
Done when: no unconditional pixels call; counter counts shows; G3D excludes
UI.

## S7 — 3D stalls + edge target (owns gl3d file)

Files: `engine/platform/plat_gl3d.c` + `engine/platform/plat_gl_int.h` ONLY.
Do: delete per-frame `CheckFramebufferStatus` from steady path (keep at
init/resize + `EIDOS_EDGE_DIAG`); route per-frame errors through
release-no-op `EIDOS_GL_CHECK` (init checks stay); orphan vertices
(`BufferData(NULL)` + `SubData`, VAO once); `TexSubImage` on same-size
change else `TexImage`; edge+fog: `DrawBuffers(1,{C0})` after binding
`post_fbo`, dedicated empty VAO instead of `vao3`, pin raster state at top
of both (scissor/depth/stencil/blend/cull off, masks open); keep fail-safe
(fault leaves unmarked) + diag dump (alpha-1 probe).
Deliver: diff + note (queries removed, orphan rule, VAO scope).
No-go: no `hw3d.c`/`plat_gl.c`/boot edit, no shader-version change, no diag
removal.
Done when: steady field does zero status queries + zero errors; `test_gpu3d`
edge cases pass at merge.

## S8 — boost line (owns host entry)

Files: `host/main.c` ONLY (one hunk).
Do: verify `appletSetCpuBoostMode(true)` runs first in `main` under
`__SWITCH__`; if missing, add it; return ignored. Nothing else.
Deliver: 1-line diff/status note.
No-go: no heap-size, guard, fpcast, redirect, or thread change.
Done when: title-mode runs boosted (log or code position confirms).

## Merge + test build (implementer only)

1. Order: Phase 0 base → S1,S8 (isolated) → S4,S5 (same file, S4 hunks
   `:573-770` first, S5 `:1150-1370` second — disjoint, verify no overlap)
   → S2,S7 (seam: setter/call) → S3 → S6-boot-seam → S7. Resolve only at
   documented seams. Anything else fails review.
2. `py_compile` bridge; desktop `cmake --build + ctest +
   tests/test_gpu3d.c` GL 3.3 green (base pre-existing fails
   `test_audio`/`test_mod_wasm` must match exactly — new fail stops).
3. Rebuild the FIXED module (Phase 0 flags — stale module silently drops
   everything), then NRO native with cached mesa `16732f6` prefix
   (`--parts 96`). Record `strings` Mesa, `drm_nouveau` 0, NRO size+sha,
   `readelf -S` if size moved >1MB, wasm size+sha.
4. Write `logs/next/` env (gl, reports, EDGE=0 then on) / args (`--profile`
   + `300` two lines) / checklist (bench spot 120s+, scene shots, `nxlink
   -s` stdout mandatory, want=3 diagnostic first). Stop, ask the human.
   Claim nothing here.

## Risks

- Phase 0 invalidates all old numbers — 26/28/29 never compare to the new
  build. First pair after this plan is the new baseline.
- S4/S5 share `hw3d.c`: disjoint hunks only. If they collide, land S4,
  rebase S5 by hand.
- S2/S6 seam: unset mask defaults to 3 (today's behaviour — safe). All-0
  mask with no drain is the fix, not a leak (reset rule covers re-want).
- S7 probe alpha must be 1 (`unpremultiply` zeroes alpha-0); `pedge`
  compiles once at init — NRO must be rebuilt or the run tests nothing.
- Present diagnostic decides R-growth: force want=3 → present must fall;
  re-gate → present stays down with g2dread 0. Else reopen the queue.

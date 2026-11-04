# perf-plan-round2.md — fix the perf-all regression (16fps → worse)

Goal: undo the run-28 regression and bank the wins. Run 26: 61ms wall,
present 4ms. Run 28: 71 → 107 → 187 → 217ms wall, present 46 → 169ms.
g2dread 22.5 → 0 held, main2d 8.8 → 2-4 held. Everything below is
eidos-side only. No Mesa. No aletheia. No hardware runs inside this plan —
one test build + ready checklist is the end.

Root causes (from the four audits):

- R1: compose still queues a PBO read every frame (`plat_gl2d.c:629-643`),
  never drained now reads are gated. 196KB/screen abandoned per frame.
  Fits 46 → 169ms growth. Biggest item.
- R2: per-frame `CheckFramebufferStatus` (`plat_gl3d.c:1395,1402,1542,1615`)
  + `glGetError` via `EIDOS_GL_CHECK` in debug every frame. gldraw 1 → 12ms,
  boot3d 2 → 13ms.
- R3: boot stub `eidos_boot.c:1567` writes busy=1 always. Forces lower-only
  to miss, keeps bottom screen wanted, title ink zero.
- R4: UI reuse still pays `memset` (`kit.c:81`) + two full copies
  (`ui.c:2313,2388`). `getenv(UI_SKIP)` per recipe per frame (`:2146`).
  `g3d` contains `ui` (`eidos_boot.c:1509`) — not additive.
- R5: hashing over 1MB+/frame: BG 512KB+128KB (`hwcompose.c:1305-1311,:1367,
  :1819`), OBJ (`:1319-1335,:1444`), raster 196KB/layer (`:1425`), present
  2x196KB (`plat_gl.c:780`), per-poly copy+hash on hit (`hw3d.c:655-694`).
- Cleared (do not touch): audio convert (once in SDL), boost (runs), saves
  (not per-frame), EGL attrs, bridge reuse (sound), hwcompose skip (working).

Implementer: set up 8 worktrees/branches from `switch/perf-all` tip
(`src/wt/q1-pbo` … `q6-plat`, branches `switch/fix-q1` … `q6`). Dispatch all
8 subagents at once. Subagents write code only, no builds except
`py_compile`. You merge, build once, write the checklist, stop. Commits
authored `terezi <jules@foxtails.me>`, `fix:` prefix, no agent lines.

Global no-go: no Mesa/toolchain change, no aletheia, no wasm ABI / lp64 /
wasm_layout change, no feature disable to hit a number, no `glFinish`,
no per-frame `glGetError` in release, no new sync readback, no push/PR,
no hardware run, never land on red desktop.

Contracts (so streams never block):

- `int eidos_ui_screens_wanted(void);` exists (P4 owns impl). Q1/Q3 call it
  via `engine/ui/ui.h`. Q1 does not declare it, only includes the header.
- `void eidos_ui_summary_update(int screen,int dark,int flat_rgb,
  int is_flat,int busy);` Q4 implements real logic. Q3 calls it with real
  values (no stub). Q3 does not implement it.
- `void eidos_gl2d_set_wanted(int mask);` Q1 adds in `plat_gl2d.c` /
  `plat_gl.h`. Q3 calls it before compose (one line). Q1 implements.

## Q1 — gate the compose-side PBO (owns the 169ms)

Files (exclusive): `engine/platform/plat_gl2d.c`,
`engine/platform/plat_gl.h`. Do not touch boot, platform.c, bridge.

Do:

1. Read `plat_gl2d.c:620-650` (orphan+`glReadPixels`+ring advance) and
   `:400-420` (map/collect) in full.
2. Add `eidos_gl2d_set_wanted(mask)` storing a static mask + setter. Default
   mask `0x3` (both) so behaviour is unchanged until Q3 calls it.
3. In compose (`~:627-644`): only orphan + `glReadPixels` + ring-advance for
   screens in the mask (or when `EIDOS_G2D_GPU_CHECK` set). Unwanted screens
   still draw + `glCopyTexSubImage2D` into their texture (display keeps
   working), they just skip the CPU-read enqueue.
4. Keep the orphan-before-reuse hunk. Keep ring depth 3, `pending>=2` collect
   rule. On mask-shrink, reset `pending` for dropped screens so a later
   re-want starts clean (no stale collect).
5. Diagnostic support: when mask is 0 for a screen, count skips in a static
   (no log per frame; expose via existing FPS line only if trivial).

Deliver: diff of the two files + 8-line note (when PBO queued vs skipped,
stale-reset rule).

No-go: no boot edit (Q3 owns), no `platform.c` edit (Q6 owns), no format
change, no ring-depth change, no removal of the display texture copy.

Done when: with mask 0, compose does zero `glReadPixels`/orphan for that
screen; with mask 3, identical to today; `grep -n set_wanted plat_gl2d.c`
shows the setter.

## Q2 — kill the 3D sync stall (owns gl3d file)

Files (exclusive): `engine/platform/plat_gl3d.c`,
`engine/platform/plat_gl_int.h`. Do not touch `plat_gl.c`, `hw3d.c`.

Do:

1. Remove per-frame `CheckFramebufferStatus` from steady path:
   `:1395,:1402` (fbo_attach), `:1542,:1615` (edge/fog post). Keep one at
   init/resize (fbo creation) + on `EIDOS_EDGE_DIAG` only.
2. Make `EIDOS_GL_CHECK` release-no-op: under `NDEBUG` return success
   (`plat_gl_int.h:92-94` already does — verify every per-frame call site
   `:1592,:1664,:2266,:2297` uses it, replace any bare `glGetError` on the
   frame path).
3. Vertex upload (`:2114-2141`): orphan steady path with `glBufferData(NULL)`
   + `glBufferSubData`, else `SubData` within `vbo3_cap`. Grow with
   `BufferData` only when `bytes > cap`. VAO attribs once (keep).
4. Keep: empty `vao_edge` in edge/fog only (`:1572,:1654`), `DrawBuffers(1)`
   on post_fbo + restore of 2 in `fbo_attach`/resolve, fail-safe
   (fault leaves unmarked). Do not move DrawBuffers logic.
5. Keep `EIDOS_EDGE_DIAG` dump (caps/masks/viewport/DRAW_BUFFER0/bindings/
   centre pixel). Probe shader keeps alpha 1.

Deliver: diff + note (which queries removed, orphan rule, VAO scope).

No-go: no `hw3d.c` edit, no `plat_gl.c` edit, no DrawBuffers redesign, no
shader-version change, no diag removal.

Done when: steady field does zero `CheckFramebufferStatus` + zero
`glGetError`; `grep -n CheckFramebufferStatus plat_gl3d.c` shows init/diag
only; `test_gpu3d` edge cases still pass at merge.

## Q3 — boot wiring (owns boot file only)

Files (exclusive): `demo/eidos_boot.c`. Do not implement UI or GL functions,
only call them.

Do:

1. Replace the stub at `:1567` (`summary_update(i,0,0,0,1)` both screens)
   with real values: compute dark/busy from the 750-pt grid + flat from the
   9-pt grid over `hi_top/hi_bottom` (or call Q4's `summaries_tick` helper
   if exposed — prefer calling it, do not duplicate the grids). Neutral
   fallback if buffers NULL: dark=0, busy=0, flat=0, is_flat=0 (must not
   force field work).
2. Unify force: `:1206` uses `eidos_ui_holding()`, `:1594` uses
   `bw_mp_paused()`. Make both `holding || paused || capture || shot`.
   Include the `summaries_tick` helper so a hold refreshes.
3. Move `g_gpu2d_screens++` from the pixels branch (`:1605-1610`) to the
   `gpu2d_show` branch (`:1602`) so the log counts composes queued. Keep
   the pixels call gated on want/force. Add one call
   `eidos_gl2d_set_wanted(want|forceMask)` before compose so Q1 stops
   enqueueing for unwanted screens.
4. Fix the `G3D` probe: start it after `boot_ui_frame` (move `t0` for
   `EIDOS_PROF_G3D` to post-UI at `~:1509`), so `g3d` stops containing `ui`.
   Keep `UI`/`RENDER`/`G2DREAD`/`READ3D`/`BCAP` nesting as documented in
   `prof.h`.
5. Diagnostic first (no commit needed, note it): forcing want=3 at
   `:1593-1600` should bring present back down if Q1's hypothesis holds.
   Record the delegation: Q1 is the fix, this is the proof.
6. Leave audio chunk 546, latency 2304, cap 8192 as is.

Deliver: boot-only diff + note (stub replaced by what, force unified to
what, counter moved where).

No-go: no `ui.c/ui.h` edit (Q4), no `plat_gl2d.c` edit (Q1), no `plat_gl3d.c`
edit (Q2), no audio-constant change, no matcher-threshold change.

Done when: no `summary_update(i,0,0,0,1)` remains; `grep -n gpu2d_screens`
shows increment on show; G3D starts post-UI.

## Q4 — UI reuse + summaries (owns UI files)

Files (exclusive): `engine/ui/ui.c`, `engine/ui/ui.h`,
`engine/ui/recipe_field.c`. (Other blitting recipes already call
`want_screens` — do not touch them here.)

Do:

1. Implement real `summary_update` + backing store (per-screen dark, flat
   color+flag, busy/distinct2, ink_sum). `summaries_tick (:1895)`: refresh
   every 3rd frame or on `game_darkness` change; coherent with gating —
   a gated-NULL screen keeps its last summary, never a forced busy=1.
   `flat_screen (:1913)` reads summary (keep).
2. Fix reuse (`:2094,2202,2313,2388`): do not `memset` (`kit.c:81` runs in
   `ui_begin`) a canvas that will be overwritten by `g_prev`; skip the
   gap-copy + keep-prev pair on reuse frames; if the saving is unmeasurable,
   delete reuse (preferred over a fake win). Keep `g_gap==0` requirement
   honest in a comment.
3. Hoist `getenv(EIDOS_UI_SKIP)` out of the match loop (`:2146`) to frame
   top (one call). Keep skip semantics for field profiling.
4. Keep thresholds (12 / 750-pt / 9-pt / 3-frame debounce) identical.

Deliver: diff + note (summary refresh rule, reuse skips what, getenv
hoisted).

No-go: no boot edit (Q3), no compose edit, no matching-behaviour change,
no canvas-size change (`g_k` untouched here).

Done when: reuse frames skip memset+copies or reuse is gone; `grep -n getenv
ui.c` shows one per frame max; summaries refresh without attached pixels.

## Q5a — hwcompose hash diet (owns hwcompose)

Files (exclusive): `engine/g2d/hwcompose.c`, `engine/g2d/g2d_gpu.h`.

Do: hash BG pages (`:1305-1311`) + OBJ (`:1319-1335`) + raster (`:1425`)
only when their generation/counter changed since last compose (reuse the
`bgv_gen`-style guards you already have; add counters where missing).
Skip unchanged raster CPU memcpy + upload. Keep predictor conservatism
(refuse raster/unsupported). Keep `behind`-NULL rule.

Deliver: diff + bytes-hashed per field frame before/after.

No-go: no pixel-mix change, no boot/UI edit, no hash-key change (same
inputs → same key).

Done when: steady field hashes ~0 bytes (all hits); `grep -n _hash hwcompose.c`
shows gen-guarded calls.

## Q5b — present hash diet (owns plat_gl.c upload hunk)

Files (exclusive): `engine/platform/plat_gl.c` upload section only
(`~:753-798`). Do not touch draw/swap/context attrs.

Do: gate `up_hash` (`:780`, 2x196KB FNV per frame) on CPU-fallback screens
only (GPU-composed screens never reach `eidos_gl_upload` with fresh bytes
— skip hashing them entirely); or version-gate (hash only when source
pointer/serial changed); or delete the hash if field never hits (animated).
Keep TexImage-on-size-change / SubImage-steady behaviour (`:786-794`).
Reset statics on context loss/window recreate (stale-hit fix).

Deliver: diff + note (when hashed, stale-reset rule).

No-go: no swap/pacer edit, no behind-logic edit, no EGL-attr edit.

Done when: GPU-path presents hash zero bytes; CPU fallback still skips
identical uploads.

## Q5c — texture hit diet (owns texture hot path)

Files (exclusive): `engine/g3d/hw3d.c` texture section (`:613-710`,
pool `:991-1118` read-only for context) + `engine/g3d/raster.c` read-only.
Edit `hw3d.c` only.

Do: on store hit, skip the texel/palette copy + full FNV (`:626-659`
before `:661-670` test). Compare `(key_tex,key_pal,VRAM generation)` first;
reuse scratch buffers; hash the VRAM source in place on suspected change
only. Keep pool-reuse (`:1063-1078`) + `seen` logic. Do not change draw
order (sort stays, per Q-plan).

Deliver: diff + texture mallocs/hashes per field frame before/after.

No-go: no cache-key change, no snapshot-order change, no `plat_gl3d.c` edit.

Done when: steady field does zero texture mallocs + zero full-buffer hashes.

## Q6 — platform small fixes (owns platform.c only)

Files (exclusive): `engine/platform/platform.c`. Do not touch boot/gl2d/gl.

Do:

1. Stale-fallback (`:2095-2099`): on `gl2d_compose` failure, copy `src[i]`
   into `shadow` before `eidos_gl_upload` (3 lines). Prevents stale/zero
   upload that the hash then skips forever.
2. `audio_queued (:957-959)`: scale by the actual device rate (44100 post-
   conversion frames), not pinned 32768. One-line fix toward underrun.
   No resample-path change (already fast path).

Deliver: 4-line-ish diff + note.

No-go: no `:2027-2090` redesign beyond the fallback copy, no pacer/swap
change, no resample change.

Done when: fallback uploads fresh bytes; queued-count matches device frames.

## Merge + test build (implementer only)

1. Merge into `switch/fix-round2` in order Q5a,Q5b,Q5c,Q2,Q4,Q6,Q1,Q3
   (hash + stall first, wiring last). Resolve only at the documented seams:
   `set_wanted` call (Q1/Q3), summary call (Q3/Q4), `ui.h` include (Q3/Q4).
   A touch outside your files fails review.
2. `python3 -m py_compile tools/wasm_gl_imports.py` (unchanged this round,
   sanity only).
3. Desktop: `cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../aletheia &&
   cmake --build build && ctest --test-dir build --output-on-failure` +
   `tests/test_gpu3d.c` GL 3.3. Green or stop. Known pre-existing fails at
   base (`test_audio`, `test_mod_wasm` at `c707f58`) must match base exactly
   — any new fail stops the line.
4. Module: rebuild the branch module (engine/demo in module — stale module
   silently drops the fix), then NRO native (`DEVKITPRO=/opt/devkitpro
   ... switch/build.py --module <branch wasm> --build-dir /tmp/build-fix2
   --mesa-prefix ~/.cache/eidos/mesa-switch/16732f6…/… --parts 96`).
   Record `strings` Mesa `16732f6`, `drm_nouveau` 0, NRO size+sha256,
   `readelf -S` if size moved >1MB (build-variance check from R5).
5. Write `logs/next/` env/args/checklist (bench spot, 1x, EDGE=0 then on,
   NOWAIT pair, `--profile`/`300` two lines, nxlink `-s` stdout mandatory).
   Stop and ask the human to run. Claim nothing here.

## Risks

- Q1/Q3 seam: if `set_wanted` is never called, default mask 3 keeps today's
  behaviour (safe). If called with 0 always, PBO never drains by design —
  that is the fix, not a leak; the reset rule covers re-want.
- Q2/Q6 seam: DrawBuffers restore + pinned raster state rely on scene setup
  to re-enable. Q2 keeps restore; Q6 does not touch scene state.
- Q3/Q4 seam: neutral summary default (0,0,0,0) is mandatory — busy=1
  forced field work last round.
- Q5a/Q5b/Q5c: same-input-same-key invariant. Any key change fails review.
- Present diagnostic (Q1 §5 / Q3 §5) runs before claiming the win: force
  want=3, present must fall; then re-gate, present must stay down while
  g2dread stays 0. Otherwise reopen R1.

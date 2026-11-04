# perf-plan-round5.md — the in-game wall, from evidence, no guessing

Goal: cut the IN-GAME wall (field 3D scenes sit at 54-77ms; stuck tails
hit 144-201ms), where menus already sit at 33-43ms. Everything below is
argued from 38 runs of logs, not from vibes. No Mesa. No aletheia.
One test build + checklist, no hardware runs inside this plan.

## Where we stand (38 runs, 00-37)

Early (00-12): in-game 6-14fps from day one, titles 44-82fps. Bench
locked run 12 (Mistralton cargo lift, 6-7fps, EDGE=0).
Middle (13-24): outlines paint the field black (all EDGE=1..7 black);
EDGE=0 renders it. Run 22 first profile: 62ms wall = render 39.9
(g2dread 22.5 + main2d 9.4 + geom 8.4 + g3d 6.0) + present 4.0 +
audio 4.1 + step 3.4 + boot3d 1.9.
Baseline (26): bench 61.2ms = game 9.95 (geom 8.18 inside) + render
39.16 + audio 4.19 + present 4.03 + step 3.39 + pump 0.67.
Regression scare (28/29): 71->218ms + present 46->169ms. Two causes,
neither game code: wrong scene (3D interior, not bench) and a -O0
module build (216 objects, ~180 at -O0; wasm 27.7MB, NRO 61.9MB).
Fixed build (30): full `-O2 -fno-strict-aliasing -fwrapv
-fno-delete-null-pointer-checks -D_GNU_SOURCE` -> wasm ~14.5MB, NRO
~52MB, walls flat 44-51. Old numbers never compare; 26/28/29 are
history, not baselines.
Instr era (31-34): present splits preup ~0.6-2.2 / predraw ~0.01 /
preswap ~9-36 (and prefin ~= preswap: the wait is queued GPU work
draining, not the swap call). Scene log flaps fallback/boot/field
every frame -> hysteresis (run 36, ui ~3.4 static, 0 scene lines) +
adaptive repaint (run 37, no /20 strobing). EGL table: every config
is NON_CONFORMANT 0x3051 — no conformant config exists, EGL tuning
is dead, the "slow config" log is permanent noise. The table's
init/terminate black-screened run 33; never touch the shared display
again.
Round 4 (35-37): canvas upload skip (upbytes prove it), static UI
redraw gate, S3 autopsy. Static menus 33-43ms, ui ~3.4. Flicker fixed
per user. Still open: field 54-77ms, stuck tail 144-201ms present
lock after 3D excursion/long park, three hitch flavors (read3d
700-1222ms; boot3d/glup 1045-1380ms with prefin 1300-4143ms; steady
wait/present 540-568ms x5-6/run).

## The hardware we are actually on (Tegra X1, Switch title mode)

- 4x Cortex-A57, 1 core reserved for the OS. Game + render + audio +
  present-submit share 3 cores at 1020MHz (1785MHz boosted).
  Single-thread CPU work is the scarce resource; anything per-frame
  on the frame thread divides 60fps directly.
- Maxwell GPU, 256 cores, 307-768MHz, driven by a reverse-engineered
  Mesa fork (26.2.4 16732f6). No conformant configs, weak command
  submission, expensive sync. Draw-call count and bytes-per-frame
  through it cost more here than on any desktop. The swap path is
  fixed-cost; it cannot be tuned, only fed less.
- 4GB LPDDR4 shared at ~25GB/s. Our footprint (1.5GB heap + 1GB
  module block) leaves headroom, but per-frame megabyte memcpys and
  unbounded GPU-side growth both show up as wall time.
- The game: a DS title (67MHz ARM9 + 33MHz ARM7, 256x192 x2, small
  3D). Raw emulation is single-digit percent of one A57. Nearly all
  of `game` 3-22ms is OUR per-frame port work, not the game.

## The in-game bucket ledger (field 3D scenes, runs 34-37)

1. geom ~8 (bench) / game 16-22 (field): per-GX-command CPU
   processing + snapshot work. NEVER TOUCHED by any round. Prime
   suspect #1. (If exec_cmd's side effects only feed the snapshot,
   and the snapshot only feeds the GPU, the CPU-side repeats are
   dead weight on GPU frames.)
2. present drain+swap ~10-35 typical, 120-183 stuck: GPU-bound on a
   fixed-cost driver. Feeding it less (done: canvas skip) is the
   only lever; EGL tuning is proven dead.
3. main2d ~2-16: CPU compose that feeds the GPU path (verified:
   gpu_fill consumes built bg layers + CPU-built OBJ scratch, so the
   builds cannot simply be deleted). Inherent while scrolling.
4. ui ~3-11 live / ~3.4 static: gated when static (done); live cost
   = kit draw at 640x360 + upload. Size lever exists but costs text
   sharpness — NOT taken silently (user call).
5. game-minus-geom ~2-5, audio ~1-4 (device is 44100, stream
   converts; the 32768 request is not honored), pump/pac NO WAIT.
6. Hitches: read3d sync stalls to 1222ms; boot3d/glup to 1380ms with
   prefin to 4143ms; steady wait/present 540-568ms repeating; stuck
   tail 144-201ms present lock with flat upbytes + want=0. Root
   causes unidentified — the autopsy line (in fix-round4) is the
   instrument that catches them.

## What was missed (the honest list)

- geom was never touched. 8ms at bench, more in field, zero rounds.
- No same-scene bench pair exists on any fixed build. Every
  comparison so far is scene-confounded. The want=3 diagnostic from
  the round-4 checklist was never run either.
- The stuck tail is unexplained. Shape (post-3D/long-park onset,
  flat uploads, want=0) fits GPU-side accumulation: gtex slots only
  ever grow (`while (g->n_gtex < s->n_tex)` + glGenTextures, never
  freed), snapshot pools grow-only. Nobody has audited what grows
  without bound across a 10-minute session.
- read3d's 1.2s sync stall: nobody checked WHEN the DS-size readback
  is actually needed (capture only? every frame?) or made it async.
- Audio converts every frame (44100 device vs 32768 stream) because
  the request isn't honored; nobody found where the override is.
- Canvas size (640x360, 923KB/frame when live) is a quality lever
  the user must approve — never taken unilaterally.

Implementer: set up 3 worktrees/branches from `switch/fix-round4`
tip (`src/wt/r5geom`, `r5stuck`, `r5audio`; branches `switch/fix-r5s1`
…`s3`). Dispatch all 3 subagents at once. Subagents write code only,
no builds. You merge, build once, write the checklist, stop. Commits
authored `terezi <jules@foxtails.me>`, `fix:` prefix, no agent lines.

Global no-go: no Mesa/toolchain change beyond flags, no aletheia, no
wasm ABI / lp64 / wasm_layout change, no feature disable, no `glFinish`
in shipped code, no per-frame `glGetError`/status in release, no new
sync readback, no push/PR/hardware run, never land on red desktop.
No "spend work to save work": no new scans, summaries, or caches whose
cost rivals the saving. Each stream names exclusive files — a touch
outside fails review.

Contracts (streams never block):
- S1 owns `exec_cmd` + snapshot extraction in `engine/g3d/hw3d.c`.
  S2 owns `engine/platform/plat_gl3d.c` texture-slot management.
  No shared edits; S1 never touches texture upload, S2 never touches
  geometry.
- S3 owns audio open/scale hunks only. No other stream touches them.
- The hitch autopsy line (S3 round 4, in base) stays as is; S2's
  resource census reuses its format if needed, no new buckets.

## S1 — geometry processing on GPU frames (owns the 8ms)

Files (exclusive): `engine/g3d/hw3d.c` GX-command path
(`exec_cmd` + its per-command callees) and the snapshot-extraction
read path. Do NOT touch texture upload, sort, pool, or plat_gl3d.c.

Do, in order (read-only first, prove each step before cutting):
1. Map what consumes `exec_cmd` side effects on a GPU frame: matrix
   stack -> snapshot verts? register file -> toon/wbuffer capture?
   List every consumer. Anything with no consumer on GPU frames is
   dead weight — name it with the evidence (who reads it, or that
   nobody does).
2. If snapshot verts are CPU-transformed per vertex per frame, check
   whether the transform can ride the existing vertex attribs +
   uniforms instead (GPU does it per vertex in parallel; the CPU
   does it serially). Prototype only if step 1 proves the CPU copy
   is redundant; otherwise stop after the map and report it.
3. Cut ONLY what step 1 proves dead, keeping bit-identical snapshots
   and register captures. No cache-key change, no draw-order change.

Deliver: the consumer map + diff (or verified no-cut with the map).
No-go: no snapshot-content change, no key change, no raster change.
Done when: GPU frames do zero dead geometry work; `test_gpu3d`
unchanged at merge; bench geom bucket falls or the map explains why
it cannot.
Falsifier (will go south if): the game reads GX/state back (picking?
clipmatrix reads?) — step 1 must check game-visible reads, and any
such read kills the cut for that state.

## S2 — stuck tail: cap GPU-side growth + gate the sync read (owns it)

Files (exclusive): `engine/platform/plat_gl3d.c` texture-slot block
(`gtex`/`gtex_key`/`gtex_w`/`gtex_h` growth + `glGenTextures` sites),
`engine/g3d/hw3d.c` snapshot-pool growth sites (read-only unless the
fix is a one-hunk cap), and the `read3d_1x` call-path decision
(gate or async — same files only).

Do, in order:
1. Audit every per-frame grow-only structure across a 10-minute
   session shape: gtex slots (grown, never freed), tex arrays,
   snapshot pools/seen, vbo caps, PBO ring. Instrument counts ONLY
   (reuse the autopsy-line format; no new buckets): log high-water
   marks per profile window.
2. Cap what grows without bound: shrink gtex to the scene's steady
   need on snapshot-serial change (delete textures above the new
   high-water mark instead of leaking them); same treatment for any
   other grow-only pool the audit proves unbounded. No behavior
   change: re-create on demand exactly as today.
3. read3d: determine WHEN the DS-size sync readback is actually
   needed (capture path only? every frame?). If capture-only, gate
   it off the hot path (skip unless capturing); if per-frame,
   convert to the existing PBO-ring async pattern (never add a new
   sync point). A 1222ms sync stall must become impossible by
   construction, not by tuning.

Deliver: growth table (structure/high-water/bounded?) + diff + note
(read gated when / async how).
No-go: no draw-order or key change, no new sync readback anywhere,
no texture-content change.
Done when: high-water marks stop growing after warmup; no sync
readback exists on the hot path; stuck-tail shape (present lock
with flat uploads) has one fewer suspect.
Falsifier: if the stuck tail reproduces with flat high-waters, the
tail is driver/OS-side (thermal, compositor) — report that with the
numbers, do not invent a second fix.

## S3 — audio convert removal (owns audio open/scale only)

Files (exclusive): the audio-device open path + `audio_queued`
scale site (`engine/platform/platform.c` audio hunks only) and the
boot audio constants if and only if the device rate demands it
(`demo/eidos_boot.c` chunk/latency lines only).

Do:
1. Find where the 32768Hz request is overridden (log says device is
   44100, "the stream converts"). If SDL picks 44100 regardless,
   switch the request to the device's native rate so NO conversion
   runs anywhere (one conversion today, in the stream; target: zero
   conversions on every path).
2. Keep the fast path semantics (raw push when rates match), keep
   latency 2304 + cap 8192 bounds, keep the linear resample as the
   fallback only.
3. Verify by log line (device rate == stream rate == no-convert).

Deliver: diff + note (who overrode, what the rates are now).
No-go: no resample-path redesign, no pacer/swap change, no timebase
change.
Done when: the open log shows matching rates and zero conversion on
all paths.
Falsifier: if the device rate is fixed by the OS mixer and 44100 is
mandatory, take 44100 natively instead — same result (no convert),
different constant. Either outcome removes the convert.

## Merge + test build (implementer only)

1. Order: base fix-round4 → S1, S2, S3 (disjoint files; verify no
   overlap before merging). Resolve nothing — no shared seams by
   design. Anything crossing files fails review.
2. `py_compile` sanity; desktop `cmake --build + ctest +
   tests/test_gpu3d.c` GL 3.3 green (known fails `test_audio`,
   `test_mod_wasm` must match base exactly — any new fail stops).
3. Rebuild the FIXED module (full `-O2 -fno-strict-aliasing -fwrapv
   -fno-delete-null-pointer-checks -D_GNU_SOURCE` — stale module
   silently drops everything), then NRO native with cached mesa
   `16732f6` prefix (`--parts 96`). Record `strings` Mesa,
   `drm_nouveau` 0, NRO + wasm size+sha, `readelf -S` if size moved
   >1MB.
4. Write `logs/next/` env (gl, reports, EDGE=0 then on) / args
   (`--profile` + `300`) / checklist (Mistralton bench 120s+, scene
   shots, `nxlink -s` stdout mandatory, want=3 diagnostic FIRST,
   then the same-scene pair that has never yet been run). Stop, ask
   the human. Claim nothing here.

## Risks

- S1's cut is the deepest in the codebase: the consumer map is the
  deliverable that matters, the code cut is optional. A wrong cut
  breaks 3D correctness silently (wrong verts, stale matrices) — the
  map plus unchanged `test_gpu3d` plus a same-scene hardware pair
  are all required before calling it done, and the pair is the later
  session, not this plan.
- S2's shrink must never delete a texture the next frame needs:
  shrink only to the new serial's proven need, recreate lazily —
  worst case is one re-upload, never a missing texture.
- If S2's audit finds flat high-waters, STOP: the stuck tail is not
  ours to fix in this plan, and the honest output is the numbers.
- Canvas size stays 640x360. Nobody touches it without the user's
  explicit approval — text sharpness is a product call.

# perf-plan-round4.md — kill the menu cost and the hitches, certain fixes only

Goal: cut the steady ~50ms wall where it actually lives. Run 34 (fixed
-O2 build, flat wall, no growth): intro/title-menu wall ~45-51ms =
present ~20-36 (of which GPU drain ~10-20, swap path the rest) + UI
10-20 + uploads ~1MB/frame that never skip + one ~1.5-2s hitch per
300-frame window. g2dread 0, hashing ~0, readbacks gated — those stay.
No Mesa. No aletheia. One test build + checklist, no hardware runs.

Proven by the probe run (run 34):
- prefin ~= preswap (~10ms each) => the swap wait is queued GPU work
  draining, not the swap call. Fix = fewer bytes through the pipe.
- upbytes ~923KB/frame with upskip ~0 => the interface canvas is
  redrawn + re-uploaded every frame, never identical, never skipped.
- ui 19-21ms in title_menu scenes => the software canvas draw itself
  is the biggest single CPU chunk.
- wall max ~1500-2000 every window, in prefin+preswap+ui together.
- All EGL configs are EGL_NON_CONFORMANT => no config fix exists.
  The "slow config" log is permanent noise, not a lead.

Implementer: set up 3 worktrees/branches from `switch/fix-round3` tip
(`src/wt/r4canvas`, `r4uidraw`, `r4hitch`; branches `switch/fix-r4s1` …
`s3`). Dispatch all 3 subagents at once. Subagents write code only,
no builds. You merge, build once, write the checklist, stop. Commits
authored `terezi <jules@foxtails.me>`, `fix:` prefix, no agent lines.

Global no-go: no Mesa/toolchain change beyond flags, no aletheia, no
wasm ABI / lp64 / wasm_layout change, no feature disable, no `glFinish`
in shipped code (the sampled prefin probe stays on instr-run only —
do NOT carry it here), no per-frame `glGetError`/status in release,
no new sync readback, no push/PR/hardware run, never land on red.
No "spend work to save work": no new scans, summaries, or caches whose
cost rivals the saving. Each stream names exclusive files — a touch
outside fails review.

Contracts (streams never block):
- S1 versions uploads by bytes (hash-compare before TexImage, skip
  identical). S2 gates redraws by UI generation. S1 never reads UI
  state; S2 never touches plat upload code. No shared header changes.
- `eidos_prof_add/set` census calls from round 3 may be reused for
  proof counters ONLY (upskip/upbytes pattern); no new buckets, no
  prefin, no present-split changes here (that stays on instr-run).

## S1 — skip identical canvas/screen uploads (owns upload hunks)

Files (exclusive): `engine/platform/plat_gl.c` one_canvas hunk
(`eidos_gl_one_canvas` upload block) + `engine/platform/platform.c`
`eidos_platform_set_ui_canvas` hunk (the rgba/size gate before the
upload call). Nothing else in either file.

Do: hash the canvas/panel bytes before TexImage/SubImage; skip the
upload (and the bind) when identical to the last uploaded; count
upskip/upbytes with the existing census pattern as proof. Keep
TexImage-on-size-change / SubImage-steady. Reset the version on
context loss/window recreate (same stale rule as the screen hash).
Do NOT change draw, swap, or the UI draw itself (S2 owns the redraw).

Deliver: diff + note (when hashed vs skipped, reset rule).
No-go: no draw/swap change, no UI/kit edit, no pacer/audio hunk.
Done when: static menu uploads zero bytes (upskip hits every frame
after the first); animated frames upload as today.

## S2 — redraw the UI canvas only when it changed (owns UI draw)

Files (exclusive): `engine/ui/kit.c` (canvas draw entry) + `engine/ui/ui.c`
(redraw gating only — no matcher/threshold/summary changes, those are
landed and frozen).

Do: gate the kit canvas redraw on UI-model change (generation counter
or equivalent already in the UI layer; if none exists, add the smallest
one at the draw entry — a struct gen already bumped on model writes).
Force redraw on: input this frame, scene change, animation tick, gap
end, first frame after show. When gated: skip kit draw AND leave the
last canvas for S1's upload check (which then skips the upload too).
Keep all thresholds, debounce, summaries, and match behavior identical.

Deliver: diff + note (gate condition, force list, what still draws).
No-go: no matcher/threshold/summary change, no plat upload edit (S1),
no canvas-size change.
Done when: static menu draws zero canvas pixels (ui bucket falls by
the draw share); any input/scene/anim frame draws fully and correctly.

## S3 — 1.5s hitch autopsy + per-frame megabyte audit (owns prof)

Files (exclusive): `engine/frame/prof.h`, `engine/frame/prof.c`
(autopsy print + current-phase tracking) for the diagnostic; audit is
read-only everywhere else. If the audit finds a per-frame alloc/copy
over 1MB, fix it in place in that file (single hunk, same rules).

Do:
1. Track the current probe phase (set on `eidos_prof_begin`, cleared
   on `eidos_prof_end`). On any frame whose wall exceeds 500ms, print
   one autopsy line: phase name, the window census so far, and the
   top-3 buckets by max. Rate-limit to one line per window.
2. Audit every per-frame malloc/realloc/memcpy/hash for operands over
   1MB (snapshot pools, scratch grows, texture copies, canvas, shadow,
   PBO, audio chunks). Census the largest single per-frame alloc
   (`maxalloc`) and the largest single memcpy (`maxcopy`).
3. If an over-1MB per-frame alloc exists and a bounded fix exists
   (cap, reuse, split across frames), do it; else report it as the
   prime hitch suspect with the call site, and stop.

Deliver: diff + note (autopsy format, audit table bytes/site, fixed
or prime suspect with site).
No-go: no behavior change to reach the audit (read-only until the
fix), no new buckets beyond the autopsy line, no glFinish anywhere.

## Merge + test build (implementer only)

1. Order: base fix-round3 → S1, S2, S3 (disjoint files; verify no
   overlap before merging). Resolve nothing — there are no seams by
   design. Anything crossing files fails review.
2. `py_compile` sanity (no python changes expected). Desktop:
   `cmake --build + ctest + tests/test_gpu3d.c` green; known fails
   (`test_audio`, `test_mod_wasm`) must match base exactly — any new
   fail stops the line.
3. Rebuild the FIXED module (full `-O2 -fno-strict-aliasing -fwrapv
   -fno-delete-null-pointer-checks -D_GNU_SOURCE` — stale module
   silently drops everything), then NRO native with cached mesa
   `16732f6` prefix (`--parts 96`). Record `strings` Mesa,
   `drm_nouveau` 0, NRO + wasm size+sha256, `readelf -S` if size moved
   >1MB.
4. Write `logs/next/` env (gl, reports, EDGE=0 then on) / args
   (`--profile` + `300`) / checklist (parked 120s+, scene shots,
   `nxlink -s` stdout mandatory, want=3 diagnostic STILL FIRST before
   any pair). Stop, ask the human. Claim nothing here.

## Risks

- S2's gate must never freeze a live element (cursor blink, fade,
  counter): the force list (input/scene/anim/gap/first-frame) is the
  whole safety case — review it line by line, and when in doubt draw.
- S1+S2 overlap in effect, not in files: S1 proves by upskip,
  S2 proves by the ui bucket. Either alone is a win; together they
  must not double-skip (S1 hashes the actual bytes, so a stale canvas
  can never upload — the ordering is safe).
- S3 may find the hitch is outside our code (driver compile, OS
  service, thermal step): then the deliverable is the autopsy line +
  the suspect, and the fix waits for data — do not invent a fix.
- The prefin probe stays on instr-run. If anyone needs drain numbers
  again, that branch still builds; do not merge it here.

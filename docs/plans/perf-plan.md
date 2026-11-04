# perf-plan.md — Switch field 16fps → 60fps: parallel fix plan

Goal: field at the Mistralton bench spot, handheld 1x, outlines off, NOWAIT,
goes from 61.2ms/frame (16 game fps) to under 16.7ms, with the world correct
with outlines on. Eidos-side engine/host work only. No Mesa changes. No
aletheia edits.

Baseline (run 26, `switch/mesa-p0prof`, NOWAIT, EDGE=0, `--profile 300`):

- wall 61.2ms = render 39.16 + game 9.95 + audio 4.19 + present 4.03 + step 3.39 + pump 0.67
- render 39.16 = g2dread 22.49 + main2d 8.76 + ui 5.96 (inside g3d 5.96, real 3D ~0) + boot3d 1.93 (contains gldraw 1.13)
- game 9.95 contains geom 8.18 (non-snap geom ~7.24, snap 0.94 contains texdec 0.37)
- raster/glread/unprem/capture/read3d all 0.00 here — rasteriser and 3D readback not running at this spot
- outlines on = black 3D, HUD fine, all EDGE=1..7 black; constant-magenta still black (run 25)

This plan is written for an agent implementer that dispatches subagents.
The implementer does setup + dispatch + merge + one test build. Subagents
write code only. No hardware runs inside this plan — the test build plus a
ready checklist is the end.

## 0. Global rules (every stream)

Do:

- Work on 8 worktrees under `src/wt/`: `p1-gate`, `p2-bridge`, `p3-hwcomp`,
  `p4-uisum`, `p5-g3d`, `p6-gl3d`, `p7-audiopres`, `p8-host`. One branch each
  from the same base: `switch/perf-p1` … `switch/perf-p8`.
- One commit per logical change. Message prefix by area (`2d:`, `bridge:`,
  `g2d:`, `ui:`, `g3d:`, `gl3d:`, `audio:`, `host:`). Author
  `terezi <jules@foxtails.me>`, no agent lines.
- Read every file you edit in full before editing. Copy surrounding style
  (C99, 4-space, `/* */`, file header `path -- what it is`).
- Shared engine code keeps desktop/Android/iOS identical. Switch-only code
  behind `__SWITCH__` / `NINTENDO_SWITCH`, or portable logic that is a
  no-op elsewhere (try-first-then-fallback, reuse-scratch, gate-on-flag).
- Small diffs. No reformatting, no renames, no drive-by refactors.

No-go (everywhere):

- No Mesa edits, no toolchain/devkitPro package changes. No aletheia edits.
- No stage-one wasm ABI change, no `lp64.md` design change, no `wasm_layout.h` change.
- No disabling edge/fog/AA/capture to hit a number. Off stays opt-in only.
- No `glFinish`, no per-frame `glGetError` in release, no new sync readbacks,
  no `--allow-multiple-definition`.
- No force push, no push to main/upstream, no PRs inside this plan.
- Streams build nothing except `python3 -m py_compile` for python. The one
  full build happens at merge. Do not start hardware runs.

Contracts (so streams do not block each other):

- `void eidos_ui_want_screens(int mask); int eidos_ui_screens_wanted(void);`
  P4 implements in `engine/ui/ui.c` + `engine/ui/ui.h`. P1 forward-declares
  in `demo/eidos_boot.c` (`extern ...;`) and calls it. Merge swaps the
  forward declaration for the header include.
- `void eidos_ui_summary_update(int screen, int dark, int flat_rgb, int is_flat, int busy);`
  P4 implements in `engine/ui/ui.c`. P1/P3 call it after hi buffers are
  ready. P4 makes it compile even if P1 lands first (stub that stores flags).
- P5 owns `content==0` behaviour in `engine/g3d/hw3d.c:954-962`. P6 only
  tolerates it (no edit there).

Done when for the whole plan: all 8 streams merged into `switch/perf-all`,
desktop `cmake --build + ctest + tests/test_gpu3d.c` green, module + NRO
built once, checklist for the later hardware session written. No fps claim
without a same-scene before/after pair — that pair is the later session,
not this plan.

## P1 — 2D readback gate (owns the 22.5ms copy sites)

Objective: plain field does zero full-screen CPU copies. Display still shows
via the GPU texture.

Files owned (exclusive — no other stream touches these hunks):
`demo/eidos_boot.c` (`:1196-1216` downsample attach, `:1451-1498` hi-buffer
setup, `:1510-1528` show+pixels, `:1782-1783,:1892-1894` held copies),
`engine/platform/platform.c` (`:2027-2105` present incl. `:2068-2070` shadow
copy, `:2141-2149` pixels/show), `engine/platform/plat_gl2d.c` consumer
call sites only (`:398-423` guard, `:627-644` orphan note — implementation
of orphan lives here too, no other stream edits this file).

Do, in order:

1. Forward-declare `eidos_ui_want_screens/screens_wanted` at the top of
   `demo/eidos_boot.c`. Note `/* P4 replaces with ui.h include */`.
2. Gate `demo/eidos_boot.c:1198-1216`: only attach/downsample `screen_rgba[i]`
   when `screens_wanted() & (1<<i)` or hold/capture/screenshot active.
   Pass NULL otherwise.
3. Gate `demo/eidos_boot.c:1520-1528`: only call
   `eidos_platform_gpu2d_pixels` for wanted screens. Leave
   `eidos_platform_gpu2d_show` (pointer store, `:2149`) always on.
4. `engine/platform/platform.c:2068-2070`: skip the `shadow` memcpy when
   `gpu2d_frame[i] != NULL` and the NN path consumes the texture directly.
5. `engine/platform/plat_gl2d.c:627-644`: orphan each PBO before reuse with
   `glBufferData(PIXEL_PACK, 256*192*4, NULL, STREAM_READ)` immediately
   before `glReadPixels`. Keep `glReadPixels(0,0,256,192,RGBA,UBYTE,0)`.
   Do not change format here (RGB stretch is out of scope).
6. Add `eidos_ui_summary_update` call after hi buffers are ready in
   `demo/eidos_boot.c:1451-1498` (values from P4 later; pass through for now).

Deliver: diff of the three files + 10-line note (which screens skipped on
plain field, which callers still force reads).

No-go: no format change, no bridge change (P2 owns), no `ui.c/ui.h` edit
(P4 owns), no hwcompose edit (P3 owns), no present-upload change beyond the
shadow skip (P7 owns `plat_gl.c`).

Done when: `grep -n gpu2d_pixels demo/eidos_boot.c` shows the gate; plain
field path never reaches `plat_gl2d.c:415-418`; `eidos_boot.c` still
compiles with the forward declaration (full build is at merge, syntax care
now).

Subagent prompt: "In worktree src/wt/p1-gate branch switch/perf-p1, implement
P1 above. Read demo/eidos_boot.c, engine/platform/platform.c,
engine/platform/plat_gl2d.c in full first. Small diff, terezi authorship.
Python N/A. Do not build. Return the diff stat + note."

## P2 — wasm GL bridge (isolated, no conflicts)

Objective: remove per-frame malloc/free + full-size copies from the map path.
Halves bridge bytes.

Files owned: `tools/wasm_gl_imports.py` only.

Do, in order:

1. Add `glMapBufferRange` to `SPECIAL` (`:55-57`). Emit
   `w2c_env_glMapBufferRange(env,target,offset,length,access)` that maps only
   `[offset,offset+length)`, mallocs only `length`, copies only `length` on
   read. Extend `g_map` struct at `:350` with `offset/length`, grow slots
   4→8.
2. Rewrite `w2c_env_glMapBuffer (:352-402)` as thin wrapper calling the range
   version with `0,size`.
3. Reuse scratch across `glUnmapBuffer (:404-424)`: keep `addr+cap` per slot,
   reuse when `cap>=needed`, else free+malloc. Preserve NULL checks and
   zero-on-first-alloc. Fix the leak where malloc-fail at `:384-387` returns
   without unmapping (call real unmap before returning 0).
4. Keep `glReadPixels (:199-234)` PBO passthrough as is. No change to the
   RGB shim (screenshot-only).

Deliver: the script diff + `python3 -m py_compile` pass line.

No-go: no engine edit, no generated `gl_imports.c` checked in, no behaviour
change for existing callers (range defaults to full).

Done when: py_compile passes; `grep -n MapBufferRange` shows the new import;
existing `glMapBuffer` callers need no edit.

Subagent prompt: "In src/wt/p2-bridge branch switch/perf-p2, implement P2
above after reading tools/wasm_gl_imports.py in full. Run py_compile.
Return diff stat."

## P3 — hwcompose prep skip (owns 8.8ms setup)

Objective: GPU path skips getting-ready work, not just the final mix.

Files owned: `engine/g2d/hwcompose.c`, `engine/g2d/g2d_gpu.h` only.

Do, in order:

1. Hoist skip-eligibility before `alloc_engine` in `compose_graphics`
   (`:1443-1525`): compute from DISPCNT/capture/need6 (`:1549,:1558,:1811`),
   BG0-is-3D (`:1801-1802`), zero-request (`:1489-1494`), hold (`:1831`).
2. When skipping: headers/params only via `gpu_fill`, skip `gather`
   (`:1116-1119`), `build_text/build_raster/build_objs/emit_obj_poses`
   (`:632-718,:424-460,:601-630,:909-974,:983-1043`), and big copies
   (`:1301 ~576KB, :1362 196KB/layer, :1386 196KB 3D-layer`).
3. Extend `bgv_gen (:1300-1304)` hashing to rasters/OBJ/3D-layer; skip
   unchanged raster uploads + CPU memcpy (`:1356-1362`, up to 786KB).
4. Gate third behind compose (`:1770-1771,:1858-1859,:1861-1876`) on actual
   need (capture off, no blend-over-3D, or last-frame `hide_2d`).

Deliver: diff + note (which functions skipped on field, hash keys used).

No-go: no pixel-mix change (contract `g2d_gpu.h:76-84` stays), no boot edit
(P1 owns), no UI edit (P4 owns), no bridge edit.

Done when: field GPU path reaches `gpu_fill` headers then returns before
`gather`; behind job absent on plain field.

Subagent prompt: "In src/wt/p3-hwcomp branch switch/perf-p3, implement P3
after reading hwcompose.c + g2d_gpu.h in full. Return diff stat + note."

## P4 — UI summaries + want-mask (owns readers)

Objective: matchers run on flags, full pixels only when asked.

Files owned: `engine/ui/ui.h` (`:152-176` frame struct, `:162-171` buffers,
`:288-294` want contract), `engine/ui/ui.c`
(`:193-203` want, `:2003-2004` clear, `:2260-2261` latch, `:1805-1827` flat,
`:2045-2061` match order, `:2182-2226` gap path), `engine/ui/recipe_field.c`
(`:2580-2632` dark/busy/lower, `:1458-1491` sheet, `:2491-2554` entra,
`:812-839,:2314,:2438` objs users), plus blitting recipes only:
`recipe_bag.c:673,683`, `recipe_dex.c:886,1018`, `recipe_pc.c:2150`,
`recipe_menus.c:3728,4127,4231,4269,4306,4601,5645`,
`recipe_title.c:433,1444,1640,1660`, `recipe_input.c:392`. No boot edit.

Do, in order:

1. Add per-screen summary struct in `ui.h` next to `EidosUiFrame`:
   `dark` (max≤12 flag over the 750-pt grid), `flat_color+is_flat` (9-pt),
   `busy/distinct2`, `ink_sum` (title step-8 grid). Add
   `want_screens/screens_wanted` mirroring `want_objs`, plus
   `summary_update` setter.
2. Implement in `ui.c` mirroring `:193-203,:2004,:2260-2261`.
3. Rewrite `dark_screen/busy_screen` to flag tests, `lower_only_match`
   to read summary (keep 3-frame `g_lower_frames` debounce). Rate-limit to
   every 3rd frame + MASTER_BRIGHT fast path (`ui.c:315-327`).
4. Rewrite `flat_screen` to `is_flat && flat==0xFFFFFF`; call site `:2201`
   unchanged in behaviour.
5. Add `want_screens` calls in `run()` of every recipe that blits full
   pixels (list above) the frame before they need them.

Deliver: diff + note (which recipes ask, which now use summary).

No-go: no boot/platform/gl edit (P1/P7 own), no compose change (P3 owns),
no behaviour change to matching (same debounce, same thresholds 12/750-pt/
9-pt).

Done when: `grep -rn screen_rgba engine/ui/` shows readers replaced by
summary except blitting runs; `want_screens` has callers.

Subagent prompt: "In src/wt/p4-uisum branch switch/perf-p4, implement P4
after reading ui.h/ui.c/recipe_field.c in full plus each listed blitting
site. Return diff stat."

## P5 — g3d CPU waste (owns texture/sort/clear/snapshot)

Objective: zero texture mallocs steady-state, no 0.9MB clears or scans on
GPU frames.

Files owned: `engine/g3d/hw3d.c`, `engine/g3d/raster.c` only. Note P6 reads
`hw3d.c:954-962` but does not edit it.

Do, in order:

1. `hw3d.c:626-670`: key-check (`key_tex+key_pal`) against the store before
   texel/palette malloc+`copy_slot`+hash. Reuse scratch texel/pal buffers.
   Only copy+hash when new or stale. Keep `realloc (:685)` rare path.
2. `hw3d.c:1193` clear + `raster.c:783-829` sort: move after the `:1331`
   skip test (or early-out when `g_skip_raster && g_snap_on`). Keep register
   capture/toon (`:1196-1201`) + wbuffer/aa (`:1191-1192`) running. Sort keeps
   the call (GPU draws snapshot order at `plat_gl3d.c:1979`) but hoists `k`
   +`copy` into reusable buffers (`snap_grow` semantics), keeps `any`
   early-out. Fixes the prompt's wrong gate: allocation gated, order kept.
3. `hw3d.c:991-1118`: pool/version snapshot buffers, reuse `seen`
   (`:1058-1059`), keep content-hash dedup (`:1063-1078`). Remaining cost is
   vert/idx/poly copies + bookkeeping.
4. Replace `g_lookup_hash` global (`:713,731,752,1055`) with out-param.

Deliver: diff + per-site malloc count before/after (steady field).

No-go: no draw-order change, no cache-key change, no raster-pixel change,
no `plat_gl3d.c` edit (P6 owns).

Done when: field steady state does no texture-path mallocs; clear/sort
absent on skip-taken frames; `test_g3d/test_gpu3d` expectations unchanged.

Subagent prompt: "In src/wt/p5-g3d branch switch/perf-p5, implement P5 after
reading hw3d.c + raster.c in full. Return diff stat + malloc counts."

## P6 — gl3d upload + edge correctness (owns gl3d file)

Objective: no per-frame re-spec, edge can never blank the field.

Files owned: `engine/platform/plat_gl3d.c`, `engine/platform/plat_gl_int.h`,
`engine/platform/plat_gl.h` only.

Do, in order:

1. Vertex (`:1819-1853,:1925-1944`): orphan once (`glBufferData(NULL)`)+
   `glBufferSubData`, or `SubData` when `need` fits `cap_vtx`. VAO attribs
   once. `glGetError (:2058,:2089)` behind debug-only `EIDOS_GL_CHECK`
   (release returns success, init checks stay).
2. Textures (`:1799-1810`, key `:1615-1633`): per-slot w/h tracking,
   `glTexSubImage2D` on same-size change, `glTexImage2D` only on size change.
3. Edge (`:1346-1370,:1372-1406,:1410-1466`): add
   `glDrawBuffers(1,{C0})` after binding `post_fbo` in edge (`:1439`) and
   fog (`:1481`). Dedicated empty VAO (gen once, no attribs) bound in
   edge (`:1456`) + fog (`:1508`) instead of `vao3`. Pin raster state at top
   of both (disable scissor/depth/stencil/blend/cull, masks open). Keep the
   existing fail-safe (fault leaves frame unmarked, outlines off for session
   with log). Check `glReadBuffer(C0)` in `read_fb (:2158)` and `fb[0]` vs
   `fb[1]` sample path (`plat_gl.c:1010` vs `:2264` — read only, edit lives
   here if needed).
4. Probes: keep `EIDOS_EDGE_DIAG` raster dump (caps, masks, viewport,
   DRAW_BUFFER0, bindings, centre pixel before/after) — do not remove.

Deliver: diff + note (upload path steady-state, edge hypotheses closed).

No-go: no `hw3d.c` edit (P5 owns), no `plat_gl.c` edit (P7 owns), no Mesa,
no shader-version change, probe shader keeps alpha 1 (unpremultiply maps
alpha-0 to zero at `:2115-2146`).

Done when: steady field shows 0 re-specs; edge fail-safe present;
`test_gpu3d` edge cases pass on desktop.

Subagent prompt: "In src/wt/p6-gl3d branch switch/perf-p6, implement P6
after reading plat_gl3d.c + plat_gl_int.h in full. Return diff stat."

## P7 — audio/present/pacer/UI-perf (owns plat_gl.c + audio)

Objective: 1-2ms each off audio/present, UI stops scaling with window.

Files owned: `sdk/arm7/snd_driver.c`, `sdk/arm7/snd_hw.c`,
`engine/platform/platform.c` pacer/audio/pump sections ONLY
(`:701-774` pacer incl. `:711-724` NOWAIT, `:866-1008` resample+open,
`:1790-1986` pump — do NOT touch `:2027-2105,:2141-2149` owned by P1),
`engine/platform/plat_gl.c` (`:748-765` upload, `:824-827` behind,
`:1060-1130` draw_frame, `:1500-1579` canvas/present, `:521-532` ctx attrs
read-only), `engine/ui/ui.c` perf hunks only (`:1995-2000` canvas size,
`:2168` memset, `:2194-2195,:399-400` keep, `:2268` fade — no matcher edits,
P4 owns). Boot audio constants noted for merge (P1 owns boot).

Do, in order:

1. Audio: request 32768 in open (don't override at `:991-996`); keep fast
   path `:889-903` when rates match. Cheapen `cubic16 (:866-874,:914-940)`
   to linear/fixed when resampling. Note for merge: chunk 256→546
   (`demo/eidos_boot.c:1585`), keep latency 2304 + max 8192.
2. Present: keep `SubImage (:762-763)`, never `TexImage (:757-760)` steady
   state (freeze `tex_n` so `frame_alloc at platform.c:1301-1332` never
   reruns); no re-spec behind (`:824-827`); version the two 196KB screens,
   SubImage only on change.
3. Pacer: keep swap 0 + pacer default (`plat_gl.c:548`, `platform.c:701-774`
   sleep bulk `:766-769` else yield `:770-773`). Document swap1+NOWAIT+
   bypass combo, ship only one wait. Remove per-frame `getenv` in hot path.
4. UI: reuse `g_prev` via `hold_canvas (:397-400)` on static field frames;
   run every 2nd/3rd frame or gate on input/scene (`:2106-2119`); `EIDOS_UI_SKIP`
   for field during profiling. `g_k (:1995)` raise left as merge note.

Deliver: diff + note (dev_rate logged, resample path, upload counts).

No-go: no `platform.c:2027-2149` edit (P1), no `plat_gl3d.c` edit (P6), no
`ui.c` matcher edit (P4), no 59.8261Hz timebase change
(`platform.h:568-573`, `:594-597`).

Done when: 32768 fast path taken when honoured; steady present does no
re-spec; UI skips static frames.

Subagent prompt: "In src/wt/p7-audiopres branch switch/perf-p7, implement P7
after reading the listed sections in full. Respect the hunks you do not own.
Return diff stat."

## P8 — host/saves/EGL/boost (owns host + save replace)

Objective: saves survive reboot, EGL tightened, free CPU boost on.

Files owned: `host/main.c` (`:56-62` heap, `:344-351` pin, `:407-522`
mobile_args incl. `:435-456` redirect, `:571-609` switch thread,
`:683-710` boot markers), `host/sdl.c` (`:513-547,:719-763` fps/swap,
`:602-675` ES attrs/retry), `host/wasi.c` (`:750-764` sync, `:919-1125`
open/mkdir/rename), `host/CMakeLists.txt` (`:34-35,:97-114,:160-166,213-234,
317-339`), `host/mem_switch*.c`, `sdk/plat_save.c` (`:146-207` mkdirs/sync/
replace), `engine/platform/plat_replace.c:30-33`,
`engine/platform/settings.c:2319-2365` logging only,
`sdk/arm7/nvram.c:115-181` logging only, `sdk/save.c:101-125` logging only.

Do, in order:

1. Saves: unlink-then-retry in `sdk/plat_save.c:169-172`,
   `engine/platform/plat_replace.c:30-33`, `host/wasi.c:1101-1125` (try
   replace first, on EXISTS unlink+retry once — portable, desktop same
   behaviour). Add errno/strerror logs in `bw_save_write_atomic`,
   `save_model`, `nvram_flush`, WASI rename (gate on `EIDOS_WASI_TRACE`
   where noisy). Verify `profile/saves` mkdir (`main.c:433,460-462` +
   `plat_save.c:176-207` + WASI `:1032-1053`).
2. `host/main.c`: `appletSetCpuBoostMode(true)` early. Batch hot-path
   stderr (redirect at `:435-456` is unbuffered FAT — no per-frame fprintf
   additions).
3. EGL: set `SDL_GL_RED/GREEN/BLUE/ALPHA 8, DEPTH 0, STENCIL 0,
   MULTISAMPLEBUFFERS 0, MULTISAMPLESAMPLES 0, DOUBLEBUFFER 1` before
   `SDL_GL_CreateContext` (engine passthrough; host remap at
   `host/sdl.c:602-621` untouched). Keep swap 0 + pacer (P7 owns the
   decision, P8 only sets attrs).
4. Invalidate hints loaded locally in `plat_gl.c` via P7? No — P8 loads them
   for `plat_gl3d.c` call sites? Neither: to avoid the `plat_gl.c` clash,
   P8 only adds the loader helper in `host/sdl.c` + documents call sites
   (`plat_gl3d.c:2027-2049` resolve, `:1439-1466,:1481-1518` swaps,
   `:2056` bind-0). P6 inserts the calls at merge. Note it.
5. Note for merge: `WASM_RT_NONCONFORMING_ALLOW_OOB_READ_ELIMINATION=1`
   + `-O3` for generated C (bounds checks stay). Guard-pages + fpcast-OFF
   stay diagnostic only.

Deliver: diff + note (rename behaviour, boost line, EGL attrs).

No-go: no guard-pages default change (`EIDOS_SWITCH_GUARD_PAGES` stays OFF),
no fpcast ship change (`EIDOS_HOST_FPCAST` stays ON), no heap-size change
(`__nx_heap_size` stays 1536MiB), no `platform.c:2027-2149` edit.

Done when: replace-fallback present in all three rename sites; boost call
present; EGL attrs set; no per-frame host malloc added (`to_module :96-114`
pattern reused).

Subagent prompt: "In src/wt/p8-host branch switch/perf-p8, implement P8
after reading the listed host + save files in full. Return diff stat."

## Merge + test build (implementer only, after all 8 return)

1. Merge into `switch/perf-all` in order P2,P8,P3,P5,P6,P4,P1,P7 (least
   conflict first). Resolve: forward-decl→`ui.h` include (P1/P4),
   summary call wiring (P1/P3/P4), audio chunk 256→546 + `g_k` note
   (P1/P7), invalidate call insertion (P6/P8). Keep every stream's no-go.
2. `python3 -m py_compile tools/wasm_gl_imports.py`.
3. Desktop: `cmake -S . -B build -G Ninja -DALETHEIA_ROOT=../aletheia &&
   cmake --build build && ctest --test-dir build --output-on-failure` and
   `tests/test_gpu3d.c` GL 3.3. Green or stop — never land on red.
4. Module: rebuild the branch module
   (`cmake --build build-macos/module --target eidos_boot.wasm -j8` or
   `tools/build64.py` per checkout — engine/demo code is in the module,
   a stale module silently drops the fix).
5. NRO native: `DEVKITPRO=/opt/devkitpro PATH=$DEVKITPRO/devkitA64/bin:
   $DEVKITPRO/tools/bin:$PATH python3 switch/build.py --module <branch wasm>
   --build-dir /tmp/build-perf --mesa-prefix
   ~/.cache/eidos/mesa-switch/16732f6…/mesa-install/opt/devkitpro/portlibs/switch
   --parts 96`. Record `strings` Mesa `16732f6`, `drm_nouveau` 0 refs,
   NRO size + sha256. Full NRO ~9min, host-only ~12s.
6. Write `logs/next/env.txt` (`EIDOS_RENDERER=gl`, FPS/RENDERER/GPU3D reports;
   `EIDOS_EDGE=0` until edge confirmed; NOWAIT only for attribution),
   `args.txt` (`--profile` + `300` on two lines — one-per-line or usage
   exit), and the hardware checklist (title, bench spot ≥120s, scene shots,
   nxlink `-s` stdout capture — stderr alone voids profile). Stop and ask
   the human to run. Do not claim fps here.

## Risks

- `eidos_boot.c` merge: P1 owns it; P4/P7 leave notes, not edits. Merge
  applies the 3 one-liners by hand.
- `platform.c` split: P1 owns `:2027-2149`, P7 owns `:701-774,:866-1008,
  :1790-1986`. A touch outside your hunks fails review.
- `plat_gl.c` single owner (P7). P6/P8 read it, note call sites, do not edit.
- Sort-order trap (P5): gating allocation, not the call — GPU draws snapshot
  order, deleting the call breaks blending.
- Magenta-trap (P6): probe shader must write alpha 1; `pedge` compiles once
  at init (`:1216`) so the NRO must be rebuilt or the run tests nothing;
  caches (`:2107,:2252`) can redisplay stale black.
- FAT-trap (P8): rename-replace never worked on sdmc; unlink-retry loses
  crash-atomicity (FAT has none anyway) — keep try-first ordering.
- Slow-config line is noise (all configs NON_CONFORMANT) — EGL attrs still
  worth tightening, but not the 16fps cause. The 22.5+8.8 2D wins are.

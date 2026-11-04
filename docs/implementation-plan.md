# eidos on Switch: mesa-switch GPU 3D at 60 fps — implementation plan

Revision 3, 2026-11-02. Written for agents. Every slice lists the same four things:

- **Do:** the steps to take, in order.
- **Deliver:** the exact artifacts to produce.
- **No-go:** what is forbidden in that slice.
- **Done when:** the checkable exit condition.

Sources: `mesa-switch.md`, `eidos-gl.md`, `mesa-feasibility.md`, `bottlenecks.md` in this folder, plus an exploration of the tree on 2026-11-02.

**Path conventions.** Bare paths are under `/Users/terezi/bw-switch/src/eidos`. `mesa:` means `/Users/terezi/bw-switch/src/mesa-switch` (our personal **local** fork, see §2), `logs/` means `/Users/terezi/bw-switch/logs`, `plan:` means `/Users/terezi/bw-switch/switch-plan.md`, and `fix-ts` means `/Users/terezi/bw-switch/fix-ts.md`.

### Changelog: rev 2 → rev 3
Absorbs `recovery-plan.md` (§2 overlap verdict, §3 Step 0) and the maintainer's decisions of 2026-11-02. `recovery-plan.md` no longer overrides this file.
- **§0** rewritten for the real state: eidos `e229250`, mesa `16732f6`, A1–A4 done, A5 ran and failed P0, out-of-plan perf commits listed.
- **Mesa pin** is a SHA in our local fork; nothing is pushed upstream. The "no PRs on danfromtico" rule is replaced. **No further Mesa changes** (fix-ts rule) is now policy. The recovery plan's S3 ("make the pin fetchable") is resolved by this decision and dropped.
- **§2 capture protocol** corrected: `--profile` prints to stdout, so `nxlink -s` capture is mandatory; `args.txt` takes one argv per line; `env.txt`/`args.txt` are stored per run under `logs/<NN>/`.
- **AA off by default** (`803f322`) accepted as a **temporary exception** until C6.
- **§1 scenes** fixed: two Mistralton Gym saves (answers rev 2 Q2).
- **A5** recorded as P0 FAIL. **A5b** = fix-ts Phase 1, plus the owed Zink run.
- **B1** parser reads the nxlink stdout capture; "landed before baseline" commit list added.
- **C2** step 5 replaced by fix-ts Phase 2 (remove the sync field readback).
- **New slices:** C7 (texture-hit zero-copy), E6 (geom-sort gate, CPU clear gate, snapshot pool, heap lock measure-first), S1 (settings/nvram persistence, before F2), S2 (`test_mod_loader` fix, blocks every C/D/E slice).
- **F4** CI reworded for the local-fork pin. **§3** graph and **§4** questions updated.

---

## 0. Starting state (re-verified 2026-11-02, rev 3)

| Fact | Evidence |
|---|---|
| eidos `perf/switch-integration` @ `e229250` (clean). mesa local fork `perf/switch-integration` @ `16732f61427b795abf37e40d957cb62213caec8a`. Current NRO is `out/eidos.nro` | `out/BUILD.md` |
| A1–A4 are **done**. The NRO links the mesa-switch prefix: no duplicate EGL, `drm_nouveau` = 0 | `sdk-build.md`, `8a0e187`, `out/BUILD.md` step 7 |
| `16732f6` and the older pin `0ff52079` exist **only in the local fork** (not on any `origin` branch). `16732f6` = `0ff52079` + local `perf/*` + `fix/switch-no-compression` merges | git in `mesa:` |
| A5 ran on hardware and **failed P0**: title renders, the field is black with outlines on (NVC0). With outlines off the world renders, so the edge pass is the defect. Zink run (b) was never done | logs/06–09, fix-ts §0 |
| mesa-switch reports `OpenGL NV12B (OpenGL ES 3.2 Mesa 26.2.4 …)`, GPU 3D features `0x3`, integer samples 8, unsupported `0x0` in every field log | logs/06–09, 11 |
| Switch now defaults to `EIDOS_RENDERER=gl` (`c8249d3`), with no SDL fallback yet | `host/main.c:407-409` |
| Switch forces `--resolution 1` | `host/main.c:~450` |
| AA is **off by default on Switch** (`803f322`, `msaa_wanted()`); `EIDOS_MSAA=1` re-enables it. Accepted as a temporary exception until C6 (§2) | `plat_gl3d.c:1534` |
| A field frame is 1 scene draw + 1 **synchronous** readback (`glReadPixels` + `malloc` + full-frame `unprem`); the PBO ring (`4a7768d`) is unused on that path | fix-ts §0, `demo/eidos_boot.c:1406-1416` |
| Every log's fps collapses after the title (logs/11, NOWAIT + EDGE=0: ~49 fps for ~85 s, then ~25). fix-ts telemetry reads the collapses as CPU-bound | logs/*, fix-ts §0 |
| No usable `--profile` data: logs/10 died on `--profile 300` written as one `args.txt` line; logs/11 did not capture stdout | logs/10, 11 |
| Settings and the firmware flash do not persist on sdmc (`cannot replace …/settings.ini`, `nvram: cannot write the firmware flash`) | logs/07, 09 (settings); every log (nvram) |
| `tests/test_mod_loader.c` does not compile on wasi-sdk 25.0, so `ctest` is red | fix-ts L2 |
| Rear bitmap and shadow polygons have no GPU path. Fog and edge do have one; the comments that say otherwise are stale | `engine/g3d/hw3d.c:1136-1149`, `engine/platform/plat_gl3d.c:42-43,1215-1261` |
| Textures are fully re-specified when their hash changes. There is no SubImage path. Texture **hits** still malloc + copy + hash | `plat_gl3d.c:1738-1742`, `hw3d.c:626-708` |
| Guard pages fail (`svcMapMemory(268435456) failed`) on the 20.1-era builds; bounds checks are on | logs/01–03 |

**Landed before baseline** (out-of-plan or unmeasured; B1 measures them as they are, it does not revert them): eidos `b37bbe9` + `938e53f` (guard-page experiment, E1), `c8249d3` (gl default, F1 part), `c323c54` (core pinning, E4), `4a7768d` (PBO ring, C2 part), `cd51064` (polygon state in vertices), `803f322` (AA off on Switch), `3f84f9f` (single 3D draw at 1x). Mesa local fork: `perf/staging-cached`, `fix/switch-no-compression`, `perf/zero-bufs`, `perf/tex-upload`, `perf/submit-overhead` (merged into `16732f6`).

## 1. Performance milestones

Fps is measured with `--profile` / `300` (two `args.txt` lines, stdout captured over `nxlink -s`, §2) over steady-state frames. All milestones use mesa-switch with `EIDOS_RENDERER=gl`. The canonical scenes (maintainer, 2026-11-02):

| Scene | Save | What to do |
|---|---|---|
| **field** | Mistralton Gym save with a few trainers left. Path: TBD (being located) | walk the gym floor, same route each run |
| **battle** | same save | fight one of the remaining gym trainers (move animations) |
| **leader** | second save, right before the gym leader fight. Path: TBD (being located) | start the leader battle |

The leader save is right before Skyla (confirmed 2026-11-02). Until the saves are located, a run states the scene it actually used.

| ID | Mode | Internal res | Bar | Status |
|---|---|---|---|---|
| **P0** | handheld 1280×720 | 1x | world renders correctly at any fps | entry gate (end of Phase A) |
| **P1** | handheld | 1x | ≥ 59.8 fps avg, p99 ≤ 16.7 ms, field + battle | required |
| **P2** | handheld | **2x** | ≥ 59.8 fps avg, p99 ≤ 16.7 ms, field + battle; 30-min soak without decay | **required: ship gate** |
| P3 | docked 1920×1080 | 2x | same bar | stretch |
| P4 | docked | 3x+ | same bar | stretch, no work scheduled |

P2 is what flips Switch's defaults to `gl` + `--resolution 2` (Phase F).

## 2. Global rules (apply to every slice)

**Do:**
- Work on a branch `switch/mesa-<slice>` in eidos, in its own worktree under `src/wt/<slice>`. (`mesa:` gets no new branches, see the no-go list.)
- Make one commit per logical change, with the message prefix used by the repo (`switch:`, `host:`, `gl3d:`, `g3d:`, `tools:`, `docs:`).
- Before and after every slice that changes C code, run the desktop build plus `tests/test_gpu3d.c` with GL 3.3. Also run the existing test suite (`ctest` must be green; S2 makes it so).
- Record every on-device measurement in `plan:` → Results. Each row has the date, the eidos commit, the mesa-switch commit, the backend, the mode, the resolution, the scene, avg fps, p99 ms and the top three `prof.h` buckets.
- End every slice with a short hand-off note in the commit body: what changed, what was measured, what is left.
- Commits are authored `terezi <jules@foxtails.me>`, with no agent attribution lines.

**No-go (everywhere):**
- No fabricated or estimated device results. Hardware steps are done by the human. The agent prepares the build plus an `env.txt` + checklist, then **stops and asks**.
- No changes to the stage-one wasm ABI, `docs/lp64.md` design, or `sdk/wasm_layout.h`.
- No edits to aletheia.
- No Switch-only forks of engine logic. Engine changes go in shared code and must keep desktop/Android/iOS behavior identical. Switch-specific code is allowed only in `host/`, `switch/`, and behind `__SWITCH__`/`NINTENDO_SWITCH`.
- No vendoring Mesa into eidos. Mesa is consumed as a prefix built from a pinned commit.
- No disabling visual features (edge, fog, AA, capture) to hit a number unless the slice says so. If a feature is degraded, it must be a documented runtime option that defaults to correct output.
  - **Exception (temporary):** `803f322` defaults AA off on Switch (`EIDOS_MSAA=1` re-enables it). Accepted by the maintainer until C6 picks MSAA or SSAA; C6 removes the exception.
- No `glFinish`, no per-call `glGetError` in release, no new synchronous readbacks.
- **No further Mesa changes.** Mesa stays frozen at `16732f6` in the local fork. A suspected driver bug gets a repro under `switch-mesa/repro/`, not a patch.
- No force pushes and no pushes to `main`/upstream. Nothing from the Mesa fork is pushed or PR'd anywhere.
- No moving on past a failed "Done when". Stop and report.

**Mesa pin.** Mesa lives in our personal local fork at `mesa:`, branch `perf/switch-integration`, pinned to `16732f61427b795abf37e40d957cb62213caec8a` (`COMMIT` in `tools/fetch_mesa_switch.py`). The pin is not fetchable from any remote. `fetch_mesa_switch.py` never fetches: it uses the cached prefix, or with `--build` builds from the checkout named by `EIDOS_MESA_SWITCH_SOURCE`. The default source is the sibling `<eidos repo root>/../mesa-switch`, i.e. `src/mesa-switch` for the main checkout. From a worktree under `src/wt/` the sibling doesn't exist, so set `EIDOS_MESA_SWITCH_SOURCE`. The checkout must be at the pinned SHA and clean.

**Standard device capture `env.txt`** (the "standard capture"):
```
EIDOS_NXLINK=1
EIDOS_FPS_LOG=1
EIDOS_RENDERER_REPORT=1
EIDOS_GPU3D_REPORT=1
EIDOS_RENDERER=gl
```
Until A5b lands, add `EIDOS_EDGE=0` for any run that isn't about the edge pass. The in-game setting doesn't persist (S1).

**`args.txt`** takes **one argv entry per line** (`host/main.c:467-475`). `--profile 300` on one line is a single unknown argument, and the program exits with usage (logs/10). Write:
```
--profile
300
--resolution
<N>
```
**`--profile` prints to stdout** (`engine/frame/prof.c:61-70`, one `prof frame …` line every N frames). On device stdout is lost unless `EIDOS_NXLINK=1` is set and the PC runs `nxlink -s`. `stderr.txt` never contains it. Save the nxlink output as `logs/<NN>/stdout.txt`. A profile run without it is void.

**Per run**, store under `logs/<NN>/`: `boot.txt`, `stderr.txt`, `stdout.txt` (if profiled), the exact `env.txt` and `args.txt` used, and title + scene screenshots. The agent must state the exact files in each hardware checklist. A run that fails the entry conditions (title-mode boot, unsupported `0x0`) is not a data point.

---

## Phase A — Build on mesa-switch and reach P0

**Status (rev 3):** A1–A4 done (`sdk-build.md`, branches `switch/mesa-a2..a4`, `8a0e187`; pin moved to `16732f6` in `ebb17bb`). A5 ran and failed P0. A5b is next. The A1–A4 text below is kept as the record.

### A1. Build the mesa-switch SDK
**Do:**
1. Check out the current HEAD of `mesa:`. Record the SHA.
2. Read `mesa:docs/switch-opengl.rst`, `build-opengl.sh` and `build-unified.sh`.
3. Build the GL-only SDK (`build-opengl.sh`, Docker or devkitPro Linux). Install it to `~/.cache/eidos/mesa-switch/<sha>/`.
4. List what was produced: libraries, headers, pkg-config files, CMake config files.

**Deliver:**
- The installed prefix.
- `switch-mesa/sdk-build.md`: the host OS, the commands as run, the SHA, the toolchain versions (devkitA64, libnx, Rust, Meson), the file list, and the size of each `.a`.

**No-go:**
- No source edits in `mesa:`.
- No building the Vulkan/unified SDK in this slice.
- No `ALLOW_DIRTY=1`.

**Done when:** the prefix contains static EGL and GLESv2 (or GL) archives plus headers, and `sdk-build.md` lets someone reproduce the build.

### A2. Fetch/build helper
**Do:**
1. Write `tools/fetch_mesa_switch.py`, modeled on `tools/fetch_sdl_switch.py`. It pins the commit SHA, uses the cache root `EIDOS_MESA_SWITCH_CACHE` (default `~/.cache/eidos/mesa-switch`), takes `--build` to run A1's command, and prints the prefix as its last line.
2. Add `--mesa-prefix` to `switch/build.py`. When it is absent, call the helper.

**Deliver:** the helper script, the `switch/build.py` change, and a usage paragraph in `switch/README.md`.

**No-go:**
- No network fetch when the cache is already populated.
- No floating branch refs; pin the SHA only.

**Done when:** `python3 tools/fetch_mesa_switch.py` prints the prefix, and running it twice is a no-op.

### A3. Link eidos against mesa-switch
**Do:**
1. In `host/CMakeLists.txt`'s `NINTENDO_SWITCH` branch, add a cache variable `EIDOS_SWITCH_MESA_PREFIX`.
2. When the variable is set, link with the prefix's exported targets (`OpenGL::EGL`, `OpenGL::GLES2`/`OpenGL::GL`), or with explicit archives if no targets are exported, and **remove `glapi drm_nouveau`**.
3. When it is unset, keep today's line unchanged.
4. Make sure devkitPro SDL resolves `eglGetProcAddress`/`eglGetDisplay` from mesa-switch. Inspect the final link map for duplicate EGL symbols.
5. Build the NRO.

**Deliver:**
- The CMake change.
- `nm eidos.elf | grep -c drm_` = 0, recorded in the commit body.
- The link-map excerpt showing where the EGL symbols come from.
- The NRO size.

**No-go:**
- No patching devkitPro SDL in this slice.
- No removing the old link line yet (A6 does that).
- No `-Wl,--allow-multiple-definition`.

**Done when:** the NRO links on the mesa-switch prefix, with no duplicate EGL definitions and no drm_nouveau symbols.

### A4. Request the real GLES version
**Do:**
1. In `host/sdl.c:566-578`, under `__SWITCH__` only, try ES 3.2. If `SDL_GL_CreateContext` fails, fall back to 3.1 and then 3.0.
2. Have `report_gl()` keep setting `host_gl_es_version` from the actual string.
3. In `tools/wasm_gl_imports.py:269-286`, gate the `MAX_INTEGER_SAMPLES` → 0 override on `host_gl_es_version < 31` (the real version), not on the requested one. Regenerate.
4. On desktop, check that the generated `gl_imports.c` diff is limited to that branch.

**Deliver:** the code change, the regenerated imports, and the desktop test pass.

**No-go:**
- No change to Android/iOS context requests.
- No change to shader `#version` rewriting in this slice.

**Done when:** desktop is unchanged and the Switch build compiles.

### A5. First boot on hardware (P0)
**Do (agent):**
1. Build two NROs: `eidos-mesa.nro` (A3+A4) and `eidos-old.nro` (the old line).
2. Write `switch-mesa/checklists/A5.md`. It must say exactly what to copy where, list the standard capture `env.txt`, and give three runs:
   - (a) default backend (NVC0)
   - (b) `MESA_SWITCH_GL_DRIVER=zink`
   - (c) `eidos-old.nro`, as a baseline
   Each run boots to the title, goes to the first town, waits 60 s, then quits via HOME. The checklist must say which `stderr.txt` lines to send back and which screenshots to take (title, town).
3. **Stop and ask the human to run it.**
4. Parse the returned logs. Record the GL strings, `GL_MAX_SAMPLES`, `GL_MAX_INTEGER_SAMPLES`, the GPU3D feature mask, the fps series and any errors.

**Deliver:** the checklist, the parsed results table in `plan:` Results, and screenshots stored under `logs/A5/`.

**No-go:**
- No interpreting missing logs as success.
- No code changes during this slice.

**Done when (P0):** at least one mesa-switch backend renders the title and the town correctly, with no black world. If neither does, go to A5b. Otherwise skip A5b.

**Result (rev 3): P0 FAIL.** Run (a) NVC0: title renders, field black with outlines on (logs/06–09). With outlines off the world renders (logs/09). Run (b) Zink was **never done** and is owed in A5b. Run (c), the old-Mesa baseline, is logs/00–02. The Results row is in `plan:`. No env/args were stored for logs/04–11, and `logs/A5/` is empty.

### A5b. Edge pass writes black on NVC0 (= fix-ts Phase 1)
The rev 2 bisect set is superseded. Fog, MSAA, rear-bitmap and shadow are already exonerated (fix-ts §0), and `EIDOS_EDGE=0` alone fixes the world.

**Do:**
1. Write `switch-mesa/checklists/A5b.md` (protocol in §2). One hardware batch:
   - field runs with `EIDOS_EDGE=1` … `7`, one each, same spot (variants at `plat_gl3d.c:174-189`) (fix-ts 1a)
   - the owed A5 run (b): `MESA_SWITCH_GL_DRIVER=zink`, edge **on**. If Zink draws outlines, the bug is likely NVC0-side; record it for B3.
   - one attribution run on the current tree (`--profile`/`300`, `EIDOS_NOWAIT=1`, `EIDOS_EDGE=0`, nxlink `-s`), as the "before" for everything
2. **Stop and ask the human to run it.**
3. From the split, reduce to the failing construct, one variable per host-only rebuild (fix-ts 1b suspects: `usampler2D`+`texelFetch` of the R8UI ID target; the `gl_VertexID` fullscreen triangle; `DrawBuffers`/reattach order in `edge_pass`, `plat_gl3d.c:1408-1464`).
4. Apply the minimal fix and add a `tests/test_gpu3d.c` edge case that fails without it (fix-ts 1c). If it is a driver bug, write a standalone repro NRO under `switch-mesa/repro/<name>/` instead. **Don't patch Mesa.**

**Deliver:** the per-variant table (black/correct/partial), the Zink result, the named construct with a one-paragraph root cause, and the fix + test (or the repro).

**No-go:**
- No rewriting the pass blind. No disabling edge globally to hit a number.
- No Mesa changes. No filing the repro upstream.

**Done when:** desktop tests pass and hardware renders the field correctly with outlines **on**, at any fps (P0). Otherwise the repro is ready and the maintainer has been asked how to proceed.

### A6. Retire the 20.1 path
**Do:**
1. Remove the old link line and the `switch-mesa`/`switch-libdrm_nouveau` dependencies from `host/CMakeLists.txt`, `switch/build.py` and `switch/README.md`.
2. Keep the mesa-switch pin in the README (`16732f6`, local fork; S0 documented it).
3. Put a correction note in `switch-mesa/mesa-feasibility.md` and `mesa-switch.md` (the facts superseded by §0). Do not rewrite the history in those docs.

**Deliver:** the commits and the updated README version table.

**No-go:** no deleting the old docs; annotate them.

**Done when:** a clean build uses only the mesa-switch prefix.

---

## Phase B — Baseline and attribution

### B1. Profile matrix
Measures the tree as it stands, including everything in §0 "Landed before baseline". `baseline.md` lists those commits under that heading; they are measured, not reverted.

**Do:**
1. Write `switch-mesa/checklists/B1.md` using the §2 protocol (two-line `--profile`/`300`, nxlink `-s` stdout capture, env/args stored per run).
   - Matrix: scenes {title, field, battle, leader} (§1) × backends {nvc0, zink} × renderer {gl, sdl} × res {1x, 2x}, handheld. The sdl renderer runs only at 1x.
   - Run `EIDOS_NOWAIT=1` once per scene for the raw cost.
   - Do a 10-minute field run on gl and on sdl to answer whether the fps decay is caused by GL or by the CPU. Every log so far collapses; logs/11 drops from ~49 to ~25 fps with NOWAIT and EDGE=0.
2. Stop for the human run.
3. Write `tools/switch_prof_report.py`. It parses the **nxlink stdout capture** (`stdout.txt`, `prof frame …` lines) for buckets, and `stderr.txt` for the `eidos: N fps` series. Output is CSV: per-bucket mean/p50/p99 and the fps series. A run with no `stdout.txt` is rejected, not treated as zero.
4. Produce the table and name the top three buckets per scene and resolution.

**Deliver:** the checklist, the parser, `switch-mesa/baseline.md` (tables, a decay analysis, and the "landed before baseline" list), and the Results rows.

**No-go:**
- No optimization work started before `baseline.md` exists. (The rule was broken before rev 3; see §0. Those commits are grandfathered as "landed before baseline". Nothing new lands early.)
- No dropping cells from the matrix without saying why.

**Done when:** `baseline.md` gives the P1 and P2 gap in ms per scene, and attributes it to buckets that together explain ≥ 80% of the frame.

### B2. CPU-fallback census
**Do:** from the B1 logs (`EIDOS_GPU3D_REPORT`), count the frames per scene with `unsupported != 0` and break them down by bit (EDGE/FOG/REARBMP/SHADOW). The field is already `0x0` in every log. Battle and leader are the open cells.

**Deliver:** a census table in `baseline.md`.

**No-go:** no guessing about scenes that weren't captured.

**Done when:** each bit's frequency per scene is known.

### B3. Pick the backend and order the work
**Do:**
1. Choose NVC0 or Zink on correctness first, then on P2 frame time.
2. Hard-code the choice in `host/main.c` (Switch only) with `setenv("MESA_SWITCH_GL_DRIVER", …, 0)` before SDL/EGL init, so `env.txt` still overrides it.
3. Rewrite the order of Phases C–E in `baseline.md` so the biggest measured bucket comes first.

**Deliver:** the commit and a prioritized slice list.

**No-go:** no keeping both backends as defaults by platform variant.

**Done when:** the default is committed and the slice order is written down.

---

## Phase C — GPU-side waste (shared engine code + host shims)

**Each C slice must:**
- start after S2 (green `ctest`) and after `baseline.md` exists
- leave `tests/test_gpu3d.c` passing
- produce one before/after device row at **2x handheld** for field and battle
- name the bucket it targets

### C1. Persistent textures (`glup`)
**Do:**
1. In `plat_gl3d.c`, add a texture pool keyed by (w, h, internal format). Allocate with `glTexImage2D` once (or `glTexStorage2D` if the context is ES ≥ 3.0 or GL ≥ 4.2; add it to `GL_FUNCS` as optional).
2. Replace the re-spec at `:1742` with `glTexSubImage2D` into a pooled slot when the size and format match.
3. Apply the same to the presenter screen textures (`plat_gl.c:740-756`). Re-spec those only on size change.
4. Add `glTexSubImage2D` to `GL_FUNCS` and check that `wasm_gl_imports.py` generates it.

**Deliver:**
- the code
- a desktop counter, behind `EIDOS_GPU3D_REPORT`, that prints re-specs/sec
- a desktop and device `glup` before/after

**No-go:**
- No change to texture decode or hash semantics in `hw3d.c`.
- No unbounded pool growth: cap it, then evict LRU.

**Done when:** field steady state shows 0 re-specs/sec and `glup` drops measurably on device.

### C2. Readback without stalls (`glread`, `unprem`)
Step 1 already landed (`4a7768d`, unmeasured). Step 5 (fix-ts Phase 2) comes **first**: it is the biggest expected win.

**Do:**
1. ~~Allocate a ring of 3 PBOs once per size in `read_fb_start`/`read_fb_finish`, instead of calling `glBufferData` per read.~~ Done in `4a7768d`. Measure it in B1.
2. Use `glMapBufferRange(READ_BIT)` when it is available.
3. In `wasm_gl_imports.py`, add `glMapBufferRange`/`glFlushMappedBufferRange` special wrappers. They copy only the mapped range into module memory, and only for `READ_BIT`. Make `glMapBuffer` on ES route through them.
4. Move `unpremultiply` to a shader pass on the GPU when the consumer accepts it. Otherwise keep the CPU path but run it on a worker.
5. **(rev 3, replaces "leave the sync path as is") Remove the synchronous field readback** (fix-ts Phase 2). First check whether the field consumes the 1x frame at all: `cap3d` is computed at `demo/eidos_boot.c:1406`, and the read runs with `async=0` at `:1415-1416` because `main3d && graphics`. If the result is unused when capture is off or unchanged, skip the read. Otherwise route it through the PBO ring (`read_fb_start`/`read_fb_finish`) instead of `malloc` + `glReadPixels` + `unpremultiply`. Keep a counter of any remaining sync reads.

**Deliver:** the code, a sync-readback counter in the report, `glread`/`unprem` before/after (step 5 is its own commit with its own attribution pair), and proof of identical pixels (VRAM capture + display).

**No-go:**
- No change to capture semantics or the 1-frame latency contract of the async path.
- No reading uninitialized PBOs on the first frame.

**Done when:** the field path does no synchronous readback, async readback does no per-call allocation, and `glread`+`unprem` on field/battle ≤ 0.5 ms average, with no visual diff.

### C3. Drop RGB readback
**Do:** find every engine `glReadPixels(..., GL_RGB, ...)`, change it to RGBA and do the conversion at the consumer. Keep the shim branch only for legacy callers.

**Deliver:** the code and a grep proof (0 RGB call sites).

**No-go:** no change to the screenshot file format.

**Done when:** the ES RGB shim never runs (assert it under `EIDOS_GPU3D_REPORT`).

### C4. Buffer streaming and invalidation
Same work as fix-ts Phase 3.5.

**Do:**
1. For per-frame vertex and index data, orphan the buffer (`glBufferData(NULL)`) and then `glBufferSubData`, or map with `INVALIDATE_RANGE|UNSYNCHRONIZED` into a ring.
2. After the MSAA resolve and the final composite, call `glInvalidateFramebuffer` on depth/stencil/ID (optional in `GL_FUNCS`; ES 3.0+ or GL 4.3).
3. Put the `glGetError` calls at `plat_gl3d.c:2004,2032` behind `EIDOS_GL_CHECK` (default on in debug builds, off in release).

**Deliver:** the code and `gldraw` before/after.

**No-go:** no removing error checks from init, only from per-frame paths.

**Done when:** `gldraw` improves or stays the same, with no visual diffs in `test_gpu3d`.

### C5. Present path and driver threading
**Do:** build variants and measure them on device at 2x:
- (a) swap interval 0 + pacer (today)
- (b) swap interval 1, with the pacer slaving to vsync and dropping its own sleep under `__SWITCH__`
- (c) `MESA_SWITCH_GLTHREAD=1`
- (d) NWindow buffer count 3, if the EGL attrib or env allows it (check `mesa:docs/switch-opengl.rst`)

Measure `present`, `wait`, p99 and audio sync (no underruns in the log). Commit the best combination as the Switch default in `host/`.

**Deliver:** a 4-variant table, the default commit, and any pacer change behind `__SWITCH__`.

**No-go:**
- No changing the 59.8261 Hz emulation timebase.
- No audio resampling hacks in this slice.

**Done when:** the default is chosen from the table and p99 does not regress versus (a).

### C6. AA/edge cost on GM20B
Resolves the temporary exception for `803f322` (AA off by default on Switch, §2).

**Do:**
1. With the real `GL_MAX_INTEGER_SAMPLES` (8 on both Mesa builds, logs/01–11), measure 4x MSAA+R8UI against the 2× supersample fallback (`plat_gl3d.c:2007-2035`) at 2x handheld in a scene with the AA bit set.
2. Add a selection rule. If the GL/ES context reports `integer_samples >= 4` and the measured cost is lower, use MSAA. Keep it overridable with `EIDOS_AA=msaa|ssaa|off`, and map the existing `EIDOS_MSAA` onto it.
3. Replace the Switch "off" default from `803f322` with the chosen path. Off stays opt-in only.

**Deliver:** a numbers table and the selection code.

**No-go:** no defaulting to `off`. The `803f322` default is the one tolerated exception, and only until this slice is done.

**Done when:** the cheaper correct path (MSAA or SSAA) is the default on Switch, and desktop stays unchanged.

### C7. Texture hits are zero-copy (fix-ts 3.1)
**Do:**
1. In `hw3d.c:626-708`, stop the texel + palette `malloc`, the copy and the FNV hash on a store **hit**. Reuse scratch buffers, and skip the copy and hash when the key inputs are unchanged.
2. Make it one commit, with one before/after attribution pair (field + battle).

**Deliver:** the code, `texdec` before/after, and a per-frame malloc count (field) before/after.

**No-go:**
- No change to texture cache keys or decode results. The C1 "hash semantics" rule still holds: same inputs give the same key and the same texels.
- No Switch-only path; this is shared engine code.

**Done when:** a field steady state does no texture-path mallocs, `texdec` drops or stays the same, and `test_gpu3d` shows no diffs.

---

## Phase D — Eliminate CPU 3D fallback frames (shared code)

**Each D slice must:**
- add a `tests/test_gpu3d.c` case: GPU vs software at 256x192, inside the existing tolerance
- update `g3d_hw_gpu_features` only after that test passes
- show the B2 census bit going to 0 on device

### D1. Rear-plane bitmap (DISP3DCNT 0x4000, `G3D_SNAP_REARBMP`)
**Do:**
1. Carry the clear color/alpha/fog bitmap and the clear-depth bitmap in the snapshot. Match the CPU semantics in `hw3d.c:836-863`, including the scroll offsets.
2. Upload them as textures through the C1 pool.
3. Replace the clear with a full-screen pass that writes color, the fog flag/ID and depth (`gl_FragDepth`, with the same 15-bit → depth conversion as the CPU).
4. Set `G3D_SNAP_REARBMP` in `g3d_hw_gpu_features` when the pass compiled.

**Deliver:** the code, a test case, and a before/after screenshot of a rear-bitmap scene.

**No-go:**
- No changes to the CPU rasterizer behavior.
- No approximating depth with a constant.

**Done when:** the test passes and the census REARBMP count is 0.

### D2. Shadow polygons (`G3D_SNAP_SHADOW`)
**Do:**
1. Implement the DS rule using the existing stencil layout (`0x40|ID`, `plat_gl3d.c:1874-1877`).
   - Shadow polygons with ID 0 (the mask) write a stencil "shadow" bit where the depth test fails, with no color write.
   - Shadow polygons with ID ≠ 0 draw color where the mask bit is set and the stored polygon ID ≠ the shadow ID, then clear the bit.
2. Remove the `mode==SHADOW` skip in `draw_run` (`plat_gl3d.c:1920-1923`).
3. Keep the draw order identical to the CPU path.
4. Stop setting `G3D_HW_UNSUP_SHADOW` in `hw3d.c:1206-1207,2029` only when the GPU path is active.

**Deliver:**
- the code
- a test case covering mask + shadow + same-ID reject
- a screenshot of a scene with character shadows

**No-go:** no reallocating stencil bits already used by the translucent-ID reject without updating that logic and its test.

**Done when:** the test passes and the census SHADOW count is 0.

### D3. Docs
**Do:** fix `plat_gl3d.c:42-43` and `docs/graphics-backends.md:37-41` to list what still falls back after D1/D2.

**Deliver:** the docs commit.

**No-go:** no perf claims without a Results row behind them.

**Done when:** the docs match the code.

---

## Phase E — Game-thread CPU budget

**Each E slice must** produce a device row for `step`/`geom`/`main`/`sub` at 2x handheld, before and after.

### E1. Guard pages work on hardware
Partial, unmeasured: `b37bbe9` (virtmem experiment), `938e53f` (stack-region reservation). Parked (fix-ts L6) until `baseline.md` orders it.

**Do:**
1. Instrument `host/mem_switch_guard.c` to log the address-space info (`svcGetInfo` for alias/heap/ASLR regions) and free memory before the reservation.
2. Try, in order, building one variant each:
   - (a) reserve before `SDL_Init`/`eglInitialize`
   - (b) map with `svcMapPhysicalMemory` in the alias region
   - (c) reduce the reservation to module max + guard (e.g. 1 GiB + 64 KiB rather than 256 MiB chunks), and check what size `svcMapMemory` actually rejects
   - (d) a smaller `__nx_heap_size` so the heap doesn't consume the region
3. Keep the fixed-block fallback.

**Deliver:**
- the variant table with each result
- the working variant committed and on by default in `switch/build.py`
- the bounds-check build kept as `--no-guard-pages`

**No-go:**
- No disabling bounds checks without guard pages. Memory safety is mandatory.
- No applet-mode support.

**Done when:** logs show "guard pages active" and `step`+`geom` improve on device.

### E2. Memory budget with mesa-switch
**Do:**
1. Log the free memory after `eglInitialize`, after the first GL frame, and at peak in battle.
2. Sweep `EIDOS_SWITCH_MEM_MB` and `__nx_heap_size` to find the smallest budget that passes a 30-minute play session.
3. Set the defaults with ≥ 128 MiB headroom.

**Deliver:** the sweep table and the default commit.

**No-go:** no budget below the S0 measured module peak + 25%.

**Done when:** the soak passes with the new defaults.

### E3. fpcast-emu measurement
**Do:**
1. Build `EIDOS_HOST_FPCAST=OFF` as a diagnostic only.
2. Measure the delta on the device for `step`/`geom`.
3. Write `switch-mesa/fpcast.md` with the numbers and a cross-platform proposal if the gain is ≥ 15%.

**Deliver:** the doc.

**No-go:**
- No shipping the OFF build.
- No Switch-only lp64 change.

**Done when:** the doc exists and the maintainer has it.

### E4. Core placement
Step 1 landed early (`c323c54`, unmeasured, no soak). What's left is the measurement and the 30-minute soak.

**Do:**
1. Pin the game thread to core 0, the workers to cores 1 and 2, and the glthread (if C5 enabled it) to whichever core the profile shows as least loaded.
2. Run CPU boost (`appletSetCpuBoostMode`) only during extraction/loading.

**Deliver:** a per-variant table and the default commit.

**No-go:**
- No core 3.
- No boost during gameplay.
- No overclock.

**Done when:** p99 improves or stays the same and nothing deadlocks in the 30-minute soak.

### E5. GPU 2D compose
**Do:**
1. Check that the `plat_gl2d.c` usampler shaders compile on the chosen backend. Log the compile result.
2. Enable `EIDOS_OPT_GPU_2D` on Switch.
3. Test field, battle, menus and a capture scene against the CPU 2D path (`EIDOS_G2D_GPU_CHECK`).

**Deliver:** the compile log, the diff-check results and `main`/`sub` before/after.

**No-go:** no enabling it if `EIDOS_G2D_GPU_CHECK` reports any diff.

**Done when:** it is on by default with zero diffs, or the diffs are written up for later.

### E6. GPU-path CPU waste (fix-ts 3.2–3.4, Phase 4 heap lock)
**Do**, in order, as one commit each with one before/after attribution pair:
1. **Geom-sort gate.** `g3d_geom_sort` (`raster.c:780-829`) mallocs twice, scans and `qsort`s every frame before the GPU-hit skip. Run it only on the CPU raster path.
2. **CPU clear gate.** `clear_planes` + `g3d_target_clear` write ~0.9 MB in 2 full scans per frame. Run them only for the CPU fallback.
3. **Snapshot pool.** Replace the per-frame snapshot memcpy/regrow (`hw3d.c:1025-1036`) with pooled, versioned snapshot buffers.
4. **Heap lock, measure first.** Only if `step`/`geom` still show cross-thread blocking after 1–3 and C7: measure contention on the shared module heap lock (game thread + 2 workers), then decide. No preemptive sharding.

**Deliver:** the code, `geom`/`raster`/`snap` before/after per item, and for item 4 a measurement note (and code only if the numbers justify it).

**No-go:**
- No behavior change on the CPU fallback path. A frame the GPU can't draw must still get sort + clear.
- No Switch-only forks of engine logic.
- No heap-allocator change without a measurement that names it.

**Done when:** `geom`+`raster`+`snap`+`texdec` are flat across menu/field up to polygon count, per-frame mallocs are gone from the field path (with C7), and `test_gpu3d`/`test_g3d` show no diffs.

---

## Phase S — Support slices (rev 3)

### S0. Paperwork
Rev 3 of this plan, the Results backfill from logs/00–11 in `plan:`, and L5 (profile capture) plus the local-fork Mesa pin in `switch/README.md`. Done 2026-11-02 on branch `switch/mesa-s0`.

### S1. Settings and nvram persistence on sdmc (fix-ts L1)
**Do:**
1. Find why `sdmc:/switch/eidos/profile/settings.ini` can't be replaced (logs/07, 09) and why `nvram: cannot write the firmware flash (…/saves/firmware.bin)` appears in every log. Both are sdmc write failures, probably the same family (rename-over or directory creation).
2. Fix it in `host/` (or in shared code if the cause is shared, with desktop unchanged).
3. Write a checklist that toggles outlines, saves in game, reboots, and checks both survive.

**Deliver:** the fix commit, the checklist, and a log with neither message.

**No-go:** no Switch-only fork of save logic in engine code. No silent fallback to in-memory settings.

**Done when:** a setting and an in-game save survive a reboot on hardware. **F2 depends on this** (its step 2 needs persistence).

### S2. `tests/test_mod_loader.c` on wasi-sdk 25.0 (fix-ts L2)
**Do:** make the test build on the pinned wasi-sdk 25.0 (witx `static_assert` alignment failures). Fix the test, or the pin if the pin is wrong, and say which.

**Deliver:** the commit and a green desktop `ctest` log.

**No-go:** no deleting or skipping the test. No changes to the stage-one wasm ABI.

**Done when:** desktop `ctest` is green. **Blocking:** no C, D or E slice lands before it.

### Housekeeping
- L3: delete `switch/diag-fog-toggle` (fog is exonerated). As of rev 3 the branch is not in the eidos repo's branch list, so check whether it already exists anywhere before acting.
- L6: guard pages stay parked until `baseline.md` orders E1.

---

## Phase F — 2x handheld default and ship

### F1. Default to GL
Step 1 landed early (`c8249d3`). Steps 2–3 and the forced-failure test are still owed.

**Do:**
1. ~~In `host/main.c`, default to `gl` on Switch.~~ Done (`host/main.c:407-409`).
2. If context creation or `gl3d` init fails, automatically recreate the window with the SDL renderer and log why.
3. Keep `EIDOS_RENDERER=sdl` as an override.

**Deliver:** the commit and a forced-failure test (`EIDOS_GL_FAIL=1`) showing the fallback works.

**No-go:** no removing the SDL path.

**Done when:** a normal boot uses gl, and a forced failure boots via sdl.

### F2. 2x handheld default
Requires S1 (settings persist).

**Do:**
1. Change `host/main.c:442-444` to pass `--resolution 2` on Switch in handheld mode. Docked uses 2x until P3 is decided.
2. Make sure the in-game setting still overrides it and persists.
3. Make sure dock/undock keeps the internal resolution and only rescales the output.

**Deliver:** the commit and a dock/undock test in the F3 checklist.

**No-go:**
- No auto-dropping to 1x on load without a documented dynamic-resolution option, which defaults to off.

**Done when:** a fresh install starts at 2x handheld.

### F3. Acceptance (P1, P2, P3)
**Do:**
1. Write `switch-mesa/checklists/F3.md`. It covers:
   - P1 runs
   - P2 runs
   - P2 30-minute soak in the field, with the fps series logged and thermals noted if available
   - P3 docked 2x
   - dock/undock ×5 mid-battle
   - HOME → resume
   - quit-and-continue save check
2. Stop for the human run.
3. Grade every row pass/fail against §1.

**Deliver:** the Results rows and a pass/fail summary.

**No-go:** no declaring P2 passed on average fps alone; p99 and the soak are required.

**Done when:** P1 and P2 pass. Otherwise loop back to the slice that owns the biggest remaining bucket.

### F4. Release
**Do:**
1. Update `switch/README.md` (pins, env options, defaults), `docs/graphics-backends.md` and the CHANGELOG.
2. Add a CI job that builds the NRO against the pinned mesa-switch prefix inside the pinned devkitPro image, **when given the local fork as source or prefix**. The pin is not fetchable from any remote, so the job takes either `EIDOS_MESA_SWITCH_SOURCE` (a checkout of the local fork at `16732f6`) or a prebuilt prefix via `--mesa-prefix`, and fails clearly if given neither. It only builds; it does not run on a device.

**Deliver:** the commits and a green CI run (on a runner given the local fork or prefix).

**No-go:**
- No release tagging.
- No uploading the NRO anywhere without the maintainer's approval.

**Done when:** CI is green and the docs match the shipped defaults.

---

## 3. Dependency graph

```
A1 → A2 → A3 → A4 → A5 (P0 FAIL) → A5b (= fix-ts Phase 1, + owed Zink run) → A6
S0 (paperwork) ─┐
S2 (ctest green)┴→ B1 (after A5b) → B2, B3
B3 → { C2' (C2 step 5 first) → E6/C7 → C4, C1, C3 → C6 → C5
     | D1 → D2 → D3
     | E1, E2, E3, E4 (soak-verify), E5 }               (order by baseline.md)
S1 (persistence) ─┐
C*, D*, E* → F1 → F2 → F3 (P1, P2 gate) → F4 (CI given the local fork)
```
Done: A1–A4, S0. The C/D/E order shown is the expected one from fix-ts's code-cited attribution. `baseline.md` confirms or reorders it.

**Hardware batches** (one human session each): #1 = A5b checklist (EDGE variants, Zink, one attribution run). #2 = B1 matrix. After that, 3–4 slices' before/after pairs per session.

**Parallelism:**
- C, D and E can run at the same time on separate worktrees.
- C1/C2/C4 and D1/D2 all touch `plat_gl3d.c`. Land C1 first because D1 uses its pool, then rebase the others.
- Hardware runs are serialized through the human. Batch the checklists so one session covers several slices.

## 4. Open questions for the maintainer

1. Do you want the GL-only SDK, or the unified SDK (Vulkan+GL)? This plan assumes GL-only. Zink is tested only as a backend alternative. *(open)*
2. ~~Which field and battle scenes are the canonical ones?~~ **Answered 2026-11-02:** a Mistralton Gym save with a few trainers left (field/gym walking + trainer battles), and a second save right before the gym leader fight. See §1. Paths: TBD (being located). Leader: Skyla (confirmed).
3. P3 (docked 2x): commit to it after P2, or leave it as a stretch goal? *(open)*
4. Is a dynamic-resolution option (2x → 1x under load) acceptable as an opt-in fallback if P2 misses? *(open)*
5. ~~Where does the Mesa `perf/*` stack live so the pin is reproducible?~~ **Answered:** in our personal local fork only (`mesa:` `perf/switch-integration` @ `16732f6`). Nothing is pushed upstream, and no further Mesa changes are made. F4 builds when given the fork.
6. ~~Accept AA off by default (`803f322`)?~~ **Answered:** yes, temporarily, until C6 picks MSAA or SSAA.

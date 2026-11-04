# A5b hardware checklist: edge pass black on NVC0 (hardware batch #1)

2026-11-02. Implements `implementation-plan.md` rev 3 A5b step 1 (= fix-ts Phase 1a + the owed A5 run (b) + one attribution run). Protocol: implementation-plan §2. **Batch run 2026-11-02/03; results in §5 and switch-plan.md Results.**

Build under test: `out/eidos.nro` (eidos `e229250`, mesa-switch `16732f6`). Every run uses this NRO, unchanged. **No rebuilds inside this batch.**

## 0. Who does what

| Tag | Meaning |
|---|---|
| **[AGENT]** | Driven by the agent through the console MCP: writes `sdmc:/switch/eidos/env.txt` and `args.txt`, reads `stderr.txt`/`boot.txt` back, takes screenshots, sends HID input (walking, menus), copies files into `logs/<NN>/`. |
| **[HUMAN]** | Done by the person at the console: launching **title mode** (hold **R** while starting a retail title, then open hbmenu and pick `eidos.nro`), running `nxlink` on the PC, and **judging visuals** (world rendered/white/black, outlines present). The agent never decides a visual verdict alone. |

The agent writes the files and then stops and asks the human to launch (§2 no-go). After each run the agent collects the files and fills in the results table (§5) from the logs and the human's verdict.

## 1. Fixed conditions (every run)

- **Save / spot:** `BENCH SPOT TBD — Mistralton gym field, from pre-Skyla save`. Same save, same standing spot, same facing for all runs. Load the save, walk to the spot, **stand still** (no in-game menus).
- **No in-game toggling.** Don't open Settings, and don't touch the outlines option. Outlines are controlled by `EIDOS_EDGE` in env only. The in-game setting doesn't persist (S1), so each boot starts at the setting default.
- **Entry conditions** (a run that fails these isn't a data point; redo it): `boot.txt` contains `title mode`, and the `EIDOS_GPU3D_REPORT` line shows unsupported `0x0`.
- **Timing:** from the moment the field is on screen at the spot, stand for **≥ 120 s** (so the step-down seen in logs/11 falls inside the window, see §4), then take the scene screenshot again and quit via HOME → close.
- **Screenshots** [AGENT]: `title.jpg` (title screen), `scene_t0.jpg` (at the spot, first seconds), `scene_t120.jpg` (at ~120 s). The human confirms what each shows.
- **Clean slate per run** [AGENT]: before every run, overwrite both `env.txt` and `args.txt` (write an empty `args.txt` when the run has no args), and delete the old `stderr.txt`/`boot.txt` on sdmc so stale files can't be mistaken for new ones.

### How the host reads these files (verified in source)
- `sdmc:/switch/eidos/env.txt`: one `NAME=value` per line, `#` lines ignored, values via `setenv` (`host/main.c` `mobile_args`, ~455-465).
- `sdmc:/switch/eidos/args.txt`: **one argv entry per line**, appended after the built-in `--resolution 1` etc. (`host/main.c` ~466-475).
- `stderr.txt` and `boot.txt` are written next to them in `sdmc:/switch/eidos/`.
- `EIDOS_NXLINK` (any value) calls `nxlinkStdio()` right after `host: stderr ok` (`host/main.c:597-598`). libnx's `nxlinkStdio()` redirects **both stdout and stderr** to the nxlink socket. So with it set, `stderr.txt` holds only the lines before the redirect, and the fps series, the renderer report and the GPU3D report all go to `nxlink -s`. **That is why runs 1–10 below leave `EIDOS_NXLINK` out** (a deliberate deviation from the §2 standard capture, so that `stderr.txt` keeps the data). Only run 11 uses it.
- `EIDOS_NOWAIT`: any value other than empty or `0` disables frame pacing (`engine/platform/platform.c:677-689`).
- `EIDOS_FPS_LOG`: one `eidos: N fps` line per second (`host/sdl.c:488+`).
- `MESA_SWITCH_GL_DRIVER`: read by mesa `src/egl/drivers/switch/egl_switch.c:1345`. An unknown value logs `egl-switch: unknown MESA_SWITCH_GL_DRIVER=…` and falls back.

## 2. What `EIDOS_EDGE` does (from `engine/platform/plat_gl3d.c`)

Outlines are drawn only when all of these hold (`plat_gl3d.c:1855-1856`): the game enables edge marking (`DISP3DCNT` bit `0x20`), the GPU supports it (`pedge`), the setting's `edge_mode != 0`, and `EIDOS_EDGE` does not start with `0`. `edge_mode` starts at 0 (`plat_gl.c:523`) and is set from the settings model's "3D outlines" option (`settings.c:83`: choices off/thin; the default looks like **1 = thin**). With `edge_mode == 1` the variant defaults to 1, and `eidos_gl_set_edges(2)` picks 3 (`plat_gl3d.c:1413`). `EIDOS_EDGE=<0-7>` overrides the variant (first character only, `:1417-1421`). `EIDOS_EDGE_WIDTH` isn't used in this batch.

| `EIDOS_EDGE` | Variant (comment at `plat_gl3d.c:174-189`) |
|---|---|
| unset | setting default → variant 1 when outlines = thin |
| 0 | no edge pass at all (the known "world renders" config, logs/09) |
| 1 | thin: a pixel touching a farther pixel of another ID, one window pixel wide (melonDS rule at scale 1) |
| 2 | thin behind: the farther pixel of the pair, drawn in the nearer one's colour (DeSmuME rule) |
| 3 | one DS pixel: variant 1 widened by a disc inside its own polygon |
| 4 | the first rule: four rays of one DS pixel (spikes/combs) |
| 5 | half a DS pixel: variant 3 at half width (`step = (step+1)/2`) |
| 6 | one DS pixel behind: variant 2 widened outward by a disc |
| 7 | thin, half strength (melonDS with AA on) |

All of variants 1–7 go through the same pass setup (fullscreen `gl_VertexID` triangle `k_vse`, `usampler2D u_id` `texelFetch` of R8UI, `u_dep`, `edge_pass` FBO reattach). Only the `k_fse` branch differs. So a split between variants points into the shader, and all-black points at setup (fix-ts 1a "Reading").

## 3. Runs

Directories continue after the highest existing `logs/` dir (`logs/11`). Base env lines used below:

```
EIDOS_FPS_LOG=1
EIDOS_RENDERER_REPORT=1
EIDOS_GPU3D_REPORT=1
EIDOS_RENDERER=gl
```
(written "BASE" below, always as these exact four lines first).

Files collected for **every** run into `logs/<NN>/` [AGENT]: `env.txt`, `args.txt` (byte-exact copies of what was on sdmc), `boot.txt`, `stderr.txt`, `title.jpg`, `scene_t0.jpg`, `scene_t120.jpg`, plus `notes.txt` (the human's visual verdict, the time the field appeared, anything odd).

### Run 1 → `logs/12/`: control, outlines OFF
env.txt:
```
EIDOS_FPS_LOG=1
EIDOS_RENDERER_REPORT=1
EIDOS_GPU3D_REPORT=1
EIDOS_RENDERER=gl
EIDOS_EDGE=0
```
args.txt: empty file.
Expect: the world renders (reproduces logs/09). **If this run is black, stop the batch**: the baseline has moved and nothing after it can be read.

### Run 2 → `logs/13/`: control, outlines ON (default)
env.txt: BASE only (no `EIDOS_EDGE` line).
args.txt: empty.
Expect: black field (reproduces logs/06–08). Record exactly what the human sees: black, white, partial, or correct.

### Runs 3–9 → `logs/14/` … `logs/20/`: edge variants
env.txt: BASE + one line `EIDOS_EDGE=<n>`. args.txt: empty.

| Run | Dir | Extra env line |
|---|---|---|
| 3 | `logs/14/` | `EIDOS_EDGE=1` |
| 4 | `logs/15/` | `EIDOS_EDGE=2` |
| 5 | `logs/16/` | `EIDOS_EDGE=3` |
| 6 | `logs/17/` | `EIDOS_EDGE=4` |
| 7 | `logs/18/` | `EIDOS_EDGE=5` |
| 8 | `logs/19/` | `EIDOS_EDGE=6` |
| 9 | `logs/20/` | `EIDOS_EDGE=7` |

Run 3 should match run 2, since both are variant 1. If they differ, note it: the settings default isn't what §2 assumes.

### Run 10 → `logs/21/`: owed A5 run (b), Zink, outlines ON
env.txt:
```
EIDOS_FPS_LOG=1
EIDOS_RENDERER_REPORT=1
EIDOS_GPU3D_REPORT=1
EIDOS_RENDERER=gl
MESA_SWITCH_GL_DRIVER=zink
```
args.txt: empty.
Extra checks [AGENT, from `stderr.txt`]: the renderer string must show a Zink renderer (not `NV12B`), and there must be no `egl-switch: unknown MESA_SWITCH_GL_DRIVER` line. If the string still says NV12B, or EGL fails to come up, the run is **void for the Zink question**: record "Zink not available in this build" and continue.
Reading: Zink draws outlines correctly ⇒ the bug is likely NVC0-side (record it for B3). Zink is also black ⇒ suspect eidos' pass setup.

### Run 11 → `logs/22/`: attribution ("before" for everything)
env.txt:
```
EIDOS_NXLINK=1
EIDOS_FPS_LOG=1
EIDOS_RENDERER_REPORT=1
EIDOS_GPU3D_REPORT=1
EIDOS_RENDERER=gl
EIDOS_NOWAIT=1
EIDOS_EDGE=0
```
args.txt (exactly two lines):
```
--profile
300
```
`EIDOS_EDGE=0` follows implementation-plan A5b (so the run is comparable to logs/11 and renders the world).
[HUMAN] On the PC, start the capture **before** launching, and keep everything the NRO prints:
```
nxlink -s <path>/eidos.nro -a <switch-ip> 2>&1 | tee logs/22/stdout.txt
```
(Or, if the NRO is launched from hbmenu instead of being sent, run `nxlink -s` as a listener. See uncertainty U2.) Because of `EIDOS_NXLINK`, `stdout.txt` will contain the fps lines, the renderer/GPU3D reports **and** the `prof frame …` lines, while `stderr.txt` will be near-empty. That's expected.
Files: as every run, plus `stdout.txt`. **A profile run without `stdout.txt` containing `prof frame` lines is void** (§2): redo it.

## 4. What to observe and record per run

| Observation | Source | Who |
|---|---|---|
| Title-mode boot (`title mode` line) | `boot.txt` | AGENT |
| GL renderer string; GPU3D features and unsupported mask (`0x0` required) | `stderr.txt` (run 11: `stdout.txt`) | AGENT |
| World at the spot: **rendered / white / black / partial**; outlines visible? where? | screen, `scene_t0.jpg`, `scene_t120.jpg` | HUMAN judges, AGENT screenshots |
| Did the picture change over time (e.g. turned black after N s)? | screen + both scene shots | HUMAN |
| fps series: title, field plateau, and the step-down | `eidos: N fps` lines, one per second | AGENT |
| The step-down: in logs/11 (NOWAIT, EDGE=0) the field plateau is ~49 fps from `stderr.txt` line ~35 to line 88, then drops to ~18–21 fps at line 89, i.e. roughly 82–85 s into the fps series. Record, per run, the second at which the plateau ends, the before/after fps, and whether it recovers | fps series | AGENT |
| Any GL/EGL errors, `unsupported` changes, `nvram`/`settings.ini` write errors (expected, S1) | `stderr.txt` | AGENT |
| Run 11: the full `prof frame …` bucket lines (`step g3d main2d sub2d present audio wait geom raster snap texdec gldraw glup render boot3d glread unprem`) | `stdout.txt` | AGENT |

Runs 1–10 are paced (no NOWAIT), so their plateau will sit near 60 at best. Compare step-down **timing** across runs, not absolute fps against logs/11.

## 5. Results table (agent fills in after the batch)

| Run | Dir | Config | Entry OK | World (human) | Outlines | fps plateau → after step (at s) | Notes |
|---|---|---|---|---|---|---|---|
| 1 | 12 | EDGE=0 | yes | rendered | n/a | ≈7 steady, no step | |
| 2 | 13 | default (on) | yes | black | none | ≈4 steady, no step | |
| 3 | 14 | EDGE=1 | yes | black | none | 4 steady, no step | = run 2 |
| 4 | 15 | EDGE=2 | yes | black | none | 13 steady | launched by hand; no title.jpg |
| 5 | 16 | EDGE=3 | yes | black | none | 13–14 steady | |
| 6 | 17 | EDGE=4 | yes | black | none | 13–14 steady | |
| 7 | 18 | EDGE=5 | yes | black | none | 13–14 steady | |
| 8 | 19 | EDGE=6 | yes | black | none | 12 steady | t0 caught season card |
| 9 | 20 | EDGE=7 | yes | black | none | 13–14 steady | |
| 10 | 21 | Zink, on | n/a | void | n/a | none | Zink EGL init fails → headless |
| 11 | 22 | profile, NOWAIT, EDGE=0 | yes | rendered | n/a | 15–16 steady (62 ms/frame) | prof buckets → B1; hbmenu netloader |

## 6. Pass/fail for this batch

This batch does **not** close A5b by itself. A5b's Done-when is "desktop tests pass and hardware renders the field correctly with outlines **on**, at any fps", which needs steps 3–4 (root cause, fix, test) and a later confirmation run. This batch passes when:

- **Valid:** runs 1–11 all meet the entry conditions, and every dir has the files listed in §3 (run 11 including `stdout.txt` with `prof frame` lines).
- **Controls hold:** run 1 renders the world, and run 2 shows the defect. If run 2 renders correctly, the defect didn't reproduce: stop and report (A5b may already be met on this tree; the human confirms with the outlines visible).
- **1a done** (fix-ts): runs 3–9 either split (some variants render ⇒ the bug is in a `k_fse` branch) or are all black (⇒ pass setup). Either outcome is a pass for 1a. "Partial" counts as a split; describe it.
- **Zink recorded:** run 10 gives renders/black/void-with-reason.
- **Attribution recorded:** run 11 gives the fps series plus prof buckets.

**Fail:** any missing file, any entry-condition failure not redone, or a visual verdict missing from the human. Per §2, don't move on to A5b step 3 past a fail.

## 7. Open uncertainties

- **U1:** the default of the "3D outlines" setting is inferred from `settings.c:83` (`…, 0, 1, 1, …` read as min 0 / max 1 / default 1). If the default is actually off, run 2 = no outlines and runs 3–9 still draw nothing (`edge_mode == 0` gates the pass regardless of `EIDOS_EDGE`). Run 2 vs run 1 shows which: if both render and run 2 has no outlines, the env-only variant runs are meaningless without a code/setting change. Stop and ask.
- **U2:** `nxlinkStdio()` connects to the host recorded when the NRO was **sent by nxlink**. A plain hbmenu launch may have no host address, in which case nothing reaches the PC (and stderr is still redirected). The safest path: the human starts title-mode hbmenu (R held), then sends the NRO from the PC with `nxlink -s … eidos.nro`. Check that `args.txt` is still appended in that launch mode (it is read from sdmc regardless; nxlink's own argv are replaced by `mobile_args`).
- **U3:** whether this NRO's mesa-switch prefix includes Zink. Run 10's renderer string decides it.
- **U4:** with `EIDOS_NXLINK=1` set, any output before the redirect still lands in `stderr.txt`. Everything after it goes only to the socket. This matches the README ("sends stderr to `nxlink -s` instead of `stderr.txt`"). It implies logs/11, whose fps series is in `stderr.txt`, ran without `EIDOS_NXLINK`. Its env wasn't stored, so this can't be confirmed.
- **U5:** spot/save path is a placeholder; fill in before the batch.

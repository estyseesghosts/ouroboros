# Recovery plan: fold fix-ts.md back into the implementation plan

2026-11-02. Read with `implementation-plan.md` (rev 2) and `../fix-ts.md`.
This file decides how the two fit together. Wherever they disagree, this
file wins until implementation-plan rev 3 absorbs it.

## 1. Where we actually are

eidos `perf/switch-integration` @ `e229250` (clean). mesa-switch
`perf/switch-integration` @ `16732f6`. NRO in `out/eidos.nro`.

| Slice | State | Evidence / gap |
|---|---|---|
| A1–A4 | **done** | `sdk-build.md`, branches `switch/mesa-a2..a4`, `8a0e187` |
| A5 | **ran, P0 FAILED, never recorded** | Title renders; field is black with outlines on (logs/06–09). Zink run (b) **never done** (no log mentions zink). The old-Mesa baseline (c) exists as logs/00–02. `logs/A5/` is empty and the Results table is empty |
| A5b | **half done, informally** | `EIDOS_EDGE=0` renders the world (logs/09), so the edge pass is the defect. This is fix-ts Phase 1 |
| A6 | not started | |
| B1–B3 | **not started** | No usable `--profile` data: logs/10 died on `--profile 300` written on one line, and logs/11 never captured stdout |
| C2 | partial, unmeasured | `4a7768d` PBO ring. The field path still does a synchronous read (fix-ts §0) |
| C6 | **violates a no-go** | `803f322` turns AA off by default on Switch. C6 and the global rules forbid defaulting to off |
| E1 | partial, unmeasured | `938e53f`, `b37bbe9` guard-page experiment |
| E4 | landed, unmeasured | `c323c54` core pinning, no soak |
| F1 | partial | `c8249d3` defaults to gl, but there is **no SDL fallback** and no `EIDOS_GL_FAIL` test |
| (none) | out of plan | `cd51064`, `3f84f9f` (3D draw restructure). mesa `perf/*` branches (submit, tex-upload, zero-bufs, no-compression, staging-cached) |

Process debt: optimization landed before `baseline.md` existed (breaks the B1
no-go). Branch names don't follow `switch/mesa-<slice>` / `eidos/<slice>`.
No hand-off notes. logs/04–11 have no stored `env.txt`/`args.txt`.

**The Mesa pin can't be reproduced.** Neither `16732f6` nor the old
`0ff52079` is on any branch of `origin` (danfromtico/mesa-switch). The
pinned SDK carries local, unpushed Mesa source changes. A2 ("pin the SHA")
and F4 (CI builds against the pinned prefix) can't be met as things stand.

## 2. Overlap verdict

fix-ts.md is not separate work. It re-scopes part of the implementation
plan around what was measured. Mapping:

| fix-ts | Implementation plan | Relationship |
|---|---|---|
| §1 measurement protocol | §2 standard capture, B1 | **Correction**: profile goes to stdout (needs `nxlink -s`), `args.txt` takes one argv per line, store env/args per run. The plan's B1 parser assumes `stderr.txt`, which is wrong |
| Phase 1 (edge black) | A5b | Same slice, narrower and better aimed. Adopt it as A5b |
| Phase 2 (sync field readback) | C2 step 5 | **Conflict**: the plan says leave the sync capture path alone, fix-ts says remove it. Adopt fix-ts and keep C2's no-gos. Amend C2 |
| Phase 3.1 (texture hit zero-copy) | C1 no-go "no change to hash semantics in hw3d.c" | Compatible if the keys and results are unchanged. Make it a new slice, C7 |
| Phase 3.2–3.4 (geom sort, CPU clears, snapshot pool) | none | **New work**. Add it as E6 "GPU-path CPU waste" |
| Phase 3.5 (vertex orphaning, glGetError gate) | C4 steps 1 and 3 | Identical. Do it as C4 |
| Phase 4 (TexSubImage pool / heap lock / unprem) | C1 / new / C2.4 | Same as the plan, except the heap lock, which goes into E6 measure-first |
| Phase 5 (2x ship gate) | F2 + F3 (P1, P2) | A subset. The plan adds P3, dock/undock, save check, F1 fallback, F4 CI |
| L1 settings don't persist | F2 step 2 needs persistence | **Promote to a required slice** (S1). The same family as logs/11's `nvram: cannot write firmware.bin`, which is a sdmc write failure |
| L2 test_mod_loader red | global rule "run the existing test suite" | **Blocking**. Fix it first (S2) |
| L3 diag-fog-toggle | none | Delete the branch. Fog is exonerated |
| L5 profile capture doc | B1 | Do it in S0 |
| L6 guard pages parked | E1 | Let `baseline.md` order it. Not dropped |

fix-ts leaves out: the Zink run, A6, all of B, C3, C5, C6, D1–D3, E2,
E3, E5, F1 fallback and F4. These all stay in scope. fix-ts's
"no Mesa changes" rule is adopted: Mesa stays frozen at `16732f6` from
here on, apart from the publishing question in S3.

## 3. Course of action

Hardware runs are batched so each human session covers several slices.

### Step 0. Paperwork (agent, no hardware)
- **S0** Update `implementation-plan.md` to rev 3 with the amendments above
  (§1 capture, C2 step 5, new C7/E6, S1–S3). Fill the Results table from
  logs/00–11 as far as each log supports, and mark unknown env as unknown.
  Write up A5 as "P0 FAIL: edge pass black on NVC0; zink owed".
  Document L5 in `switch/README.md`.
- **S2** Make `tests/test_mod_loader.c` build on wasi-sdk 25.0 so the
  desktop `ctest` is green. Every later C slice depends on it.
- **S3 (needs your decision)** Make the Mesa pin fetchable: push the
  `perf/*` stack to a fork you control, or get the maintainer's approval
  to push or open a PR. Then point `fetch_mesa_switch.py` at that remote.
  Until this is done, F4 is blocked.
- Delete `switch/diag-fog-toggle` (L3).

### Step 1. Hardware batch #1: reach P0 (A5b = fix-ts Phase 1)
Checklist `checklists/A5b.md` (fix-ts §1 protocol):
- `EIDOS_EDGE=1..7` field runs (fix-ts 1a)
- A5 run (b) `MESA_SWITCH_GL_DRIVER=zink` with edge **on**. If zink
  renders outlines, that is a strong hint the bug is NVC0-side, and it
  feeds B3.
- One proper attribution run (`--profile` / `300`, nxlink `-s`) on the
  current tree, as the "before" for everything.

Then fix-ts 1b/1c: name the construct, apply a minimal fix, add the
`test_gpu3d` case. If it turns out to be a driver bug, write a
`switch-mesa/repro/` NRO as A5b says and **don't** patch Mesa.
→ **P0 passes. Then A6** (the 20.1 baseline from logs/00–02 is enough).

### Step 2. Hardware batch #2: baseline (B1–B3)
Measure the tree as it now stands, with the out-of-plan perf commits.
`baseline.md` records them as "landed before baseline", so they're
measured rather than reverted. The B1 matrix uses the fixed protocol.
`switch_prof_report.py` parses the **nxlink stdout capture** plus
`stderr.txt` fps. The 10-minute gl-vs-sdl run answers the fps decay seen
in every log (logs/11 drops from ~50 to ~25 fps with NOWAIT and
EDGE=0). B2 runs the census in battle as well; the field is already 0x0.
B3 picks NVC0 or Zink and orders the rest.

### Step 3. C/D/E in `baseline.md` order
Expected order, given fix-ts's code-cited attribution (B1 confirms or
reorders it):
1. **C2'** (fix-ts Phase 2) kills the field sync readback, then finishes
   C2 (MapBufferRange, unprem).
2. **E6 / C7** (fix-ts 3.1–3.4): texture zero-copy, geom-sort gate, CPU
   clear gate, snapshot pool. One commit and one before/after attribution
   pair each.
3. **C4** (fix-ts 3.5), **C1** (pool + TexSubImage), **C3**.
4. **C6** must pick MSAA or SSAA. With 8 integer samples MSAA+R8UI is
   viable. This resolves the `803f322` no-go: off can remain an opt-in
   `EIDOS_AA=off`, never the default.
5. **C5** present variants, **E1** guard pages, **E2** memory, **E4**
   soak-verify the pinning that already landed, **E5** GPU 2D, **E3**
   fpcast doc.
6. **D1/D2/D3**, scoped by the B2 census. They're still required by the
   plan, but they can go last if the census shows them rare.

Batch the device before/after runs: about 3–4 slices per human session.

### Step 4. Ship
- **S1** Settings and nvram persistence on sdmc (L1). Verify across a
  reboot.
- **F1** Add the SDL fallback and the `EIDOS_GL_FAIL=1` test. The gl
  default already landed.
- **F2 → F3** (P1, P2, soak, P3 docked, dock/undock, HOME, save) **→ F4**
  (needs S3).

## 4. Decisions needed from you
1. **S3**: where can the mesa `perf/*` stack live so the pin is fetchable
   (your fork vs. a maintainer PR)?
2. Accept "AA off by default" as **temporary**, until C6? (Recommended:
   yes, with C6 as the fix.)
3. Plan Q2 is still open: canonical field/battle scenes and save files.
   B1 and F3 need them to be reproducible.

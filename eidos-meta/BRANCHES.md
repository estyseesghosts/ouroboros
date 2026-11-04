# eidos branch tips captured in this monorepo snapshot

Snapshot source: switch/fix-h at 9f12190 (2026-11-04), 81 commits ahead of perf/switch-integration.

All local branches below are recorded so no work is lost. The snapshot in eidos/ is switch/fix-h.

| branch | tip | date | subject |
|---|---|---|---|
| backup/integration-pre-merge | e229250 | 2026-11-02 | Merge branch 'switch/pin-mesa-integration' into perf/switch-integration |
| backup/old-integration | b9e24db | 2026-11-02 | Merge branch 'perf/async-readback' into perf/switch-integration |
| backup/pre-rebase-a5b | 5c9b215 | 2026-11-03 | gl3d: EIDOS_EDGE_DEBUG shows one edge input alone; EIDOS_EDGE_DIAG dumps the draw's raster state |
| backup/pre-rebase-p0prof | 330a4bc | 2026-11-03 | prof: the buckets add up to the frame; a game-frame rate beside the swap rate |
| backup/pre-rebase-s0 | 72f9bed | 2026-11-02 | docs: Switch profile capture and the local-fork Mesa pin |
| backup/pre-rebase-s2 | 9d74372 | 2026-11-02 | wasi: the tests that need the host are built and not run, each with its reason |
| fix/ov095-side-effect-handlers | 136c925 | 2026-11-02 | Battle: ov095's unlisted handler pointer is a code pointer the data scan finds |
| main | 000cf64 | 2026-11-02 | Save slots: New Game never writes over a file, even one the game would not load |
| perf/async-readback | 4a7768d | 2026-11-02 | switch: ring of readback PBOs, never map the frame just queued |
| perf/cpu-build | c323c54 | 2026-11-02 | switch: pin the game thread and compositor workers to cores 0-2 |
| perf/msaa-default | 803f322 | 2026-11-02 | GL 3D: anti-aliasing off by default on the Switch, EIDOS_MSAA overrides |
| perf/scene-cost | cd51064 | 2026-11-02 | GL 3D: a polygon's state travels in its vertices, not in uniforms |
| perf/single-3d-render | 3f84f9f | 2026-11-02 | GL 3D: draw the DS-size frame once, in the display's framebuffer at 1x |
| perf/switch-guard-stack | 938e53f | 2026-11-02 | switch: reserve the guard-page memory in the stack region |
| perf/switch-integration | eefcb7f | 2026-11-03 | host: the Switch keeps its own ROM picker and has no iOS view |
| switch/default-gl-renderer | c8249d3 | 2026-11-02 | switch: default to the GL renderer |
| switch/diag-want3 | e87cf8c | 2026-11-04 | fix: DIAG force want=3 for the show/pixels comparison run (throwaway) |
| switch/fix-ff-a | 77c65ed | 2026-11-04 | fix: lock the fixed -O2 module flags behind build asserts |
| switch/fix-ff-b | 376f962 | 2026-11-04 | fix: keep a bounded spare pool of 3D texture stores |
| switch/fix-ff-c | c931fdf | 2026-11-04 | fix: version GPU-frame raster and 3D windows by inputs, not pixels |
| switch/fix-ff-d | b3261da | 2026-11-04 | fix: orphan texture stores on re-spec, budget uploads per frame |
| switch/fix-ff-e | ade9f8d | 2026-11-04 | fix: drop per-frame GPU wait from present, document the split |
| switch/fix-ff-f | b3261da | 2026-11-04 | fix: orphan texture stores on re-spec, budget uploads per frame |
| switch/fix-ff-g | bb1d35d | 2026-11-04 | fix: freeze golden prof format, census counters, scene attribution |
| switch/fix-final | a7acd55 | 2026-11-04 | fix: restore last painted canvas on skipped UI frames |
| switch/fix-g1 | 882a094 | 2026-11-04 | fix: keep canvas row mapping in dirty-band upload (NXGL-9/12) |
| switch/fix-g2 | 22240f2 | 2026-11-04 | fix: reuse 3D snapshot stores and skip redundant per-run GL state |
| switch/fix-g3 | f39f04b | 2026-11-04 | fix: pin compositor workers to cores 1,2 and fail pool closed at 2 on Switch |
| switch/fix-g4 | c42f825 | 2026-11-04 | fix: reopen the audio stream at the device rate on all builds (NXAUD-1) |
| switch/fix-g5 | b039a85 | 2026-11-04 | fix: freeze golden prof buckets and add scene-tagged gym metrics |
| switch/fix-gym | 1ea7e26 | 2026-11-04 | fix: merge gym G4 device-rate audio reopen (NXAUD-1) |
| switch/fix-h | 9f12190 | 2026-11-04 | fix: merge h H1 menu/world generation split (NXGL-12/22) |
| switch/fix-h1 | 0beb959 | 2026-11-04 | fix: split the UI paint gate into menu-state and world-tick generations |
| switch/fix-h2 | cda146b | 2026-11-04 | fix: budget snapshot copies, revalidate pool slots by content |
| switch/fix-h3 | 8d709f3 | 2026-11-04 | fix: gate the GPU 2D show/pixels path on wanted or forced screens (NXGL-4, NXGL-12) |
| switch/fix-q1 | 369e049 | 2026-11-03 | fix: gate the 2D compose PBO read on the screens readers want |
| switch/fix-q2 | a3cc6f5 | 2026-11-03 | fix: drop per-frame FBO checks and orphan the 3D vertex upload |
| switch/fix-q3 | d4c8b44 | 2026-11-03 | fix: boot: real UI summaries, unified force, composes-queued counter |
| switch/fix-q4 | 06573cf | 2026-11-03 | fix: ui summaries refresh on darkness change, drop canvas reuse |
| switch/fix-q5a | 3172c96 | 2026-11-03 | 2d: skip hwcompose hashes when inputs are unchanged |
| switch/fix-q5b | 32ad949 | 2026-11-03 | fix: present upload cache drops on context destroy, documents CPU-fallback-only hash |
| switch/fix-q5c | ab1c5d1 | 2026-11-03 | fix: skip texture copy and hash on store hit |
| switch/fix-q6 | 1f6e93f | 2026-11-03 | fix: platform stale fallback upload and device-rate audio queue count |
| switch/fix-r4s1 | d2350fd | 2026-11-03 | fix: skip identical interface canvas uploads by hash |
| switch/fix-r4s2 | ffb8f39 | 2026-11-03 | fix: gate the UI canvas redraw on the UI model generation |
| switch/fix-r4s3 | 0fc4971 | 2026-11-03 | fix: prof prints one hitch autopsy line per window |
| switch/fix-r5s1 | fbc4b49 | 2026-11-03 | fix: gate skips only while consecutive paints agree |
| switch/fix-r5s2 | 980b6fe | 2026-11-04 | fix: shrink 3D texture slots after excursions and take 1x reads through the PBO ring |
| switch/fix-r5s3 | 48f804b | 2026-11-03 | fix: open the audio stream at the device's native rate |
| switch/fix-r6 | bce2928 | 2026-11-04 | fix: orphan texture stores on re-spec, first upload always goes |
| switch/fix-round2 | 6ea2785 | 2026-11-03 | fix: host GetAudioStreamFormat import the queued-count fix needs |
| switch/fix-round3 | a3cadc8 | 2026-11-03 | fix: drop per-frame 3D status and error queries |
| switch/fix-round4 | fbc4b49 | 2026-11-03 | fix: gate skips only while consecutive paints agree |
| switch/fix-round5 | b3261da | 2026-11-04 | fix: orphan texture stores on re-spec, budget uploads per frame |
| switch/fix3-s1 | 6372ae3 | 2026-11-03 | tests: stub the pixel-request hook the input and PC recipes call |
| switch/fix3-s2 | 8acc117 | 2026-11-03 | fix: gate the 2D PBO readback on the screens the UI wants |
| switch/fix3-s3 | 82aaa1a | 2026-11-03 | fix: refresh shadow from src on GPU 2D compose failure |
| switch/fix3-s4 | 85c6747 | 2026-11-03 | fix: skip texture copy and hash on store hit |
| switch/fix3-s5 | 6372ae3 | 2026-11-03 | tests: stub the pixel-request hook the input and PC recipes call |
| switch/fix3-s6 | adf9a2d | 2026-11-03 | fix: boot S6 wiring for the GPU 2D enqueue gate |
| switch/fix3-s7 | ad1dc88 | 2026-11-03 | fix: drop per-frame FBO checks and orphan the 3D vertex upload |
| switch/fix3-s8 | 6372ae3 | 2026-11-03 | tests: stub the pixel-request hook the input and PC recipes call |
| switch/instr-run | 3309301 | 2026-11-03 | prof: remove the EGL table probe that black-screened run 33 |
| switch/instr2 | e3cb8fd | 2026-11-04 | prof: decisive counters for the in-game wall |
| switch/mesa-a2 | afb07ec | 2026-11-02 | tools: fetch pinned mesa-switch GL SDK into the cache |
| switch/mesa-a3 | 6c932e6 | 2026-11-02 | switch: link the Switch build against a mesa-switch prefix |
| switch/mesa-a4 | 8a0e187 | 2026-11-02 | switch: A3 verification record (mesa-switch link, nm, sizes) |
| switch/mesa-a5b | 411f01e | 2026-11-03 | gl3d: EIDOS_EDGE_DEBUG shows one edge input alone; EIDOS_EDGE_DIAG dumps the draw's raster state |
| switch/mesa-cpu2d | a512b26 | 2026-11-03 | WIP: main2d sub-buckets, OBJ poses after the GPU skip |
| switch/mesa-g2d-fence | 34030e2 | 2026-11-03 | GPU 2D: a screen's readback is mapped once the GPU has done it |
| switch/mesa-p0-prof | c707f58 | 2026-11-03 | prof: the buckets add up to the frame; a game-frame rate beside the swap rate |
| switch/mesa-s0 | 623c006 | 2026-11-02 | docs: Switch profile capture and the local-fork Mesa pin |
| switch/mesa-s2 | c74e62d | 2026-11-02 | wasi: the tests that need the host are built and not run, each with its reason |
| switch/mesa-test | 15e22d0 | 2026-11-02 | switch: merge mesa-switch Phase A (A1-A4) into test branch |
| switch/nro | eb7a56a | 2026-11-02 | switch: document the port's build, run and open issues |
| switch/perf-all | 6372ae3 | 2026-11-03 | tests: stub the pixel-request hook the input and PC recipes call |
| switch/perf-p1 | f49c344 | 2026-11-03 | 2d: gate the full-screen CPU copies on the screens the UI asked for |
| switch/perf-p2 | d0090c6 | 2026-11-03 | bridge: map only the asked range and reuse the scratch |
| switch/perf-p3 | 84a74e2 | 2026-11-03 | g2d: skip the GPU frame's getting-ready work when its windows match |
| switch/perf-p4 | 37d4cff | 2026-11-03 | ui: match on screen summaries, ask for pixels only to blit |
| switch/perf-p5 | ae83863 | 2026-11-03 | g3d: reuse texture scratch, snapshot and sort buffers; skip clear on GPU frames |
| switch/perf-p6 | d190714 | 2026-11-03 | gl3d: steady-state uploads without re-specs, edge pass hardened |
| switch/perf-p7 | 4ccd081 | 2026-11-03 | ui: reuse the last canvas on static field frames |
| switch/perf-p8 | ed3ae61 | 2026-11-03 | host: Switch CPU boost, tight EGL config, GL proc helper |
| switch/pin-mesa-integration | ebb17bb | 2026-11-02 | switch: pin mesa-switch at the integration SDK |

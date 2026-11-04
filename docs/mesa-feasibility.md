Verdict: feasible for M1–M4, risky for M5
Using devkitPro Mesa (EGL over nouveau, switch-mesa 20.1.0-5 + switch-libdrm_nouveau, switch/README.md:26-34) as the graphics backend is feasible. No architectural blocker:
- Stage one is unchanged: same eidos_boot.wasm from tools/build64.py. Only stage two (host/ + SDL3) changes — switch-plan.md:23.
- The GLES path already exists for Android/iOS (HOST_GLES=1, tools/wasm_gl_imports.py) and the Switch reuses it. Desktop GL translation is not required.
- All Switch scaffolding in src/eidos already exists: CMake branch, fixed memory, fibers, workers, title-mode gate, picker, build script. What is unproven is almost entirely on hardware: GL presenter correctness (M2), input/events (S7), and frame rate (M5/S9).
The honest split: M1 (SDL renderer → title) is low risk; M2 (GL presenter + GPU 3D → first town) is medium risk; M5 (59.83 fps field + battle, handheld 1x) is high risk and will almost surely need S9 work.
Current hardware evidence (in-tree, not guessed):
- src/eidos/switch/boot.txt, boot-1.txt reach host_run: instantiate → start. Boot path works to module entry.
- bw-switch/stderr.txt:8-12 records the known failure mode under test: SDL_CreateWindow(1024x640) → null (EGL not initialized) for the setup window. src/eidos/host/sdl.c:281-291 already contains the workaround (explicit SDL_GL_LoadLibrary for non-OPENGL windows). Whether that fixed it on hardware is not recorded — first spike item.
- src/eidos/host/main.c:396-402: EIDOS_RENDERER=sdl is forced as the Switch default because "the GL presenter is slower … and can leave the world black." So M2 is a known-open bug, not a plan item.
- src/eidos/switch/stderr.txt is empty; no M2/M5 numbers in switch-plan.md:Results.
1. Integration surface (as built)
Item	State in tree
host/CMakeLists.txt NINTENDO_SWITCH branch	Done. EIDOS_HOST_SDL_SOURCE static SDL, SDL_DYNAMIC_API OFF (host/CMakeLists.txt:59-68); HOST_GLES=1 with Android/iOS (:164-166); -fsigned-char + switch_include forced-include shim for sigsetjmp (:167-176, host/switch_include/eidos_switch.h); link order SDL3::SDL3 EGL glapi drm_nouveau nx m (:252); NACP (eidos/Project Holon/${EIDOS_SWITCH_VERSION}) + nx_create_nro with switch/icon.jpg (:269-280).
HOST_GLES=1, -fsigned-char, link order	Done as above. Matches switch-plan.md:S2.
.nro/.nacp/icon	Done. Icon src/eidos/switch/icon.jpg; version flows switch/build.py:version() → -DEIDOS_SWITCH_VERSION.
switch/build.py vs android/build.py	Done, correct divergence. switch/build.py:54-94 mirrors android/build.py:68-95 (fetch SDL → configure host/ → build → print artifact) but builds eidos_nro via $DEVKITPRO/cmake/Switch.cmake, --parts 96 default, --guard-pages flag. Android builds libmain.so + SDL3-shared + Gradle APK; Switch needs no Java/packaging step.
EIDOS_HOST_SDL_SOURCE pin	Done. tools/fetch_sdl_switch.py:26-28: devkitPro SDL @ c329016c…, SHA-256 6047e478…, cached outside repo. switch/README.md:38-40 records it.
mesa-switch/	Contains only .git. Correct: Mesa is not vendored — it is the devkitPro switch-mesa package linked as EGL/glapi/drm_nouveau. There is no separate mesa-switch integration task.
2. Constraints (as built)
- Title-mode only: host/main.c:352-375 switch_application_mode() — appletGetAppletType() != AppletType_Application → libnx-console message, wait A, exit. Plus __nx_heap_size = 1536 MiB (:59). Caveat recorded in switch/README.md:8-11: an applet start can die on the heap request before the message prints. Applet mode is correctly not a goal.
- No mmap: host/mem_switch.c — one aligned_alloc'd 1 GiB block (MEM_BUDGET_DEFAULT_MB, EIDOS_SWITCH_MEM_MB override), growth returns the same pointer, over-budget fails like memory.grow with module memory budget reached. WASM_RT_USE_MMAP=0 + WASM_RT_MEMCHECK_BOUNDS_CHECK=1 + calloc/realloc/free redirect in host/CMakeLists.txt:178-186. Optional fast path host/mem_switch_guard.c (EIDOS_SWITCH_GUARD_PAGES): virtmemFindAslr + virtmemAddReservation + svcMapMemory, falls back to fixed block. Matches S3/S9 plan.
- Fixed budgets: module 1 GiB; host/fiber.c:45-52 NATIVE_STACK 2 MiB on Switch (vs 16 MiB); host/par.c:38-44 PAR_NATIVE 2 MiB, PAR_STACK 1 MiB module stack/worker.
- 1 game thread + ≤2 workers: host/par.c:114-127 forces n = 2, clamps > 2; host/main.c:538-561 run_on_switch_thread gives host_run its own 8 MiB SDL thread (loader main-stack size unknown). HOST_FIBER_ASM includes __SWITCH__ (host/fiber.c:39-42), stacks via aligned_alloc(16,…) (:204-208).
- HOME/sleep save flush — GAP: engine/platform/platform.c:1773-1912 eidos_platform_pump handles QUIT, resize, exposed, gamepad, text input — no SDL_EVENT_TERMINATING / background / low-memory case. Save durability currently rests on sdk/arm7/fs.c:192-258 (atexit flush + 1 s flush_delay) and sdk/plat_save.c:158 ("close is the flush" on libnx FAT). Whether HOME-menu kill runs eidos_platform_quit → flush path on Switch is unproven. S7 must add/handle it.
- Dock/handheld resize — half-built: host/sdl.c:269-276 picks 1920×1080 vs 1280×720 via appletGetOperationMode() at window creation only. platform.c:1870-1901 handles WINDOW_RESIZED / PIXEL_SIZE_CHANGED → render(p), so a dock/undock resize event re-renders, but dynamic dock-switch behavior is M4-hardware-unproven.
- Touch — GAP: platform.c:506-539 read_stylus uses only SDL_GetMouseState; pump has no SDL_EVENT_FINGER_* / touch case. Works only if devkitPro SDL synthesizes mouse from touch. Must be verified at M4; S7 explicitly lists it as check-first.
- Software keyboard: platform.c:2413-2425 eidos_platform_text_mode → SDL_StartTextInput/StopTextInput; host/sdl.c forwards them. Whether devkitPro SDL raises the Switch swkbd for player/rival/Pokémon naming is hardware-unproven (S7 item).
3. Milestones — what proves Mesa
#	Proves	Bar
M1	SDL-renderer path, no GPU 3D (EIDOS_RENDERER=sdl in env.txt)	Extract → title + opening music
M2	Mesa path: HOST_GLES translation → ES 3.0, GL presenter (plat_gl.c), GPU 3D (plat_gl3d.c), edge-mark R8UI pass, no forced multisample breakage	Title → first town 3D correct
M3	Full loop: New Game, first battle, save, HOME-quit, continue	Save present after restart (sdmc:/switch/eidos/profile/saves/)
M4	Dock/undock while playing	Picture correct both modes; touch handheld
M5	--profile field + battle, handheld, 1x	Numbers in Results; gates S9
M2's Mesa-specific checks (from switch-plan.md:Risks + docs/graphics-backends.md:36-41): integer render target (edge marking), multisampling behavior, fog / rear-plane-bitmap / shadow-polygon frames (these already fall back to CPU rasteriser in bands — correct but slow), DISP_1DOT_DEPTH, capture-every-frame scenes (title/logos, DS-resolution-limited by design).
4. Effort (switch-plan tasks; person-days, hardware time included)
Task	Size	Days	Note
S1 toolchain + SDL3	S	1–3	Largely done (fetch pin, README versions). Remaining: re-verify Switch.cmake provides NINTENDO_SWITCH / nx_generate_nacp / nx_create_nro on current devkitPro.
S2 host/CMakeLists.txt branch + switch/build.py	S	1–3	Done in tree; remaining is cleanup + keep-desktop-green.
S3 fixed memory	S–M	2–5	mem_switch.c done; needs S0-budget desktop-logic proof (base address stable under EIDOS_HOST_STATS).
S4 fibers/threads	S	2–4	Done in tree; needs cross-compile probe check.
S5 startup/logging/crash	S	2–4	Done in tree; needs nxlink + applet/title hardware confirmation.
S6 paths/ROM/wizard	S	2–4	install/location.c:194-198,236-239 + install/picker.c:182-227 (first *.nds sorted) done; wizard-run vs skip needs maintainer call.
S7 video/input/system	M	5–10	The real work: dock-resize verification, touch mapping fix if no mouse synthesis, swkbd check, HOME/terminate flush, no-op the meaningless imports (clipboard, window pos/icon/borders).
M2 bring-up	M	5–10	Triage GL-black/slow default; R8UI + multisample first; per-frame CPU-fallback census.
S9 speed → 59.83 fps 1x handheld	L	15–30+	Bounds-check removal (guard pages), CPU-fallback frames → GPU (graphics-backends.md beneficiaries all platforms), 1x/2x policy, --fpcast-emu measurement. Open-ended; time-box by M5 profile.
S10 docs/CI/release	S	2–5	README/building/CHANGELOG + pinned devkitpro/devkita64 digest CI job, no tests on device.
S0 (measure-first, desktop-only) precedes all budget decisions and is not estimated here.
5. Top 5 risks + mitigations
1. Mesa ES 3.0 gaps (R8UI edge marking, multisample, capture scenes). The translation targets ES 3.0; those two features are named check-first in the plan. Mitigate: M2 spike with EIDOS_RENDERER_REPORT=1; disable edge-multisample frame path first, keep CPU fallback correct; fix in pinned SDL fork / report upstream only if SDL-side.
2. CPU budget: bounds checks + single slow core. WASM_RT_MEMCHECK_BOUNDS_CHECK=1 roughly doubles memory-access cost; game runs on 1 of 3 app cores. Mitigate: mem_switch_guard.c path (--guard-pages) + M5 profile before optimizing; keep fixed block as fallback.
3. CPU-fallback 3D frames dominate. Fog / rear-plane / shadow frames already fall back to CPU row-bands (docs/graphics-backends.md:38-41). On Switch silicon these may be the M5 long tail. Mitigate: census which scenes hit fallback at M2; that GPU work helps all platforms, coordinate rather than Switch-fork.
4. Input/event gaps (touch, swkbd, HOME flush). No finger-event handling, swkbd unverified, no terminate handler — M3/M4 blockers hiding in S7. Mitigate: explicit spike cases for each; add SDL_EVENT_TERMINATING/background → bw_arm7_backup_flush() + bw_vfs_flush() + settings save, verified by kill-and-continue test.
5. Memory ceiling. 1 GiB module + Mesa + GPU buffers under title-mode ~3.2 GiB with a 1.5 GiB newlib heap request. If S0 peak + Mesa exceeds it, the plan needs a smaller budget or port memory work. Mitigate: S0 first; EIDOS_SWITCH_MEM_MB sweep on hardware; applet stays out of scope.
6. Minimal de-risk spike (hardware, via nxlink)
1. Build: python3 switch/build.py --module build64/module/eidos_boot.wasm on pinned devkitPro; install sdmc:/switch/eidos/eidos.nro; env.txt with EIDOS_NXLINK=1, EIDOS_FPS_LOG=1, EIDOS_RENDERER_REPORT=1.
2. SDL+EGL triangle: boot to setup window — expect host/sdl.c CreateWindow ok + SDL_CreateRenderer ok lines over nxlink -s, no EGL not initialized. This alone closes the recorded stderr.txt failure.
3. Present + resize: reach title (M1, EIDOS_RENDERER=sdl); undock/redock, confirm WINDOW_PIXEL_SIZE_CHANGED → re-render and correct 1280×720 ↔ 1920×1080.
4. GL probe (M2 entry): EIDOS_RENDERER=gl; record report_gl renderer/version string; screenshot title + first town; note edge-mark/multisample artifacts and which frames fall back to CPU.
5. Record all in switch-plan.md:Results with date/commit/numbers. Stop on first divergence and report — per plan rules.
7. Explicitly out of scope (unchanged)
- Stage-one wasm module: identical for all 64-bit builds (lp64.md); built on PC.
- Other platforms: desktop/Android/iOS behavior guarded by __SWITCH__ / NINTENDO_SWITCH only.
- aletheia: never edited (plan Rule 1).
- Rust port: Switch is last and may stay C (rust-port.md:12,102,678-679,1115,1140-1141): host/fiber.c, host/par.c, host/mem_switch.c, GL bindings and pickers stay C. Host stays C for this work.

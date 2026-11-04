Eidos OpenGL map — facts only
Working tree: /Users/terezi/bw-switch/src/eidos
1. File list (absolute, 1-line role)
Presenter / GL renderer:
- /Users/terezi/bw-switch/src/eidos/engine/platform/plat_gl.h — public presenter API: upload/present/grab, set_3d, render3d, capture, gl2d_compose, view helpers.
- /Users/terezi/bw-switch/src/eidos/engine/platform/plat_gl_int.h — private EidosGl + GlFb; GL_FUNCS(X) loader list; __wasm__ import note.
- /Users/terezi/bw-switch/src/eidos/engine/platform/plat_gl.c — screen presenter + 3D composite/capture shaders, eidos_gl_create/present/upload/set_3d/capture.
- /Users/terezi/bw-switch/src/eidos/engine/platform/plat_gl3d.c — GPU 3D rasteriser eidos_gl_render3d, edge/fog passes, MSAA, texture cache.
- /Users/terezi/bw-switch/src/eidos/engine/platform/plat_gl2d.c — GPU 2D compositor eidos_gl2d_compose/ready/pixels.
- /Users/terezi/bw-switch/src/eidos/engine/platform/plat_gl_view.c — spot-editor tool window keep3d/upload/begin/draw/read/end.
- /Users/terezi/bw-switch/src/eidos/engine/platform/platform.c — SDL window/renderer/GL owner, present_n/nn, set_3d*, render(), out_size/viewport/reshape.
- /Users/terezi/bw-switch/src/eidos/engine/platform/platform.h — public contract: DS sizes, layouts, present_n/nn, gpu3d/set_3d/capture/read3d_1x, pacer/audio.
- /Users/terezi/bw-switch/src/eidos/engine/platform/geometry.c:322-327 — eidos_resolution_fit/cap (auto N from viewport).
- /Users/terezi/bw-switch/src/eidos/engine/platform/settings.c:2557-2575 — eidos_settings_internal_scale() (explicit N else fit, capped by GPU vs CPU).
- /Users/terezi/bw-switch/src/eidos/engine/g3d/hw3d.h:197-237 — G3dSnapshot, G3D_SNAP_*, gpu_features/skip_raster/snapshot_enable.
- /Users/terezi/bw-switch/src/eidos/engine/g3d/hw3d.c:890-911,1136-1149,1331-1332 — snapshot build + unsupported &= ~gpu_features + skip logic.
- /Users/terezi/bw-switch/src/eidos/demo/eidos_boot.c:848-958,1365-1386 — boot_gpu3d() decision + per-frame CPU/GPU scale choice; BOOT_CPU_3D_MAX 2u:115.
Host (stage-2 native):
- /Users/terezi/bw-switch/src/eidos/host/sdl.c — w2c_env_SDL_* + w2c_env_SDL_GL_* imports; HOST_GLES attribute rewrite; report_gl() ES detect; window-size override on Switch.
- /Users/terezi/bw-switch/src/eidos/host/CMakeLists.txt — stage-2 build: wasm-opt→wasm2c→gl_names.txt→gl_imports.c→eidos; HOST_GLES=1 on Android/iOS/Switch; Switch links EGL glapi drm_nouveau.
- /Users/terezi/bw-switch/src/eidos/host/host.h:59-77 — host_ptr(), host_gl_es/_version/_window_fbo.
- /Users/terezi/bw-switch/src/eidos/host/main.c:378-474,496-560 — mobile_args() (--resolution 1 on Switch), game-thread stacks, Switch title-mode/applet check.
Tools:
- /Users/terezi/bw-switch/src/eidos/tools/wasm_gl_imports.py — generates gl_imports.c from SDL headers + module import list; GLES3 translations.
- /Users/terezi/bw-switch/src/eidos/tools/wasm_imports.py — lists env gl* imports from wasm binary (gl_names.txt).
Docs/plans:
- /Users/terezi/bw-switch/src/eidos/docs/graphics-backends.md — 3.3 core default, SDL fallback, snapshot/composite, fog/rear/shadow CPU fallback, MSAA/edge notes.
- /Users/terezi/bw-switch/src/eidos/docs/runtime.md:340-417 — pacer 59.8261Hz, no vsync, upload ABGR8888, screenshot from shadows.
- /Users/terezi/bw-switch/src/eidos/docs/lp64.md:368-424 — wasm32+wasm2c arch: imports, HOST_GLES, --fpcast-emu, guard pages.
- /Users/terezi/bw-switch/src/eidos/docs/building.md:94-206 — SDL3 per-OS, Android/iOS templates, Switch switch/README.md pointer.
- /Users/terezi/bw-switch/src/eidos/docs/gpu-2d.md — phases 1-4, fallback list, inputs table.
- /Users/terezi/bw-switch/switch-plan.md — Switch: NRO/title-mode only, HOST_GLES ES3-first, 1280x720/1920x1080, EIDOS_RENDERER=sdl default, M2 checks integer RT+MSAA.
- /Users/terezi/bw-switch/rust-port.md:585-606,668-683 — plat_gl*.c stays C, SDL/GL via platform-sys; host fiber/par/mem_switch stay C.
2. Stage-one wasm SDL3/GL → imports
Stage-one is eidos_boot.wasm (wasi-sdk, sdk/wasm_layout.h #pragma pack(4)). SDL3 + GL are env imports (pointers = u32 module offsets).
host/CMakeLists.txt:105-115:
wasm_imports.py ${_mod} env gl -o gl_names.txt
wasm_gl_imports.py --sdl-include ... --names gl_names.txt -o gl_imports.c
tools/wasm_gl_imports.py:4-34 header: prototypes from SDL_opengl.h/glext.h; SCALAR maps GLenum→u32 … GLfloat→f32; generic gen() emits w2c_env_glFoo(env,u32…) loading via SDL_GL_GetProcAddress on first use, gl_ptr(addr)=host_ptr(addr).
Hand-written SPECIAL (56): glVertexAttribPointer/glDrawElements (offset→(void*)(uintptr_t)offset), glReadPixels (pack-buffer vs memory; ES RGB→RGBA read + keep RGB 199-235), glShaderSource (array of u32 addrs; #version 330 core→#version 300 es+precision 237-267), glMapBuffer/UnmapBuffer (copy to module malloc + glMapBufferRange on ES 352-424), glClearDepth→glClearDepthf 299-316, glColorMaski→EXT/OES or buffer0 only 318-346, glBindFramebuffer fb0→host_gl_window_fbo (iOS) 288-297, glGetIntegerv MAX_INTEGER_SAMPLES→0 on ES<3.1 269-286.
Desktop: none of the host_gl_es branches taken. GLES path (HOST_GLES=1, host/CMakeLists.txt:165, host/sdl.c:569-577):
SDL_GL_CONTEXT_PROFILE_MASK→ES; MAJOR 3; MINOR 0; FLAGS 0;
report_gl(): strncmp(v,"OpenGL ES",9)→host_gl_es=1, sscanf→host_gl_es_version (30)
3. Presenter (plat_gl + platform.c)
plat_gl.h:2-10: two screens uploaded as RGBA textures any size, one fragment shader scales to window; OpenGL 3.3 core; NULL → caller recreates window without OPENGL for SDL renderer.
Create (plat_gl.c:508-540):
SDL_GL_SetAttribute(MAJOR,3); MINOR 3; PROFILE CORE;
g->ctx=SDL_GL_CreateContext(window); // NULL→free→NULL
GL_FUNCS load via SDL_GL_GetProcAddress (or &import on __wasm__)
SDL_GL_SetSwapInterval(0); // pacer is timebase
3D/capture/panel/one-screen programs optional — failure leaves working presenter (579-666).
Present (platform.c:1166-1231,1990-2053):
- out_size():456-467: GL→SDL_GetWindowSizeInPixels; SDL renderer→SDL_GetCurrentRenderOutputSize else SDL_GetWindowSize.
- viewport():431-439: ONE→eidos_one_fallback_viewport; else eidos_display_viewport_n(pixel_scale).
- render():1180-1231: eidos_gl_one_set() + eidos_gl_present(gl,ow,oh,rects) else SDL_RenderClear/Texture/Present.
- present_nn():2020-2027: GPU-composed screen (gpu2d_compose…to_screen) skips eidos_gl_upload; else upload 256N×192N; reshape_window() on arrangement change.
- One-screen world render_3d_for():2111-2138: 3D rendered at window ow×oh (or DS rect if ds_only), clamped ≤8192.
Resize: free-resize window; reshape_window():1297-1321 keeps area on layout change (windowed only); eidos_settings_on_resize persists. No OS mode change — fullscreen = desktop fullscreen (1328-1348).
1x/2x/internal resolution:
- settings.c:2557-2575: explicit N else eidos_resolution_fit_cap(vp,max, gpu?6:2) (EIDOS_RESOLUTION_MAX 6, FIT_CAP 2, platform.h:150-162).
- eidos_boot.c:1375-1385: gpu&&gpu3d_ok → snapshot_enable(1)+skip_raster(1)+scale(1) else scale(min(want,2)) (BOOT_CPU_3D_MAX 2, cost ~360ms/frame at 6x).
- Switch: host/sdl.c:269-276 forces 1920×1080 docked / 1280×720 handheld; host/main.c:442-444 passes --resolution 1 (“Handheld first: 1x … in-game setting overrides”); switch-plan.md:S7/S9: presenter follows dock/undock size event; target 1x handheld, 2x docked if holds rate.
4. GL version / extensions / targets
Requested: OpenGL 3.3 core (plat_gl.c:513-515); ES 3.0 via HOST_GLES (host/sdl.c:569-577). No loader lib — SDL_GL_GetProcAddress. All #version 330 core (plat_gl.c:14,48,79,135,169,217,319,339; plat_gl3d.c:65,78,157,180,295,348; plat_gl2d.c:21,28; plat_gl_view.c:14,29); rewritten to 300 es on host for module shaders.
Need	GL facts (lines)
Integer RT (edge marking)	GlFb.pid R8UI (plat_gl_int.h:76); glTexImage2D(R8UI,RED_INTEGER,UBYTE) (plat_gl3d.c:1389-1391); glClearBufferuiv(COLOR,1,id4) + glColorMaski(1,…) (1839-1841,1058,1181,1658-1674); layout out uint o_id (78-83); GL_FUNCS has DrawBuffers/ColorMaski/ClearBufferfv/uiv/Uniform1ui (72)
Depth	DEPTH24_STENCIL8 texture (1393-1395); stencil holds 0x40|ID for translucent-same-ID reject (1874-1877)
Multisample	MSAA_SAMPLES 4, MSAA_MAX_PIXELS 2048×1536 (1523-1524); RenderbufferStorageMultisample+BlitFramebuffer (1554-1568,1981-1994,2028); integer_samples from GL_MAX_INTEGER_SAMPLES (1249), ES3→0 (1539); Apple integer_samples<4 → 2x supersample + LINEAR blit (2007-2029); edge+MSAA mutually exclusive (1785-1791)
Edge marking	2nd pass pedge (1215-1225); optional — absent → G3D_SNAP_EDGE stays unsupported (1260-1274); width eidos_gl_set_edges 0/1/2 (2037); u_step=w/256 (1412)
Fog	pass pfog (1227-1238); optional like edge (1213-1238); fog_pass reads col/id/dep + table/offset/clear (1467-1515)
Rear-plane bitmap	DISP3DCNT 0x4000 → G3D_SNAP_REARBMP (hw3d.c:1143-1144); never cleared by gpu_features → always CPU (no GL program)
Shadow polygons	G3D_HW_UNSUP_SHADOW 0x20 (hw3d.h:57); set when MODE==SHADOW (hw3d.c:1206-1207,2029); → G3D_SNAP_SHADOW (1146-1147); draw_run skips mode==SHADOW (1920-1923)
W-buffer / AA bit	u_wbuf → gl_FragDepth=1/w/16M (151-152); DISP3DCNT 0x10 → MSAA path above
Readback/PBO	eidos_gl_read3d_1x(snap,out,async) (2161-2180); pbo+PACK_BUFFER+serial (2108-2143); ES glReadPixels RGB shim above
5. CPU fallback — where decided
- Snapshot flags (hw3d.c:1136-1149): 0x20→EDGE, 0x80→FOG, 0x4000→REARBMP, shadow→SHADOW; unsupported &= ~g_gpu_features where feat=(pedge?EDGE:0)|(pfog?FOG:0) (plat_gl3d.c:1260-1274, EIDOS_GPU3D_FORCE can force all).
- Per-frame (eidos_boot.c:861-864,1377-1385): snap==NULL‖unsupported!=0‖DISP3DCNT !3D/graphics/BG0-3D → set_3d(-1), gpu3d_ok=(unsupported==0); skip=gpu&&gpu3d_ok → snapshot_enable+skip_raster+scale(1) else CPU at min(want,2).
- eidos_platform_set_3d/3d2d/from2d return -1 when !gpu3d‖render3d!=0 (platform.c:2140-2193); render_into returns -1 when !have3d‖fbo_resize fail‖glError (1679-1705,2004).
- GPU-2D fallback (gpu-2d.md:103-110): no 3.3 / shader fail, display mode≠1, forced blank, capture running, G2D_HW_UNSUP_*.
- Stated in graphics-backends.md:38-40,112-113: fog/rear/shadow (+DISP_1DOT) → CPU in row bands on worker pool; capture-every-frame scenes capped at DS size.
6. SDL3 usage
- Window: SDL_WINDOW_RESIZABLE\|HIGH_PIXEL_DENSITY + OPENGL when try_gl (platform.c:1584-1600); dummy driver or EIDOS_RENDERER=sdl → SDL renderer (1580-1611); centered, icon from icon_rgba.h (1612-1640); hidden when EIDOS_WINDOW_HIDDEN/embed (1588-1596).
- GL context: SDL_GL_CreateContext in module (plat_gl.c:524, wasm import in host/sdl.c:607-633); attrs 3.3 core vs ES3 (above); report_gl() logs RENDERER/VERSION, warns on llvmpipe/softpipe/Software (583-605).
- Vsync/swap: SetSwapInterval(0) (plat_gl.c:540); SDL_GL_SwapWindow (host/sdl.c:652-662, fps tick on Switch); SDL path SDL_RenderPresent (platform.c:1256, host/sdl.c:504-514); renderer created with no vsync — pacer (59.8261Hz, platform.h:567-576) is timebase (platform.c:1642-1648, runtime.md).
- EGL: desktop via SDL; Switch host/CMakeLists.txt:252: SDL3 EGL glapi drm_nouveau nx m; switch-plan.md:4-5: devkitPro switch-sdl-3.4 video=EGL over Mesa.
- Handles: module u32↔host pointers (handle_of/ptr/drop, HANDLES_MAX 4096, host/sdl.c:44-81); events with string members repacked (762-796); Switch window forced size + pre-CreateWindow SDL_GL_LoadLibrary for non-GL wizard window (277-291).
7. Translation pipeline (text)
stage1: clang/wasi-sdk → eidos_boot.wasm
  engine/platform/*.c calls SDL_GL_* + SDL_* (u32 addrs, __wasm__ takes &import)
    │ imports: env/SDL_*, env/gl*
    ▼
host build:
  wasm_imports.py (reads import section) → gl_names.txt (one gl* /line)
  wasm_gl_imports.py --sdl-include → gl_imports.c
    generic: w2c_env_glF(env, u32/f32…)= SDL_GL_GetProcAddress("glF"); gl_ptr()=host_ptr()
    special: VertexAttrib/DrawElements offset, ReadPixels pack/ES-RGB,
             ShaderSource addrs+330→300es, Map/Unmap copy+Range,
             ClearDepth→f, ColorMaski EXT/OES, BindFB0→ios_fbo,
             GetIntegerv MAX_INTEGER_SAMPLES→0
  host/sdl.c: w2c_env_SDL_* (window/handles/events/audio) + GL attrs/ES detect
  host/wasi.c, fiber.c, par.c, net.c, main.c → eidos (+ SDL3, wasm2c runtime)
desktop: 3.3 core direct │ Android/iOS/Switch (HOST_GLES=1): ES 3.0 via above shims

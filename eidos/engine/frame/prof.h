/*
 * engine/frame/prof.h -- a cheap per-phase frame profiler.
 *
 * Off by default (one predictable branch per probe).  With it on, every
 * `every` frames it prints the average and maximum milliseconds each phase
 * took in that window.  Single-threaded: the frame thread owns it.
 *
 * Nesting.  The top-level buckets (game, pre, step, render, pump, mod,
 * present, audopen, audio, wait) sum to the frame's wall time: the game
 * runs between two halts, boot_frame is the halt hook, and "step" is the
 * frame run with render taken out.  Under render: ui, g2dprep, g3d (which
 * holds read3d), main2d, capture, boot3d (which holds bcap), g2dread.
 * geom, raster, snap and texdec run inside the game's own FIFO commands
 * and swaps, so they are inside "game"; texdec is inside snap or raster.
 * gldraw and glup are inside boot3d (set_3d) or read3d (eidos_gl_read3d_1x
 * draws the frame at DS size first).  glread and unprem are inside read3d
 * too: the sync read, or the async finish of the PBO ring.
 * The per-window max of a bucket with two probe sites (g2dprep) is the
 * larger of the two, not their sum.
 *
 * Golden log format (Stream G5 freeze): the run-43 bucket set above, plus
 * the per-window tail `unacc` and the census counters (paint_n,
 * upbytes_canvas/screen, upcalls, want, aqueue, upskip, swapgap, pbos,
 * skip_n, gxcmds/verts/polys, pboq, ngtex, texup/texrespec, read_async)
 * plus the S5 additions (ui_redraw/ui_skip, pool_hi, memfree) and the
 * per-window fields `fps` and `scene`. Legend: RENDER contains G3D/UI,
 * BOOT3D contains gldraw/glup, PRESENT is compose+render+swap, never
 * summed. Nested buckets never sum; no bucket is removed, ever.
 */
#ifndef EIDOS_FRAME_PROF_H
#define EIDOS_FRAME_PROF_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EIDOS_PROF_STEP,      /* input, vblank and fast forward; render excluded */
    EIDOS_PROF_G3D,       /* the 3D frame (inside render)               */
    EIDOS_PROF_MAIN,      /* the main engine's 2D compose (inside render) */
    EIDOS_PROF_SUB,       /* the sub engine's 2D compose                */
    EIDOS_PROF_PRESENT,   /* upload and present                         */
    EIDOS_PROF_AUDIO,     /* sound render and queue                     */
    EIDOS_PROF_WAIT,      /* the pacing sleep                           */
    EIDOS_PROF_GEOM,      /* 3D geometry commands (inside game)         */
    EIDOS_PROF_RASTER,    /* software 3D rasteriser (inside game)       */
    EIDOS_PROF_SNAP,      /* GPU snapshot copy (inside game)            */
    EIDOS_PROF_TEXDEC,    /* texture decode (inside snap or raster)     */
    EIDOS_PROF_GLDRAW,    /* GL 3D draw calls (CPU side)                */
    EIDOS_PROF_GLUP,      /* GL texture uploads                         */
    EIDOS_PROF_RENDER,    /* the whole render hook                      */
    EIDOS_PROF_BOOT3D,    /* the GPU 3D layer set-up (inside render)    */
    EIDOS_PROF_GLREAD,    /* glReadPixels (waits for the GPU)           */
    EIDOS_PROF_UNPREM,    /* un-premultiply and flip after the read     */
    /* Added after the first buckets, so the older names keep their places
     * and their meaning in old logs.  Top level: */
    EIDOS_PROF_GAME,      /* the game between two frames: ARM9, ARM7, OS */
    EIDOS_PROF_PRE,       /* the frame's start, before the step          */
    EIDOS_PROF_PUMP,      /* eidos_platform_pump: the window's events    */
    EIDOS_PROF_MOD,       /* after the step: mod tick, headless capture
                           * step, screenshots                           */
    EIDOS_PROF_AUDOPEN,   /* the audio device check; with no device, the
                           * sound render                                */
    /* Inside render (boot_render): */
    EIDOS_PROF_UI,        /* boot_ui_frame                               */
    EIDOS_PROF_G2DPREP,   /* compose set-up: capture plan, screen buffers,
                           * the 2D frame's prologue                     */
    EIDOS_PROF_CAPTURE,   /* the display capture in hwcompose            */
    EIDOS_PROF_G2DREAD,   /* GPU 2D screens to the presenter and their
                           * readback (and the EIDOS_G2D_GPU_CHECK compare) */
    EIDOS_PROF_READ3D,    /* eidos_platform_read3d_1x (inside g3d)       */
    EIDOS_PROF_BCAP,      /* boot_capture_run (inside boot3d)            */
    /* Inside present (present_nn; the UI canvas upload runs under
     * render/UI but is counted in PREUP too, so PREUP is uploads
     * anywhere while PREDRAW/PRESWAP stay strictly inside present): */
    EIDOS_PROF_PREUP,     /* texture uploads: screens, canvas, panel    */
    EIDOS_PROF_PREDRAW,   /* GL submit: scene, panel (inside present)   */
    EIDOS_PROF_PRESWAP,   /* SDL_GL_SwapWindow alone (inside present)   */
    EIDOS_PROF_PREFIN,    /* sampled full sync before swap (diagnostic
                           * only: every 120th frame, tell GPU drain
                           * apart from swap-path stalls) */
    EIDOS_PROF_COUNT
};

extern int eidos_prof_on;

void eidos_prof_enable(unsigned every);
unsigned long long eidos_prof_begin(void);
void eidos_prof_end(int phase, unsigned long long t0);
/* A per-window census: set() keeps the last value, add() accumulates.
 * Up to 32 distinct labels; unknown labels are ignored. Cheap branches
 * when profiling is off. Golden labels (S5 freeze, never removed):
 * paint_n, upbytes_canvas, upbytes_screen, upcalls, want, aqueue, upskip,
 * swapgap, pbos, skip_n (presents skipped for lag), gxcmds, verts, polys,
 * pboq, ngtex, texup, texrespec, read_async, ui_redraw, ui_skip, pool_hi
 * (snapshot pool high-water, entries), memfree (free bytes, -1 unknown).
 * scene() carries the UI scene name for the window (spaces become '_');
 * fps is derived per window (frames / wall seconds). */
void eidos_prof_set(const char *label, long long value);
void eidos_prof_add(const char *label, long long value);
/* The window's scene tag (a UI scene name); copied into a fixed buffer,
 * no allocation. Cheap branch when profiling is off. */
void eidos_prof_scene(const char *name);
/* End of a frame: counts it and prints a line when the window is full.   */
void eidos_prof_frame(unsigned long long frame_no);

#ifdef __cplusplus
}
#endif

#endif

/* engine/frame/prof.c -- see prof.h. */

#include <stdio.h>
#include <string.h>

#include "prof.h"
#include "plat_thread.h"

#if defined(__SWITCH__)
#include <switch.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

int eidos_prof_on;

static unsigned g_every;
static unsigned g_n;
static unsigned long long g_sum[EIDOS_PROF_COUNT];
static unsigned long long g_max[EIDOS_PROF_COUNT];
static unsigned long long g_win_start;
/* The hitch autopsy: the probe phase most recently ended (begin takes no
 * phase argument, so this is the last phase end saw; at a frame line it
 * names the frame's tail phase), the last frame line's time, and whether
 * this window already printed its one hitch line. The line prints the
 * phase and the top-3 buckets by max, nothing more: one line per window
 * max, on wall > 500 ms only. */
static int g_cur = -1;
static unsigned long long g_prev;
static int g_have_prev;
static int g_autopsied;
static unsigned long long g_win_max;    /* max single frame in the window */
static unsigned long long g_frame_t0;   /* previous frame line's time      */
/* The window's scene tag: the UI scene name at the last frame, copied
 * into a fixed buffer (no allocation, no per-frame scan beyond a short
 * compare). Spaces become '_' so the prof line stays greppable. */
#define EIDOS_PROF_SCENE 48
static char g_scene[EIDOS_PROF_SCENE] = "unknown";

/* Free memory in bytes, sampled once per window (-1 when unknown). On the
 * Switch this is the process's TotalMemorySize minus UsedMemorySize
 * (NXMEM-6: it must not trend down across map changes); elsewhere a
 * portable best effort, never a per-frame cost. */
static long long prof_free_mem(void)
{
#if defined(__SWITCH__)
    u64 total = 0, used = 0;

    if (svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0) != 0) {
        return -1;
    }
    if (svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0) != 0) {
        return -1;
    }
    return total > used ? (long long)(total - used) : 0;
#elif defined(__linux__)
    struct sysinfo si;

    if (sysinfo(&si) != 0) {
        return -1;
    }
    return (long long)si.freeram * (long long)si.mem_unit;
#else
    return -1;
#endif
}

static const char *const k_names[EIDOS_PROF_COUNT] = {
    "step", "g3d", "main2d", "sub2d", "present", "audio", "wait",
    "geom", "raster", "snap", "texdec", "gldraw", "glup", "render", "boot3d", "glread", "unprem",
    "game", "pre", "pump", "mod", "audopen", "ui", "g2dprep", "capture", "g2dread", "read3d",
    "bcap", "preup", "predraw", "preswap", "prefin"
};

/* A per-window census: set() keeps the last value, add() accumulates. */
#define EIDOS_PROF_CENSUS 32
static const char *g_clabel[EIDOS_PROF_CENSUS];
static long long g_clast[EIDOS_PROF_CENSUS];
static unsigned long long g_csum[EIDOS_PROF_CENSUS];
static int g_cmode[EIDOS_PROF_CENSUS];      /* 0 last-wins, 1 accumulate */

static int census_slot(const char *label)
{
    int i;

    for (i = 0; i < EIDOS_PROF_CENSUS; i++) {
        if (g_clabel[i] == label || (g_clabel[i] != NULL && strcmp(g_clabel[i], label) == 0)) {
            return i;
        }
    }
    for (i = 0; i < EIDOS_PROF_CENSUS; i++) {
        if (g_clabel[i] == NULL) {
            g_clabel[i] = label;
            return i;
        }
    }
    return -1;
}

void eidos_prof_set(const char *label, long long value)
{
    int i;

    if (!eidos_prof_on) {
        return;
    }
    i = census_slot(label);
    if (i >= 0) {
        g_clast[i] = value;
        g_cmode[i] = 0;
    }
}

void eidos_prof_add(const char *label, long long value)
{
    int i;

    if (!eidos_prof_on) {
        return;
    }
    i = census_slot(label);
    if (i >= 0) {
        g_csum[i] += (unsigned long long)value;
        g_cmode[i] = 1;
    }
}

void eidos_prof_scene(const char *name)
{
    char tmp[EIDOS_PROF_SCENE];
    size_t i;

    if (!eidos_prof_on) {
        return;
    }
    if (name == NULL || name[0] == '\0') {
        return;
    }
    for (i = 0; i < sizeof(tmp) - 1u && name[i] != '\0'; i++) {
        tmp[i] = name[i] == ' ' ? '_' : name[i];
    }
    tmp[i] = '\0';
    if (strcmp(g_scene, tmp) != 0) {
        memcpy(g_scene, tmp, i + 1u);
    }
}

void eidos_prof_enable(unsigned every)
{
    g_every = every != 0 ? every : 300;
    eidos_prof_on = 1;
}

unsigned long long eidos_prof_begin(void)
{
    return eidos_prof_on ? bw_clock_ns() : 0;
}

void eidos_prof_end(int phase, unsigned long long t0)
{
    unsigned long long d;

    if (!eidos_prof_on) {
        return;
    }
    d = bw_clock_ns() - t0;
    g_sum[phase] += d;
    if (d > g_max[phase]) {
        g_max[phase] = d;
    }
    g_cur = phase;
}

void eidos_prof_frame(unsigned long long frame_no)
{
    int i, t, k;
    int top[3] = { -1, -1, -1 };
    unsigned long long now;
    unsigned long long tops;
    const char *cur;

    if (!eidos_prof_on) {
        return;
    }
    now = bw_clock_ns();
    if (g_have_prev) {
        /* A frame whose wall exceeds half a second gets one autopsy line
         * per window: the last phase ended and the top-3 buckets by max
         * so far.  The hitch lands jointly across phases, so no single
         * bucket is the whole story. */
        unsigned long long wall = now - g_prev;

        if (wall > 500ULL * 1000 * 1000 && !g_autopsied) {
            for (t = 0; t < 3; t++) {
                int best = -1;

                for (k = 0; k < EIDOS_PROF_COUNT; k++) {
                    if (k == top[0] || k == top[1] || k == top[2]) {
                        continue;
                    }
                    if (best < 0 || g_max[k] > g_max[best]) {
                        best = k;
                    }
                }
                top[t] = best;
            }
            cur = g_cur >= 0 && g_cur < EIDOS_PROF_COUNT ? k_names[g_cur] : "none";
            printf("hitch frame %llu wall %.2f ms phase %s top %s %.2f %s %.2f %s %.2f (max ms)\n",
                   frame_no, (double)wall / 1e6, cur,
                   k_names[top[0]], (double)g_max[top[0]] / 1e6,
                   k_names[top[1]], (double)g_max[top[1]] / 1e6,
                   k_names[top[2]], (double)g_max[top[2]] / 1e6);
            fflush(stdout);
            g_autopsied = 1;
        }
    }
    g_prev = now;
    g_have_prev = 1;
    if (g_n == 0) {
        g_win_start = now;
        g_win_max = 0;
        g_autopsied = 0;
    }
    {
        unsigned long long dt = now - g_frame_t0;

        if (g_frame_t0 != 0 && dt > g_win_max) {
            g_win_max = dt;
        }
        g_frame_t0 = now;
    }
    if (++g_n < g_every) {
        return;
    }
    /* the top-level buckets must sum to the wall; what is missing ran
     * outside every probe (driver waits, uninstrumented glue) */
    tops = g_sum[EIDOS_PROF_GAME] + g_sum[EIDOS_PROF_PRE] + g_sum[EIDOS_PROF_STEP] +
           g_sum[EIDOS_PROF_RENDER] + g_sum[EIDOS_PROF_PUMP] + g_sum[EIDOS_PROF_MOD] +
           g_sum[EIDOS_PROF_PRESENT] + g_sum[EIDOS_PROF_AUDOPEN] +
           g_sum[EIDOS_PROF_AUDIO] + g_sum[EIDOS_PROF_WAIT];
    printf("prof frame %llu (%u frames, %.2f ms/frame wall, %.2f max):", frame_no, g_n,
           (double)(now - g_win_start) / 1e6 / (double)g_n, (double)g_win_max / 1e6);
    for (i = 0; i < EIDOS_PROF_COUNT; i++) {
        printf(" %s %.2f/%.2f", k_names[i],
               (double)g_sum[i] / 1e6 / (double)g_n, (double)g_max[i] / 1e6);
        g_sum[i] = 0;
        g_max[i] = 0;
    }
    printf(" (avg/max ms) unacc %.2f", ((double)(now - g_win_start) - (double)tops) / 1e6 / (double)g_n);
    {
        double wall_s = (double)(now - g_win_start) / 1e9;

        printf(" fps %.2f scene %s memfree %lld",
               wall_s > 0.0 ? (double)g_n / wall_s : 0.0, g_scene, prof_free_mem());
    }
    for (i = 0; i < EIDOS_PROF_CENSUS; i++) {
        if (g_clabel[i] != NULL) {
            if (g_cmode[i] == 0) {
                printf(" %s=%lld", g_clabel[i], g_clast[i]);
            } else {
                printf(" %s=%llu", g_clabel[i], g_csum[i]);
            }
            g_clast[i] = 0;
            g_csum[i] = 0;
        }
    }
    printf("\n");
    fflush(stdout);
    g_n = 0;
}

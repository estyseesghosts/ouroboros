/*
 * host/main.c -- eidos's 64-bit program for the wasm32 build: set up the
 * module's world (host/wasi.c, host/fiber.c), instantiate it and run its
 * main on this thread, which is the module's fiber 0.
 *
 * A trap (an access outside the module's memory, a call through a pointer
 * of the wrong type, an `unreachable`) ends the program with its kind and
 * exit code 3.  The module's own exit is WASI's proc_exit.
 *
 * On Android and iOS SDL owns the process's entry (SDL_main.h turns main
 * into SDL_main) and there is no command line: the arguments are made from
 * the app's own folder (mobile_args).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__SWITCH__)
#define SDL_MAIN_HANDLED 1   /* main() is this file's; libnx's entry calls it */
#endif
#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <pthread.h>
#include <sys/mman.h>
#endif
#if defined(__SWITCH__)
#include <switch.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__SWITCH__)
#define HOST_MOBILE 1
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__SWITCH__)
#include <SDL3/SDL_main.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "host.h"
#include "picker.h"
#include "plat_save.h"
#include "setup.h"
#include "wasm-rt-exceptions.h"
#include "wasm-rt-impl.h"

#if defined(__SWITCH__)
/* libnx's default heap is 512 MiB (its __libnx_initheap chooses the smaller
 * of 512 MiB and what the applet leaves).  One fixed 1 GiB module block
 * plus SDL3 and Mesa does not fit that; title mode has about 3.2 GiB, so
 * ask for 1.5 GiB before libnx starts newlib's heap. */
size_t __nx_heap_size = (size_t)1536 * 1024 * 1024;
#endif

w2c_game g_game;
struct w2c_wasi__snapshot__preview1 g_wasi;
struct w2c_eidos g_eidos;
struct w2c_env g_env;

/* clock(): the process's processor time, which WASI does not have; the
 * module only times its own work with it. */
u64 w2c_env_clock(struct w2c_env *e)
{
    (void)e;
    return (u64)clock() * (1000000u / CLOCKS_PER_SEC);
}

/* The ROM dialog (install/picker.h), the platform's own. */
u32 w2c_eidos_pick_rom(struct w2c_eidos *e, u32 title, u32 out, u32 n)
{
    char buf[1024];
    int rc;

    (void)e;
    buf[0] = 0;
#if defined(HOST_MOBILE) && !defined(__SWITCH__)
    rc = host_pick_rom((const char *)host_ptr(title, 0), buf, sizeof buf);
#else
    rc = bw_pick_rom(NULL, (const char *)host_ptr(title, 0), buf, sizeof buf);
#endif
    if (n > 0) {
        size_t len = strlen(buf);

        if (len >= n) {
            len = n - 1;
        }
        memcpy(host_ptr(out, n), buf, len);
        ((char *)host_ptr(out, n))[len] = 0;
    }
    return (u32)rc;
}

/* A console for the program's messages (sdk/plat_crash.h,
 * bw_plat_console_open): a release build on Windows is a windowed program
 * and has none until `eidos --console` asks. */
void w2c_eidos_console_open(struct w2c_eidos *e)
{
    (void)e;
#if defined(_WIN32)
    if (GetConsoleWindow() != NULL) {
        return;                         /* started from a terminal */
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) {
        return;
    }
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
#endif
}

/* The heap's lock (sdk/wasm_heap.c): one recursive mutex for the module's
 * malloc, from the module's threads and the host's alike.  malloc is the
 * only code that grows the memory, so the lock also keeps a worker's copy
 * of the memory's size (host/par.c) in step: the copy takes the shared
 * size on the way in and hands a grown one back on the way out. */
static SDL_Mutex *g_heap;

void w2c_eidos_heap_lock(struct w2c_eidos *e)
{
    w2c_game *inst = host_instance();

    (void)e;
    SDL_LockMutex(g_heap);
    if (inst != &g_game) {
        inst->w2c_memory = g_game.w2c_memory;
    }
}

void w2c_eidos_heap_unlock(struct w2c_eidos *e)
{
    w2c_game *inst = host_instance();

    (void)e;
    if (inst != &g_game && inst->w2c_memory.pages > g_game.w2c_memory.pages) {
        g_game.w2c_memory = inst->w2c_memory;
    }
    SDL_UnlockMutex(g_heap);
}

/* The module's own malloc and free, which take the lock themselves
 * (sdk/wasm_heap.c, exported as __wrap_malloc and __wrap_free). */
u32 host_malloc(u32 n)
{
    return w2c_game_0x5F_wrap_malloc(&g_game, n);
}

void host_free(u32 a)
{
    w2c_game_0x5F_wrap_free(&g_game, a);
}

/* The time zone (sdk/wasm_host.h's tz_offset): local time minus UTC at
 * `t`, by the platform's own rules, daylight saving included. */
u64 w2c_eidos_tz_offset(struct w2c_eidos *e, u64 t)
{
    time_t tt = (time_t)(long long)t;
    struct tm lt;

    (void)e;
#if defined(_WIN32)
    if (localtime_s(&lt, &tt) != 0) {
        return 0;
    }
    return (u64)(long long)(_mkgmtime(&lt) - tt);
#elif defined(__SWITCH__)
    /* newlib has no timegm.  The console's clock is UTC, which is what the
     * game's date and time screens show before the player changes them. */
    (void)lt;
    (void)tt;
    return 0;
#else
    if (localtime_r(&tt, &lt) == NULL) {
        return 0;
    }
    return (u64)(long long)(timegm(&lt) - tt);
#endif
}

#if defined(HOST_INDIRECT_LOG)
/* The diagnostic build's report of a call through a pointer of another
 * type (tools/wasm2c_patch.py): the caller's name and the target's address
 * in this executable, for addr2line; each pair once. */
void host_indirect_mismatch(const char *caller, void *target)
{
    static struct { const char *caller; void *target; } seen[4096];
    static int n;
    int i;

    for (i = 0; i < n; i++) {
        if (seen[i].caller == caller && seen[i].target == target) {
            return;
        }
    }
    if (n < 4096) {
        seen[n].caller = caller;
        seen[n].target = target;
        n++;
    }
    fprintf(stderr, "indirect: %s -> exe+0x%llx\n", caller,
            (unsigned long long)((char *)target - (char *)GetModuleHandleW(NULL)));
}
#endif

/* A call through a DS code address (tools/wasm2c_patch.py): the module
 * knows which function the build compiled for it.  The lookup runs on the
 * calling thread's own instance and stack, and under the heap's lock, as
 * it keeps a table of the addresses it has reported. */
u32 host_code_index(u32 x)
{
    u32 r;

    SDL_LockMutex(g_heap);
    r = w2c_game_bw_code_index(host_instance(), x);
    SDL_UnlockMutex(g_heap);
    return r;
}

/* The platform's folders (sdk/wasm_host.h's os_path): the same functions
 * a native build calls, compiled here for the platform. */
u32 w2c_eidos_os_path(struct w2c_eidos *e, u32 kind, u32 out, u32 n)
{
    char buf[4096];
    size_t len;
    int rc;

    (void)e;
    switch (kind) {
    case 0:  rc = bw_os_app_home(buf, sizeof buf); break;
    case 1:  rc = bw_os_exe_dir(buf, sizeof buf); break;
    case 2:  rc = bw_plat_default_save_dir(buf, sizeof buf); break;
    default: rc = -1; break;
    }
    len = strlen(buf);
    if (rc != 0 || n == 0 || len >= n) {
        return (u32)-1;
    }
    memcpy(host_ptr(out, n), buf, len + 1);
    return 0;
}

/* A trap: the native frames, as the executable's own addresses, for
 * addr2line (the translated functions carry the module's names). */
void host_trap(wasm_rt_trap_t code)
{
    fprintf(stderr, "eidos: the game stopped (wasm trap: %s)\n", wasm_rt_strerror(code));
#if defined(_WIN32)
    {
        void *frames[48];
        unsigned short n = CaptureStackBackTrace(0, 48, frames, NULL);
        HMODULE self = GetModuleHandleW(NULL);
        unsigned short i;

        for (i = 0; i < n; i++) {
            fprintf(stderr, "  frame %2u  exe+0x%llx\n", i,
                    (unsigned long long)((char *)frames[i] - (char *)self));
        }
    }
#endif
    fflush(stderr);
    exit(3);
}

#if defined(_WIN32)
/* A fault outside a wasm trap (the host's own code, a driver): where it
 * happened and the native frames, the way host_trap reports. */
static LONG WINAPI host_fault(EXCEPTION_POINTERS *ep)
{
    HMODULE self = GetModuleHandleW(NULL);

    fprintf(stderr, "eidos: the program stopped (exception 0x%08lX at exe+0x%llx)\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            (unsigned long long)((char *)ep->ExceptionRecord->ExceptionAddress - (char *)self));
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
            && ep->ExceptionRecord->NumberParameters >= 2) {
        uintptr_t a = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base = (uintptr_t)g_game.w2c_memory.data;

        fprintf(stderr, "eidos: %s 0x%llx (module address 0x%llx)\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read of",
                (unsigned long long)a, (unsigned long long)(a - base));
    }
    host_trap(WASM_RT_TRAP_NONE);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#if defined(_WIN32)
#include <dbghelp.h>

/* EIDOS_HOST_STALL=<seconds>: after that long, the frames the main thread
 * stands in, the way host_trap prints them, and the program ends.  For a
 * run that hangs (a wait no fiber ever ends). */
static HANDLE g_main_thread;

static DWORD WINAPI stall_watch(LPVOID arg)
{
    HMODULE self = GetModuleHandleW(NULL);
    CONTEXT ctx;
    STACKFRAME64 f;
    int i;

    Sleep((DWORD)(uintptr_t)arg * 1000u);
    SuspendThread(g_main_thread);
    memset(&ctx, 0, sizeof ctx);
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(g_main_thread, &ctx);
    memset(&f, 0, sizeof f);
    f.AddrPC.Offset = ctx.Rip;
    f.AddrPC.Mode = AddrModeFlat;
    f.AddrStack.Offset = ctx.Rsp;
    f.AddrStack.Mode = AddrModeFlat;
    f.AddrFrame.Offset = ctx.Rbp;
    f.AddrFrame.Mode = AddrModeFlat;
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    fprintf(stderr, "eidos: the main thread after %u s:\n", (unsigned)(uintptr_t)arg);
    for (i = 0; i < 48 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(),
                                       g_main_thread, &f, &ctx, NULL,
                                       SymFunctionTableAccess64, SymGetModuleBase64, NULL); i++) {
        fprintf(stderr, "  frame %2d  exe+0x%llx\n", i,
                (unsigned long long)(f.AddrPC.Offset - (DWORD64)(uintptr_t)self));
    }
    fflush(stderr);
    ExitProcess(4);
    return 0;
}
#endif

#if defined(__SWITCH__)
/* A small trace before SDL and the module start, for a report from a
 * console with no nxlink: sdmc:/switch/eidos/boot.txt says how far the
 * program got.  A crash before main leaves no file, which is itself the
 * answer (libnx's heap request failed). */
static int g_boot_logged;

void switch_pin_core(int core)
{
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);

    if (R_FAILED(rc)) {
        fprintf(stderr, "eidos: core %d not set (0x%x)\n", core, (unsigned)rc);
    }
}

void switch_boot_stage(const char *stage)
{
    FILE *f = fopen("sdmc:/switch/eidos/boot.txt", g_boot_logged ? "a" : "w");

    if (f != NULL) {
        g_boot_logged = 1;
        fprintf(f, "%s\n", stage);
        fclose(f);
    }
}

/* Title mode only: an applet has too little memory for the module, Mesa and
 * the GPU's buffers.  The message is libnx's console, which needs no window
 * and survives an applet's smaller heap. */
static int switch_application_mode(void)
{
    PadState pad;

    if (appletGetAppletType() == AppletType_Application) {
        return 1;
    }
    consoleInit(NULL);
    printf("eidos needs the whole console.\n\n"
           "Hold R while starting a game, then choose eidos.\n\n"
           "Press A to exit.\n");
    consoleUpdate(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    for (;;) {
        padUpdate(&pad);
        if ((padGetButtonsDown(&pad) & HidNpadButton_A) != 0) {
            break;
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
#endif

#if defined(HOST_MOBILE)
/* The game's arguments on a phone: its data and its profile in the app's
 * own folder (SDL_GetPrefPath: Android's internal files, iOS's sandbox),
 *
 *   <folder>/extract/    the extracted game data (docs/installer.md)
 *   <folder>/profile/    settings and saves
 *
 * and the one-screen layout in the game's window.  Without game data the
 * first-run window asks for the ROM through the system's file list
 * (host/pick_mobile.c) and extracts it there.  Lines of <folder>/args.txt are added after these, and <folder>/env.txt's
 * NAME=value lines set the environment (the EIDOS_* switches), since
 * neither system gives an app a command line.  The program's messages go
 * to <folder>/stderr.txt (to the console with EIDOS_MOBILE_CONSOLE set,
 * as `xcrun simctl launch --console` can). */
static char **mobile_args(int *argc)
{
    static char *argv[64];
    static char data[1024], prof[1024], file[1024], line[1024];
#if defined(__SWITCH__)
    /* The GL presenter (mesa-switch) is the Switch renderer.  env.txt can
     * still say EIDOS_RENDERER=sdl or gl. */
    if (getenv("EIDOS_RENDERER") == NULL) {
        setenv("EIDOS_RENDERER", "gl", 1);
    }
#endif
#if defined(__SWITCH__)
    /* SDL's Switch filesystem driver puts the preference path under
     * sdmc:/switch; this is the folder the README tells players to use, and
     * naming it here keeps it independent of SDL's own default. */
    const char *pref = "sdmc:/switch/eidos/";
#else
    const char *pref = SDL_GetPrefPath("eidos", "eidos");
#endif
    int n = 0;
    FILE *f;

    if (pref == NULL) {
        pref = "./";
    }
#if defined(__SWITCH__)
    SDL_CreateDirectory("sdmc:/switch/eidos");
#endif
    snprintf(file, sizeof file, "%sstderr.txt", pref);
    if (getenv("EIDOS_MOBILE_CONSOLE") == NULL) {
        /* The output descriptors themselves, which the game's writes go to:
         * an app started by a tool (xcrun devicectl) has them on a pipe no
         * one reads, and a full pipe stopped the game in its next message.
         * The file, or nowhere. */
        int fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd < 0) {
            fd = open("/dev/null", O_WRONLY);
        }
        if (fd >= 0) {
            fflush(stdout);
            fflush(stderr);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2) {
                close(fd);
            }
        }
        setvbuf(stderr, NULL, _IONBF, 0);
    }
#if defined(__SWITCH__)
    switch_boot_stage("stderr redirected");
#endif
    snprintf(data, sizeof data, "%sextract", pref);
    snprintf(prof, sizeof prof, "%sprofile", pref);
    SDL_CreateDirectory(prof);
    argv[n++] = "eidos";
    argv[n++] = "--no-window";
    argv[n++] = "--data-root";
    argv[n++] = data;
    argv[n++] = "--profile-dir";
    argv[n++] = prof;
    argv[n++] = "--layout";
    argv[n++] = "one";
#if defined(__SWITCH__)
    /* Handheld first: 1x is the S9 default until a profile says 2x holds
     * the frame rate.  The in-game setting still overrides this. */
    argv[n++] = "--resolution";
    argv[n++] = "1";
#endif
    argv[n++] = "--window";
#if !defined(__SWITCH__)
    /* A phone draws with OpenGL ES and is played by touch: the hints show the DS's own button
     * letters (a connected pad still shows its own).  Android's curved
     * screens also get a margin of 2% a side inside the safe area.  Not
     * the Switch: it is played with buttons, and EIDOS_GL_ES's fixed
     * 720-row 3D would also hold a docked console's 1080p. */
    setenv("EIDOS_TOUCH_DEVICE", "1", 1);
    /* the 3D renderer's choices for OpenGL ES (engine/platform/plat_gl3d.c) */
    setenv("EIDOS_GL_ES", "1", 1);
#endif
#if defined(__ANDROID__)
    setenv("EIDOS_EDGE_MARGIN", "2", 1);
#endif
    snprintf(file, sizeof file, "%senv.txt", pref);
    if ((f = fopen(file, "r")) != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            char *eq = strchr(line, '=');

            line[strcspn(line, "\r\n")] = 0;
            if (eq != NULL && line[0] != '#') {
                *eq = 0;
                setenv(line, eq + 1, 1);
            }
        }
        fclose(f);
    }
    snprintf(file, sizeof file, "%sargs.txt", pref);
    if ((f = fopen(file, "r")) != NULL) {
        while (n < 63 && fgets(line, sizeof line, f) != NULL) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] != 0 && line[0] != '#') {
                argv[n++] = SDL_strdup(line);
            }
        }
        fclose(f);
    }
    argv[n] = NULL;
    *argc = n;
#if !defined(__ANDROID__) && !defined(__SWITCH__)
    host_ios_view_setup();
#endif
    /* the DS screens side by side are wider than tall: landscape only */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    return argv;
}
#endif

static int host_run(int argc, char **argv);

#if defined(__ANDROID__)
/* The game runs on a thread of its own whose stack holds every fiber's
 * (host/fiber.c says why). */
static int g_run_argc, g_run_rc;
static char **g_run_argv;

static void *game_thread(void *p)
{
    (void)p;
    /* Attached to the Java VM before any fiber has a stack: attaching
     * releases the pages of the thread's stack below where it runs, which
     * would empty the fibers' first frames. */
    SDL_GetAndroidJNIEnv();
    g_run_rc = host_run(g_run_argc, g_run_argv);
    return NULL;
}

static int run_on_fiber_stack(int argc, char **argv)
{
    size_t size = host_fiber_region_size();

    void *region = mmap(NULL, size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    pthread_attr_t attr;
    pthread_t t;

    if (region == MAP_FAILED) {
        fprintf(stderr, "eidos: no room for the fibers' stacks\n");
        return 1;
    }
    host_fiber_region(region);
    g_run_argc = argc;
    g_run_argv = argv;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, region, size);
    if (pthread_create(&t, &attr, game_thread, NULL) != 0) {
        fprintf(stderr, "eidos: no game thread\n");
        return 1;
    }
    pthread_join(t, NULL);
    return g_run_rc;
}
#endif

#if defined(__SWITCH__)
/* The loader's main-thread stack is not a known size, and the module's
 * first NitroMain frames run on it before any fiber exists.  Give host_run
 * an 8 MiB thread of its own, as the Android path does with its fiber
 * region; SDL's thread stack size is what libnx's pthreads uses. */
static int g_switch_argc, g_switch_rc;
static char **g_switch_argv;

static int SDLCALL switch_game_thread(void *p)
{
    (void)p;
    switch_pin_core(0);     /* the compositor workers have 1 and 2 */
    g_switch_rc = host_run(g_switch_argc, g_switch_argv);
    return 0;
}

static int run_on_switch_thread(int argc, char **argv)
{
    SDL_PropertiesID props;
    SDL_Thread *t;

    g_switch_argc = argc;
    g_switch_argv = argv;
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                           (void *)switch_game_thread);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "eidos-game");
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER,
                          8 * 1024 * 1024);
    t = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (t == NULL) {
        fprintf(stderr, "eidos: no game thread (%s); running on the main stack\n",
                SDL_GetError());
        return host_run(argc, argv);
    }
    SDL_WaitThread(t, NULL);
    return g_switch_rc;
}
#endif

int main(int argc, char **argv)
{
#if defined(__SWITCH__)
    /* The CPU boost the OS offers a game: on from the start, free cycles
     * for the module. */
    appletSetCpuBoostMode(true);
    switch_boot_stage("main");
    socketInitializeDefault();
    SDL_SetMainReady();
    if (!switch_application_mode()) {
        socketExit();
        return 0;
    }
    switch_boot_stage("title mode");
    if (!SDL_Init(0)) {
        switch_boot_stage("SDL_Init(0) failed");
        fprintf(stderr, "eidos: SDL_Init(0): %s\n", SDL_GetError());
    }
#endif
#if defined(HOST_MOBILE)
    (void)argc;
    (void)argv;
    argv = mobile_args(&argc);
#endif
#if defined(__SWITCH__)
    switch_boot_stage("arguments");
    fprintf(stderr, "host: stderr ok\n");
    fflush(stderr);
    if (getenv("EIDOS_NXLINK") != NULL) {
        nxlinkStdio();
    }
#endif
#if defined(__ANDROID__)
    return run_on_fiber_stack(argc, argv);
#elif defined(__SWITCH__)
    return run_on_switch_thread(argc, argv);
#else
    {
        int rc = host_run(argc, argv);

#if defined(__SWITCH__)
        switch_boot_stage("host_run returned");
        socketExit();
#endif
        return rc;
    }
#endif
}

static int host_run(int argc, char **argv)
{
#if defined(_WIN32)
    if (getenv("EIDOS_HOST_STALL") != NULL) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                        &g_main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CreateThread(NULL, 0, stall_watch,
                     (LPVOID)(uintptr_t)atoi(getenv("EIDOS_HOST_STALL")), 0, NULL);
    }
#endif
#if defined(_WIN32)
    SetUnhandledExceptionFilter(host_fault);
#endif
#if defined(__SWITCH__)
    /* Before a mutex: SDL's own thread support is what a plain album or
     * title start may not have set up yet. */
    switch_boot_stage("host_run: SDL_Init");
    if (!SDL_Init(0)) {
        switch_boot_stage("host_run: SDL_Init failed");
        fprintf(stderr, "eidos: SDL_Init: %s\n", SDL_GetError());
    }
#endif
#if defined(__SWITCH__)
    switch_boot_stage("host_run: mutex");
#endif
    g_heap = SDL_CreateMutex();
#if defined(__SWITCH__)
    if (g_heap == NULL) {
        switch_boot_stage("host_run: mutex failed");
        fprintf(stderr, "eidos: SDL_CreateMutex: %s\n", SDL_GetError());
    }
    switch_boot_stage("host_run: wasm_rt_init");
#endif
    wasm_rt_init();
#if defined(__SWITCH__)
    switch_boot_stage("host_run: wasi_init");
#endif
    host_wasi_init();
#if defined(__SWITCH__)
    switch_boot_stage("host_run: fibers_init");
#endif
    host_fibers_init();
    g_wasi.argc = argc;
    g_wasi.argv = argv;
#if defined(__SWITCH__)
    switch_boot_stage("host_run: instantiate");
#endif
    wasm2c_game_instantiate(&g_game, &g_eidos, &g_env, &g_wasi);
    /* a trap ends the program in host_trap (host/CMakeLists.txt) */
#if defined(__SWITCH__)
    switch_boot_stage("host_run: start");
#endif
    w2c_game_0x5Fstart(&g_game);
    wasm2c_game_free(&g_game);
    wasm_rt_free();
    return 0;
}

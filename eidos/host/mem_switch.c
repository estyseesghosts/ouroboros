/*
 * host/mem_switch.c -- the module's linear memory on the Switch, allocated
 * once and never moved.
 *
 * wasm2c's own memory growth calls calloc and realloc (WASM_RT_USE_MMAP=0,
 * the only choice on a system without mmap), and a realloc that moves the
 * block would invalidate every SDL surface made from module pixels
 * (host/sdl.c).  The Switch build therefore gives wasm-rt-mem-impl.c these
 * three functions instead: one fixed block is made at the first
 * allocation, growth returns the same block, and a grow past the budget
 * fails the way a wasm memory.grow does.
 *
 * The budget is 1 GiB by default and EIDOS_SWITCH_MEM_MB overrides it for a
 * report.  sdk/armdata.c's and this file's budget numbers are in
 * switch/README.md.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEM_BUDGET_DEFAULT_MB 1024u

static unsigned char *g_mem;
static size_t g_size;

static size_t budget(void)
{
    const char *s = getenv("EIDOS_SWITCH_MEM_MB");
    unsigned long mb = s != NULL ? strtoul(s, NULL, 10) : 0;

    if (mb == 0) {
        mb = MEM_BUDGET_DEFAULT_MB;
    }
    return (size_t)mb * 1024u * 1024u;
}

void *eidos_switch_mem_calloc(size_t n, size_t size)
{
    size_t bytes;

    if (size != 0 && n > SIZE_MAX / size) {
        return NULL;
    }
    bytes = n * size;
    if (bytes > budget()) {
        fprintf(stderr, "eidos: module memory budget reached (%zu bytes)\n", bytes);
        return NULL;
    }
    if (g_mem == NULL) {
        g_mem = aligned_alloc(4096, budget());
        if (g_mem == NULL) {
            fprintf(stderr, "eidos: no room for the module's %zu MiB (heap too small?)\n",
                    budget() / (1024u * 1024u));
            fflush(stderr);
            exit(1);            /* wasm2c does not check this calloc's result */
        }
        g_size = bytes;
        memset(g_mem, 0, bytes);
    } else if (bytes > g_size) {
        memset(g_mem + g_size, 0, bytes - g_size);
        g_size = bytes;
    }
    return g_mem;
}

void *eidos_switch_mem_realloc(void *p, size_t size)
{
    if (p == NULL) {
        return eidos_switch_mem_calloc(1, size);
    }
    if (p != g_mem || size > budget()) {
        fprintf(stderr, "eidos: module memory budget reached (%zu bytes)\n", size);
        return NULL;
    }
    if (size > g_size) {
        memset(g_mem + g_size, 0, size - g_size);
        g_size = size;
    }
    return g_mem;
}

void eidos_switch_mem_free(void *p)
{
    if (p == g_mem) {
        free(g_mem);
        g_mem = NULL;
        g_size = 0;
    }
}

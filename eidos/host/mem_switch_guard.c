/*
 * host/mem_switch_guard.c -- the module's linear memory with guard pages on
 * the Switch.
 *
 * The fixed-block path (host/mem_switch.c) lets wasm2c check every memory
 * access, which is roughly twice the cost of the native port.  Here the
 * whole budget is reserved in the process's virtual address space with
 * libnx's virtmem, and physical pages are mapped in as the module grows:
 * an access past the mapped part faults instead of being checked, the same
 * trade the desktop's guard pages make (docs/lp64.md, "Guard pages").
 *
 * svcMapMemory comes from libnx.  Its source range is borrowed and becomes
 * inaccessible, so the large physical block is allocated once and never
 * freed through the C heap; the module's memory is the destination range.
 * EIDOS_SWITCH_MEM_MB caps the mapped budget, as in the fixed-block path.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "wasm-rt.h"

#define MEM_BUDGET_DEFAULT_MB 1024u

static unsigned char *g_phys;       /* the borrowed physical pages          */
static unsigned char *g_virt;       /* the reserved, mapped address range  */
static size_t g_mapped;
static VirtmemReservation *g_rv;
static int g_guard;                 /* 1: g_virt is a reserved range       */

static size_t budget(void)
{
    const char *s = getenv("EIDOS_SWITCH_MEM_MB");
    unsigned long mb = s != NULL ? strtoul(s, NULL, 10) : 0;

    if (mb == 0) {
        mb = MEM_BUDGET_DEFAULT_MB;
    }
    return (size_t)mb * 1024u * 1024u;
}

static int map_at(unsigned char *dst, size_t off, size_t size)
{
    if (!g_guard) {
        return 0;
    }
    if (R_FAILED(svcMapMemory(dst + off, g_phys + off, size))) {
        fprintf(stderr, "eidos: svcMapMemory(%zu) failed\n", size);
        return -1;
    }
    return 0;
}

void wasm_rt_allocate_memory(wasm_rt_memory_t *mem, uint64_t initial_pages,
                             uint64_t max_pages, bool is64, uint32_t page_size)
{
    size_t initial = (size_t)(initial_pages * page_size);
    size_t max = (size_t)(max_pages * page_size);
    size_t cap = budget();

    if (max > cap) {
        max = cap;
        max_pages = max / page_size;
    }
    mem->size = initial;
    mem->pages = initial_pages;
    mem->max_pages = max_pages;
    mem->is64 = is64;
    mem->page_size = page_size;

    g_phys = (unsigned char *)aligned_alloc(4096, max);
    if (g_phys == NULL) {
        fprintf(stderr, "eidos: no room for the module's %zu MiB\n",
                max / (1024u * 1024u));
        fflush(stderr);
        exit(1);
    }
    memset(g_phys, 0, initial);

    g_mapped = initial;
    g_guard = 0;
    virtmemLock();
    /* svcMapMemory only maps into the stack region (alias region on old
     * 36-bit address spaces); an ASLR-region address is refused, which is
     * why the first 256 MiB map failed.  libnx's stack finder is the one
     * meant for svcMapMemory destinations. */
    g_virt = (unsigned char *)virtmemFindStack(max, 0x4000);
    if (g_virt != NULL) {
        g_rv = virtmemAddReservation(g_virt, max);
        if (g_rv != NULL) {
            g_guard = 1;
        }
    }
    virtmemUnlock();
    if (!g_guard) {
        /* No virtual reservation: keep the fixed block.  Accesses have no
         * guard, but the module's own bounds are still the 256 MiB it
         * declares; this is the fallback, not the fast path. */
        mem->data = g_phys;
        return;
    }
    if (map_at(g_virt, 0, initial) != 0) {
        fprintf(stderr, "eidos: guard pages unavailable; using the fixed block\n");
        g_guard = 0;
        mem->data = g_phys;
        return;
    }
    mem->data = g_virt;
}

uint64_t wasm_rt_grow_memory(wasm_rt_memory_t *mem, uint64_t delta)
{
    uint64_t old_pages = mem->pages;
    uint64_t new_pages = old_pages + delta;
    size_t old_size = (size_t)(old_pages * mem->page_size);
    size_t new_size;

    if (delta == 0) {
        return old_pages;
    }
    if (new_pages < old_pages || new_pages > mem->max_pages) {
        return (uint64_t)-1;
    }
    new_size = (size_t)(new_pages * mem->page_size);
    if (g_guard) {
        if (map_at(g_virt, old_size, new_size - old_size) != 0) {
            return (uint64_t)-1;
        }
    } else {
        memset((unsigned char *)mem->data + old_size, 0, new_size - old_size);
    }
    mem->pages = new_pages;
    mem->size = new_size;
    g_mapped = new_size;
    return old_pages;
}

void wasm_rt_free_memory(wasm_rt_memory_t *mem)
{
    if (g_guard && g_virt != NULL) {
        (void)svcUnmapMemory(g_virt, g_phys, g_mapped);
    }
    if (g_rv != NULL) {
        virtmemLock();
        virtmemRemoveReservation(g_rv);
        virtmemUnlock();
        g_rv = NULL;
    }
    free(g_phys);
    g_phys = NULL;
    g_virt = NULL;
    mem->data = NULL;
}

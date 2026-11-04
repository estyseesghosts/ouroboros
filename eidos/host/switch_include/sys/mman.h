/*
 * host/switch_include/sys/mman.h -- libnx has no mmap, but wasm2c's runtime
 * includes <sys/mman.h> before its `#if WASM_RT_USE_MMAP`; the Switch build
 * sets that to 0 and uses host/mem_switch.c's fixed block instead.  This
 * shim only has to let the include succeed.
 */

#ifndef EIDOS_SWITCH_SYS_MMAN_H
#define EIDOS_SWITCH_SYS_MMAN_H

#endif

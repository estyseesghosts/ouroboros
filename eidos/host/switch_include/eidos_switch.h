/*
 * host/switch_include/eidos_switch.h -- newlib on libnx has setjmp and
 * longjmp but no sigsetjmp or siglongjmp.  wasm2c's wasm-rt.h uses the
 * sig-prefixed pair on every non-Windows host, including the Switch.
 * Forced into every Switch source with -include.
 */

#ifndef EIDOS_SWITCH_H
#define EIDOS_SWITCH_H

#include <setjmp.h>

#define sigsetjmp(buf, savemask) setjmp(buf)
#define siglongjmp(buf, val) longjmp(buf, val)

#endif

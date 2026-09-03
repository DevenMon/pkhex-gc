/* Host stub: enough of libogc's EXI interface for the port scan to compile
 * away from devkitPPC. The real header comes from libogc on the console. */
#ifndef PKHEXGC_STUB_OGC_EXI_H
#define PKHEXGC_STUB_OGC_EXI_H
#include <gccore.h>
s32 EXI_GetID(s32 chan, s32 dev, u32 *id);
#endif

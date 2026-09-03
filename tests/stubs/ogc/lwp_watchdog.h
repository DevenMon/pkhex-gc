#ifndef TEST_LWP_WATCHDOG_H
#define TEST_LWP_WATCHDOG_H
#include <stdint.h>
typedef uint64_t u64;
static inline u64 gettime(void) { return 0; }
static inline uint32_t diff_msec(u64 start, u64 end) { (void)start; (void)end; return 0; }
#endif

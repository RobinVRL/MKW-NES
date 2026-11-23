#pragma once
/* newlib (devkitARM) lock types that libctru's synchronization.h builds on.
   glibc has no equivalent header; layout matches devkitARM's sys/lock.h. */
#include <stdint.h>
typedef int32_t _LOCK_T;
typedef struct {
    _LOCK_T lock;
    uint32_t thread_tag;
    uint32_t counter;
} _LOCK_RECURSIVE_T;
typedef uint32_t _COND_T;

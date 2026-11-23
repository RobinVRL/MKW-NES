// Force-included in the x86-64 test build only (port/host.sh).
#pragma once
#if !defined(__arm__)
#define __builtin_arm_get_fpscr() 0u
#define __builtin_arm_set_fpscr(x) ((void)(x))
#endif

#pragma once
// libctru's sys/ioctl.h; on Linux the real header provides ioctl and FIONBIO.
#ifdef __linux__
#include_next <sys/ioctl.h>
#else
#define FIONBIO		1

#ifdef __cplusplus
extern "C" {
#endif

	int	ioctl(int fd, int request, ...);

#ifdef __cplusplus
}
#endif
#endif

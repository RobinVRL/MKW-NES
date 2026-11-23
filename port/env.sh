# Shared cross-compile settings for the NES/SNES Classic (Allwinner R16, Cortex-A7, ARMv7-A hard-float).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="$ROOT/port"
SRC="$ROOT/src3ds"
CROSS="${CROSS:-arm-linux-gnueabihf-}"
ARCH="-march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm"
INCS="-I$PORT/ctru/include -I$PORT/include -I$SRC/vendor/citro3d/include -I$SRC/vendor/cfl-mii/include -I$SRC/vendor/kinoko/include -I$SRC/vendor/kinoko/source -I$SRC/include -I$ROOT/build/gen"
DEFS="-D__3DS__ -DMKW_NESMINI -DREVOLUTION -D_GNU_SOURCE"

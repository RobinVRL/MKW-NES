#!/bin/bash
# Lists every libctru / citro3d symbol referenced by the game sources.
cd "$(dirname "$0")/../src3ds"
grep -rohE '\b(C3D|C2D|Tex3DS|gfx|apt|hid|os|svc|Light|Recursive|CondVar|linear|vram|ndsp|ptm|mcu|cfg|fs|FS|GPU|GX|shader|DVLB|romfs|thread|APT|gsp|GSP|KEY|NDSP|CFG|PTMU|MCU|Mtx|FVec|FQuat|Thread|Handle|Result|R_|TOUCH|touch|circle|irrst|ac|news|mii|MII|frd|am|cpu)[A-Za-z0-9_]*' src include vendor/cfl-mii/source vendor/cfl-mii/include \
  | sort | uniq -c | sort -rn | awk '{print $2}' | tr '\n' ' '
echo

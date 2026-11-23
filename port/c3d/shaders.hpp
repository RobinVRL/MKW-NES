#pragma once
#include "backend.hpp"
#include <cstring>
#include <string>

namespace c3d {
struct FragKey {
    struct Stage { u16 srcRgb, srcAlpha, opRgb, opAlpha; u8 funcRgb, funcAlpha, scaleRgb, scaleAlpha; };
    Stage stage[6];
    u32 bufUpdate;
    u8 program, alphaTest, alphaFunc, projective, lights, d0, d0Abs, pad;
    u8 textureMask() const;
    bool operator<(const FragKey& o) const { return memcmp(this, &o, sizeof *this) < 0; }
};
int uniformVectors(int program);
int boolCount(int program);
bool programLit(int program);
std::string vertexSource(int program);
std::string fragmentSource(const FragKey& k);
}

// libctru's DVLB/shaderProgram API for the GLESDVLB tag blobs written by
// port/gen.py. Each DVLE carries its program id in mainOffset, and uniform
// tables list the registers picasso assigns in declaration order, so
// shaderInstanceGetUniformLocation returns the same values as on the 3DS.
#include "backend.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Unif { const char* name; int reg; int count; bool isBool; };

// src/mkw.v.pica declarations (shared by the Mii variant)
const Unif kScene[] = {
    {"projection", 0, 4, false}, {"modelView", 4, 4, false}, {"light", 8, 1, false}, {"uvScroll", 9, 1, false},
    {"envRow0", 10, 1, false}, {"envRow1", 11, 1, false}, {"envRow2", 12, 1, false}, {"envParam", 13, 1, false},
    {"normalRow0", 14, 1, false}, {"normalRow1", 15, 1, false}, {"normalRow2", 16, 1, false},
    {"uvRow0", 17, 1, false}, {"uvRow1", 18, 1, false}, {"uvRow2", 19, 1, false}, {"uvRow3", 20, 1, false},
    {"tint", 21, 1, false}, {"uv1Row0", 22, 1, false}, {"uv1Row1", 23, 1, false},
    {"lightDiff", 24, 1, false}, {"lightDir", 25, 1, false}, {"lightFn", 26, 1, false},
    {"shadowDepth", 27, 1, false}, {"shadowFloor", 28, 1, false}, {"shadowReceiverFade", 29, 1, false},
    {"fogParam", 30, 1, false},
    {"retailLightTexture", 0, 1, true},
    {"environmentMapped", 1, 1, true}, {"environmentTexture2", 2, 1, true}, {"environmentPrimary", 3, 1, true},
    {"diffuseLighting", 4, 1, true}, {"projectedWater", 5, 1, true}, {"playerShadowProject", 6, 1, true},
    {"shadowCapture", 7, 1, true},
    {"bones", 31, 57, false},
    {"skinned", 8, 1, true}, {"skinnedNormals", 9, 1, true}, {"staticLighting", 10, 1, true},
};
const Unif kEffect[] = {{"projection", 0, 4, false}};
const Unif kCfl[] = {{"projection", 0, 4, false}, {"modelView", 4, 4, false}};

void fillDvle(DVLE_s& d, int program) {
    memset(&d, 0, sizeof d);
    d.type = VERTEX_SHDR;
    d.mainOffset = u32(program);
    const Unif* table = kScene;
    size_t n = sizeof kScene / sizeof kScene[0];
    if (program == c3d::ProgEffect) { table = kEffect; n = 1; }
    else if (program == c3d::ProgCfl) { table = kCfl; n = 2; }
    std::string symbols;
    auto* entries = static_cast<DVLE_uniformEntry_s*>(calloc(n, sizeof(DVLE_uniformEntry_s)));
    for (size_t i = 0; i < n; ++i) {
        entries[i].symbolOffset = u32(symbols.size());
        symbols += table[i].name;
        symbols += '\0';
        const int base = table[i].isBool ? 0x78 : 0x10;
        entries[i].startReg = u16(base + table[i].reg);
        entries[i].endReg = u16(base + table[i].reg + table[i].count - 1);
    }
    char* sym = static_cast<char*>(malloc(symbols.size()));
    memcpy(sym, symbols.data(), symbols.size());
    d.uniformTableSize = u32(n);
    d.uniformTableData = entries;
    d.symbolTableData = sym;
}
}

DVLB_s* DVLB_ParseFile(u32* shbinData, u32 shbinSize) {
    if (!shbinData || shbinSize < 16 || memcmp(shbinData, "GLESDVLB", 8) != 0) return nullptr;
    const u32 count = shbinData[2], first = shbinData[3];
    if (!count || first + count > c3d::ProgCount) return nullptr;
    auto* b = static_cast<DVLB_s*>(calloc(1, sizeof(DVLB_s)));
    b->numDVLE = count;
    b->DVLE = static_cast<DVLE_s*>(calloc(count, sizeof(DVLE_s)));
    for (u32 i = 0; i < count; ++i) {
        fillDvle(b->DVLE[i], int(first + i));
        b->DVLE[i].dvlp = &b->DVLP;
    }
    return b;
}
void DVLB_Free(DVLB_s* dvlb) {
    if (!dvlb) return;
    for (u32 i = 0; i < dvlb->numDVLE; ++i) {
        free(dvlb->DVLE[i].uniformTableData);
        free(dvlb->DVLE[i].symbolTableData);
    }
    free(dvlb->DVLE);
    free(dvlb);
}
s8 DVLE_GetUniformRegister(DVLE_s* dvle, const char* name) {
    if (!dvle || !name) return -1;
    for (u32 i = 0; i < dvle->uniformTableSize; ++i)
        if (!strcmp(dvle->symbolTableData + dvle->uniformTableData[i].symbolOffset, name))
            return s8(dvle->uniformTableData[i].startReg);
    return -1;
}
void DVLE_GenerateOutmap(DVLE_s*) {}

Result shaderInstanceInit(shaderInstance_s* si, DVLE_s* dvle) {
    if (!si || !dvle) return -1;
    memset(si, 0, sizeof *si);
    si->dvle = dvle;
    return 0;
}
Result shaderInstanceFree(shaderInstance_s* si) {
    if (!si) return -1;
    free(si->float24Uniforms);
    free(si);
    return 0;
}
Result shaderInstanceSetBool(shaderInstance_s* si, int id, bool value) {
    if (!si || id < 0 || id > 15) return -1;
    si->boolUniforms = u16((si->boolUniforms & ~(1 << id)) | (value << id));
    return 0;
}
Result shaderInstanceGetBool(shaderInstance_s* si, int id, bool* value) {
    if (!si || !value || id < 0 || id > 15) return -1;
    *value = (si->boolUniforms >> id) & 1;
    return 0;
}
s8 shaderInstanceGetUniformLocation(shaderInstance_s* si, const char* name) {
    if (!si || !si->dvle || !name) return -1;
    const DVLE_s* d = si->dvle;
    for (u32 i = 0; i < d->uniformTableSize; ++i)
        if (!strcmp(d->symbolTableData + d->uniformTableData[i].symbolOffset, name))
            return s8(d->uniformTableData[i].startReg - 0x10);
    return -1;
}
Result shaderProgramInit(shaderProgram_s* sp) {
    if (!sp) return -1;
    memset(sp, 0, sizeof *sp);
    return 0;
}
Result shaderProgramFree(shaderProgram_s* sp) {
    if (!sp) return -1;
    if (sp->vertexShader) shaderInstanceFree(sp->vertexShader);
    if (sp->geometryShader) shaderInstanceFree(sp->geometryShader);
    sp->vertexShader = sp->geometryShader = nullptr;
    return 0;
}
Result shaderProgramSetVsh(shaderProgram_s* sp, DVLE_s* dvle) {
    if (!sp || !dvle || dvle->type != VERTEX_SHDR) return -1;
    if (sp->vertexShader) shaderInstanceFree(sp->vertexShader);
    sp->vertexShader = static_cast<shaderInstance_s*>(malloc(sizeof(shaderInstance_s)));
    return shaderInstanceInit(sp->vertexShader, dvle);
}
Result shaderProgramSetGsh(shaderProgram_s*, DVLE_s*, u8) { return -1; }
Result shaderProgramSetGshInputPermutation(shaderProgram_s*, u64) { return 0; }
Result shaderProgramConfigure(shaderProgram_s*, bool, bool) { return 0; }
Result shaderProgramUse(shaderProgram_s* sp) { C3D_BindProgram(sp); return 0; }

// GLSL ES 1.00 translations of the PICA vertex shaders, and the generator for
// the fragment pipeline (PICA TexEnv combiners, alpha test, fragment lighting).
//
// Vertex shaders: line-by-line ports of src/mkw.v.pica (scene), its Mii
// variant from tools/mii_shader.py, src/effect.v.pica and cfl-mii's
// vshader.v.pica. Uniform vectors live in c[] at the registers picasso
// assigns (declaration order); bools in ub[] in the same order.
//
// The PICA output depth z/w in [-1,0] is mapped by C3D_DepthMap to
// d = zScale*z/w + zOffset in [0,1]; GL window depth is (z_ndc+1)/2, so
// gl_Position.z = (2d-1)w = 2*zScale*z + (2*zOffset-1)*w.
#include "shaders.hpp"
#include <cstdio>
#include <string>

namespace c3d {

static const char* kVertexCommon = R"(
precision highp float;
attribute vec4 a0; attribute vec4 a1; attribute vec4 a2; attribute vec4 a3; attribute vec4 a4; attribute vec4 a5;
uniform vec2 uDepth;
varying vec3 vTc0; // xy + projective w
varying vec2 vTc1;
varying vec2 vTc2;
varying vec4 vColor;
#ifdef LIT
varying vec3 vView;
varying vec3 vNormal;
#endif
vec4 depthMap(vec4 p) { p.z = 2.0 * uDepth.x * p.z + (2.0 * uDepth.y - 1.0) * p.w; return p; }
)";

// src/mkw.v.pica (+ tools/mii_shader.py tail when MII is defined)
static const char* kSceneVS = R"(
uniform vec4 c[88];
uniform bool ub[11];
#define projection0 c[0]
#define projection1 c[1]
#define projection2 c[2]
#define projection3 c[3]
#define envRow0 c[10]
#define envRow1 c[11]
#define envRow2 c[12]
#define envParam c[13]
#define normalRow0 c[14]
#define normalRow1 c[15]
#define normalRow2 c[16]
#define uvRow0 c[17]
#define uvRow1 c[18]
#define uvRow2 c[19]
#define uvRow3 c[20]
#define tint c[21]
#define uv1Row0 c[22]
#define uv1Row1 c[23]
#define lightDiff c[24]
#define lightDir c[25]
#define lightFn c[26]
#define shadowDepth c[27]
#define shadowFloor c[28]
#define shadowReceiverFade c[29]
#define fogParam c[30]
#define retailLightTexture ub[0]
#define environmentMapped ub[1]
#define environmentTexture2 ub[2]
#define environmentPrimary ub[3]
#define diffuseLighting ub[4]
#define projectedWater ub[5]
#define playerShadowProject ub[6]
#define shadowCapture ub[7]
#define skinned ub[8]
#define skinnedNormals ub[9]
#define staticLighting ub[10]
const float normalEpsilon = 0.000000000001;
vec3 safeNormalize(vec3 v) { return v * inversesqrt(max(normalEpsilon, dot(v, v))); }
void main() {
    vec3 r15 = a1.xyz;
    vec4 r0 = vec4(a0.xyz, 1.0);
    if (skinned) {
        int bone = 31 + int(skinnedNormals ? a4.x : a1.x);
        vec4 b0 = c[bone], b1 = c[bone + 1], b2 = c[bone + 2];
        vec3 r14 = vec3(dot(b0, r0), dot(b1, r0), dot(b2, r0));
        if (!shadowCapture && skinnedNormals) {
            // Cofactor(B)*n / det(B): inverse-transpose without more uniforms.
            vec3 c0 = cross(b1.xyz, b2.xyz), c1 = cross(b2.xyz, b0.xyz), c2 = cross(b0.xyz, b1.xyz);
            vec3 r5 = vec3(dot(c0, a1.xyz), dot(c1, a1.xyz), dot(c2, a1.xyz));
            float det = dot(b0.xyz, c0);
            if (det == 0.0) r15 = vec3(dot(b0.xyz, a1.xyz), dot(b1.xyz, a1.xyz), dot(b2.xyz, a1.xyz));
            else r15 = r5 * (det >= 0.0 ? 1.0 : -1.0);
            r15 = safeNormalize(r15);
        }
        r0 = vec4(r14, 1.0);
    }
    vec4 r12 = vec4(dot(c[4], r0), dot(c[5], r0), dot(c[6], r0), dot(c[7], r0));
    gl_Position = depthMap(vec4(dot(projection0, r12), dot(projection1, r12), dot(projection2, r12), dot(projection3, r12)));
    if (playerShadowProject) {
        vTc0 = vec3(dot(envRow0, r12), dot(envRow1, r12), dot(envRow2, r12));
        vec2 r5 = vec2(dot(uv1Row0, r12), dot(uv1Row1, r12));
        r5.y = shadowReceiverFade.x * r5.y + shadowReceiverFade.y * a3.w + shadowReceiverFade.z;
        vTc1 = r5;
        if (environmentTexture2) vTc2 = vec2(dot(normalRow0, r12), dot(normalRow1, r12));
        else vTc2 = vec2(dot(shadowDepth, r12), 0.5);
        vColor = vec4(1.0);
    } else {
        vec4 r5 = vec4(dot(uvRow0.xyz, vec3(a2.xy, 1.0)), dot(uvRow1.xyz, vec3(a2.xy, 1.0)), 0.0, 1.0);
        r5.xy += c[9].xy;
        vec4 r13 = r5;
        r5 = vec4(dot(uvRow2.xyz, vec3(a2.zw, 1.0)), dot(uvRow3.xyz, vec3(a2.zw, 1.0)), 0.0, 1.0);
        r5.xy += c[9].zw;
        vec4 r14 = r5;
        if (environmentMapped) {
            vec3 r7 = safeNormalize(vec3(dot(envRow0.xyz, r15), dot(envRow1.xyz, r15), dot(envRow2.xyz, r15)));
            vec4 r8 = vec4(envParam.x * r7.x + envParam.y * r7.z, r7.y, 0.0, 1.0);
            r8.xy = 0.5 + 0.5 * r8.xy;
            if (environmentTexture2) {
                r14 = vec4(dot(uv1Row0, r8), dot(uv1Row1, r8), 0.0, 1.0);
            } else {
                r5.xy += envParam.z * (r8.xy - r5.xy);
            }
        }
        if (environmentTexture2) {
            if (staticLighting) r14.xy = a5.xy;
        } else {
            r5.xy = vec2(dot(uv1Row0, r5), dot(uv1Row1, r5));
            if (environmentPrimary) { r13 = r5; r5 = r14; }
        }
        float r10 = 1.0;
        if (projectedWater) {
            r5 = r13;
            r13.x = dot(envRow0, r12); r13.y = dot(envRow1, r12); r10 = dot(envRow2, r12);
            r5.xy += envParam.xy * a2.zw;
            r13.xy += envParam.zw * a2.zw * r10;
        }
        vTc1 = r5.xy;
        vTc0 = vec3(r13.xy, r10);
        vec2 fog = vec2(fogParam.x * r12.z + fogParam.y, 0.5);
        vec4 r3 = c[8];
        if (diffuseLighting) {
            vec3 r1 = safeNormalize(vec3(dot(normalRow0.xyz, r15), dot(normalRow1.xyz, r15), dot(normalRow2.xyz, r15)));
            float d = max(dot(lightDir.xyz, r1) * lightFn.x + lightFn.y, lightFn.z);
            r3 = c[8] + lightDiff * d;
        }
        r3 = min(vec4(lightDir.w), r3);
        vec4 r4 = tint * (a3 * 0.00392156863 * r3);
        if (environmentTexture2) vTc2 = r14.xy;
        else if (retailLightTexture) vTc2 = r5.xy;
        else vTc2 = fog;
        if (shadowCapture) r4.x = min(dot(shadowDepth, r12), dot(shadowFloor, r12));
        vColor = r4;
    }
#ifdef LIT
    vView = -r12.xyz;
    vNormal = safeNormalize(vec3(dot(normalRow0.xyz, r15), dot(normalRow1.xyz, r15), dot(normalRow2.xyz, r15)));
#endif
}
)";

// src/effect.v.pica
static const char* kEffectVS = R"(
uniform vec4 c[4];
void main() {
    vec4 r0 = vec4(a0.xyz, 1.0);
    gl_Position = depthMap(vec4(dot(c[0], r0), dot(c[1], r0), dot(c[2], r0), dot(c[3], r0)));
    vTc0 = vec3(a1.xy, a1.z);
    vTc1 = a2.xy;
    vTc2 = a2.zw;
    vColor = 0.00392156863 * a3;
}
)";

// vendor/cfl-mii/source/vshader.v.pica
static const char* kCflVS = R"(
uniform vec4 c[8];
void main() {
    vec4 r0 = vec4(a0.xyz, 1.0);
    vec4 r1 = vec4(dot(c[4], r0), dot(c[5], r0), dot(c[6], r0), dot(c[7], r0));
    vView = -r1.xyz;
    gl_Position = depthMap(vec4(dot(c[0], r1), dot(c[1], r1), dot(c[2], r1), dot(c[3], r1)));
    vTc0 = vec3(a2.xy, 1.0);
    vTc1 = a2.xy;
    vTc2 = a2.xy;
    vec3 n = vec3(dot(c[4].xyz, a1.xyz), dot(c[5].xyz, a1.xyz), dot(c[6].xyz, a1.xyz));
    vNormal = n * inversesqrt(max(dot(n, n), 1e-12));
    vColor = vec4(1.0);
}
)";

int uniformVectors(int program) {
    switch (program) {
    case ProgScene: case ProgMii: return 88;
    case ProgEffect: return 4;
    case ProgCfl: return 8;
    default: return 0;
    }
}
int boolCount(int program) { return program == ProgScene || program == ProgMii ? 11 : 0; }
bool programLit(int program) { return program == ProgMii || program == ProgCfl; }

std::string vertexSource(int program) {
    std::string s = programLit(program) ? "#define LIT 1\n" : "";
    s += kVertexCommon;
    switch (program) {
    case ProgScene: case ProgMii: s += kSceneVS; break;
    case ProgEffect: s += kEffectVS; break;
    default: s += kCflVS; break;
    }
    return s;
}

// ------------------------------------------------------------------ fragment
static std::string src(int s, bool alpha) {
    switch (s) {
    case GPU_PRIMARY_COLOR: return "vColor";
    case GPU_FRAGMENT_PRIMARY_COLOR: return "fragPrimary";
    case GPU_FRAGMENT_SECONDARY_COLOR: return "fragSecondary";
    case GPU_TEXTURE0: return "tex0";
    case GPU_TEXTURE1: return "tex1";
    case GPU_TEXTURE2: return "tex2";
    case GPU_PREVIOUS_BUFFER: return "buf";
    case GPU_CONSTANT: return "konst";
    case GPU_PREVIOUS: return "prev";
    default: (void)alpha; return "vec4(0.0)";
    }
}
static std::string opRgb(int op, const std::string& v) {
    switch (op) {
    case 0x1: return "(1.0 - " + v + ".rgb)";
    case 0x2: return v + ".aaa";
    case 0x3: return "(1.0 - " + v + ".aaa)";
    case 0x4: return v + ".rrr";
    case 0x5: return "(1.0 - " + v + ".rrr)";
    case 0x8: return v + ".ggg";
    case 0x9: return "(1.0 - " + v + ".ggg)";
    case 0xC: return v + ".bbb";
    case 0xD: return "(1.0 - " + v + ".bbb)";
    default: return v + ".rgb";
    }
}
static std::string opAlpha(int op, const std::string& v) {
    switch (op & 7) {
    case 1: return "(1.0 - " + v + ".a)";
    case 2: return v + ".r";
    case 3: return "(1.0 - " + v + ".r)";
    case 4: return v + ".g";
    case 5: return "(1.0 - " + v + ".g)";
    case 6: return v + ".b";
    case 7: return "(1.0 - " + v + ".b)";
    default: return v + ".a";
    }
}
static std::string combine(int func, const std::string& a, const std::string& b, const std::string& c, bool alpha) {
    const std::string one = alpha ? "1.0" : "vec3(1.0)";
    switch (func) {
    case GPU_REPLACE: return a;
    case GPU_MODULATE: return a + " * " + b;
    case GPU_ADD: return "min(" + a + " + " + b + ", " + one + ")";
    case GPU_ADD_SIGNED: return "clamp(" + a + " + " + b + " - 0.5, 0.0, 1.0)";
    case GPU_INTERPOLATE: return "mix(" + b + ", " + a + ", " + c + ")";
    case GPU_SUBTRACT: return "max(" + a + " - " + b + ", 0.0)";
    case GPU_DOT3_RGB: case GPU_DOT3_RGBA:
        if (alpha) return "clamp(dot(" + a + " - 0.5, " + b + " - 0.5) * 4.0, 0.0, 1.0)"; // scalar operands
        return "vec3(clamp(dot(" + a + " - 0.5, " + b + " - 0.5) * 4.0, 0.0, 1.0))";
    case GPU_MULTIPLY_ADD: return "min(" + a + " * " + b + " + " + c + ", " + one + ")";
    case GPU_ADD_MULTIPLY: return "min(" + a + " + " + b + ", " + one + ") * " + c;
    default: return a;
    }
}

static bool usesSource(const FragKey& k, int s) {
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 3; ++j)
            if (((k.stage[i].srcRgb >> (4 * j)) & 0xF) == s || ((k.stage[i].srcAlpha >> (4 * j)) & 0xF) == s) return true;
    return false;
}
u8 FragKey::textureMask() const {
    return u8((usesSource(*this, GPU_TEXTURE0) ? 1 : 0) | (usesSource(*this, GPU_TEXTURE1) ? 2 : 0) |
              (usesSource(*this, GPU_TEXTURE2) ? 4 : 0));
}

std::string fragmentSource(const FragKey& k) {
    std::string s = "precision mediump float;\n";
    const bool lit = k.lights > 0 && programLit(k.program);
    if (programLit(k.program)) s += "varying vec3 vView;\nvarying vec3 vNormal;\n";
    s += "varying vec3 vTc0;\nvarying vec2 vTc1;\nvarying vec2 vTc2;\nvarying vec4 vColor;\n"
         "uniform sampler2D t0; uniform sampler2D t1; uniform sampler2D t2; uniform sampler2D tLut;\n"
         "uniform vec3 uFlip;\nuniform vec4 uConst[6];\nuniform vec4 uBufColor;\nuniform float uAlphaRef;\n";
    if (lit) {
        char b[160];
        snprintf(b, sizeof b, "uniform vec3 uAmbient;\nuniform vec4 uLPos[%d];\nuniform vec3 uLDiff[%d];\nuniform vec3 uLSpec[%d];\n",
                 k.lights, k.lights, k.lights);
        s += b;
    }
    s += "void main() {\n";
    const u8 tm = k.textureMask();
    if (tm & 1) {
        s += k.projective ? "    vec2 uv0 = vTc0.xy / vTc0.z;\n" : "    vec2 uv0 = vTc0.xy;\n";
        s += "    uv0.y = mix(uv0.y, 1.0 - uv0.y, uFlip.x);\n    vec4 tex0 = texture2D(t0, uv0);\n";
    }
    if (tm & 2) s += "    vec2 uv1 = vec2(vTc1.x, mix(vTc1.y, 1.0 - vTc1.y, uFlip.y));\n    vec4 tex1 = texture2D(t1, uv1);\n";
    if (tm & 4) s += "    vec2 uv2 = vec2(vTc2.x, mix(vTc2.y, 1.0 - vTc2.y, uFlip.z));\n    vec4 tex2 = texture2D(t2, uv2);\n";
    const bool needLight = usesSource(k, GPU_FRAGMENT_PRIMARY_COLOR) || usesSource(k, GPU_FRAGMENT_SECONDARY_COLOR);
    if (needLight) {
        if (lit) {
            s += "    vec3 N = normalize(vNormal), V = normalize(vView);\n"
                 "    vec3 diff = uAmbient, spec = vec3(0.0);\n";
            char b[600];
            snprintf(b, sizeof b,
                     "    for (int i = 0; i < %d; ++i) {\n"
                     "        vec3 L = normalize(uLPos[i].xyz + (uLPos[i].w > 0.5 ? vView : vec3(0.0)));\n"
                     "        float nl = max(dot(N, L), 0.0);\n"
                     "        diff += uLDiff[i] * nl;\n"
                     "        float nh = %s;\n"
                     "        spec += uLSpec[i] * %s;\n"
                     "    }\n",
                     k.lights, k.d0Abs ? "abs(dot(N, normalize(L + V)))" : "max(dot(N, normalize(L + V)), 0.0)",
                     k.d0 ? "texture2D(tLut, vec2(nh * (255.0 / 256.0) + (0.5 / 256.0), 0.5)).r" : "1.0");
            s += b;
            s += "    vec4 fragPrimary = vec4(clamp(diff, 0.0, 1.0), 1.0);\n"
                 "    vec4 fragSecondary = vec4(clamp(spec, 0.0, 1.0), 1.0);\n";
        } else {
            s += "    vec4 fragPrimary = vec4(0.0), fragSecondary = vec4(0.0);\n";
        }
    }
    s += "    vec4 prev = vec4(0.0);\n    vec4 buf = uBufColor, nextBuf = uBufColor;\n";
    static const char* scales[3] = {"", " * 2.0", " * 4.0"};
    for (int i = 0; i < 6; ++i) {
        const FragKey::Stage& st = k.stage[i];
        const bool passthrough = st.funcRgb == GPU_REPLACE && st.funcAlpha == GPU_REPLACE &&
                                 (st.srcRgb & 0xF) == GPU_PREVIOUS && (st.srcAlpha & 0xF) == GPU_PREVIOUS &&
                                 (st.opRgb & 0xF) == 0 && (st.opAlpha & 0xF) == 0 && st.scaleRgb == 0 && st.scaleAlpha == 0;
        char head[64];
        snprintf(head, sizeof head, "    { // stage %d\n", i);
        s += head;
        if (!passthrough) {
            snprintf(head, sizeof head, "        vec4 konst = uConst[%d];\n", i);
            s += head;
            std::string ra[3], aa[3];
            for (int j = 0; j < 3; ++j) {
                ra[j] = opRgb((st.opRgb >> (4 * j)) & 0xF, src((st.srcRgb >> (4 * j)) & 0xF, false));
                aa[j] = opAlpha((st.opAlpha >> (4 * j)) & 0xF, src((st.srcAlpha >> (4 * j)) & 0xF, true));
            }
            s += "        vec3 rgb = clamp(" + combine(st.funcRgb, ra[0], ra[1], ra[2], false) + std::string(scales[st.scaleRgb % 3]) + ", 0.0, 1.0);\n";
            if (st.funcRgb == GPU_DOT3_RGBA) s += "        float alpha = rgb.r;\n";
            else s += "        float alpha = clamp(" + combine(st.funcAlpha, aa[0], aa[1], aa[2], true) + std::string(scales[st.scaleAlpha % 3]) + ", 0.0, 1.0);\n";
            s += "        prev = vec4(rgb, alpha);\n";
        }
        s += "        buf = nextBuf;\n";
        if (i < 4) {
            if (k.bufUpdate >> (8 + i) & 1) s += "        nextBuf.rgb = prev.rgb;\n";
            if (k.bufUpdate >> (12 + i) & 1) s += "        nextBuf.a = prev.a;\n";
        }
        s += "    }\n";
    }
    if (k.alphaTest) {
        // Integer 8-bit comparison as on the PICA.
        s += "    float a8 = floor(prev.a * 255.0 + 0.5);\n";
        static const char* cmp[8] = {"true", "false", "a8 != uAlphaRef", "a8 == uAlphaRef",
                                     "a8 >= uAlphaRef", "a8 > uAlphaRef", "a8 <= uAlphaRef", "a8 < uAlphaRef"};
        s += "    if (" + std::string(cmp[k.alphaFunc & 7]) + ") discard;\n";
    }
    s += "    gl_FragColor = prev;\n}\n";
    return s;
}
}

// swp_shaders.h — embedded GLSL ES 3.2 shaders for the spacewarp estimator.
//
// Junk-free plain code; ports the VD MotionEngine ALGORITHM (block-match SAD
// with a GME seed + 9-element sorting-network median + H1 refinement (3x3
// window + parabolic subpel) + extrapolation warp with stretch-radius/clamp
// handling), not its text (EVIDENCE-shaders-deobfuscated.txt stays proprietary;
// see the 08-ESTIMATOR-B1a receipt for the conservative-choice notes).
//
// Composition table (mirrored EXACTLY by swp_est_worker.cpp and by
// tests/swp_shader_syntax.py):
//
//   swp_vert_quad                                        self-contained (own #version)
//   swp_sad_frag          = ver + DIRECT_PREFIX   + core self-contained (QCOM)
//   swp_sad_fallback_frag = ver + FALLBACK_PREFIX + core self-contained (plain GLSL)
//   swp_gme_*             = ver + <prefix>        + swp_gme_core
//   swp_h1_*              = ver + <prefix>        + swp_h1_core
//   swp_median_frag                                      self-contained
//   swp_extrap_frag                                      self-contained
//
// Motion-vector convention: stored .rg = uv displacement (= NDC delta / 2);
// .ba = 0 (RE-SHARED-IMPL.md). Internal L1/median maps carry raw input-texel
// offsets plus a quality value in .b (internal only; never reaches caller maps).

#pragma once

#define SWP_GLSL_VERSION R"GLSL(#version 320 es
)GLSL"

// Fullscreen triangle for every pass; no vertex buffers, no attribute state
// (the estimator must leave no GL state to own beyond what a pass restores).
#define SWP_GLSL_QUAD_VERT R"GLSL(precision highp float;
layout(location = 0) out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL"

// Direct path: QCOM hardware block-match SAD (GL_QCOM_block_match).
#define SWP_GLSL_DIRECT_PREFIX R"GLSL(#extension GL_QCOM_block_match : require
#define SWP_SAD(tA, cA, tB, cB, bs) blockMatchSADQCOM(tA, cA, tB, cB, bs).x
)GLSL"

// Fallback path: plain-GLSL SAD (BT-independent: summed per-channel |delta|),
// used when GL_QCOM_block_match is absent.
#define SWP_GLSL_FALLBACK_PREFIX R"GLSL(precision highp int;
precision highp float;
float swpSadFallback(highp sampler2D texA, ivec2 cA, highp sampler2D texB, ivec2 cB, int blockSize) {
    float sad = 0.0;
    for (int y = 0; y < blockSize; ++y) {
        for (int x = 0; x < blockSize; ++x) {
            ivec2 t = ivec2(x, y);
            vec3 a = texelFetch(texA, cA + t, 0).rgb;
            vec3 b = texelFetch(texB, cB + t, 0).rgb;
            sad += abs(a.r - b.r) + abs(a.g - b.g) + abs(a.b - b.b);
        }
    }
    return sad;
}
#define SWP_SAD(tA, cA, tB, cB, bs) swpSadFallback(tA, cA, tB, cB, bs)
)GLSL"

// Common search core shared by the L1 pair (composes behind either prefix).
#define SWP_GLSL_SAD_CORE R"GLSL(precision highp int;
precision highp float;

// Block-match SAD pass (four quadrant SADs per candidate, VD L1 structure).
// Forward flow: SAD over block(prev, origin) vs block(curr, origin + o);
// o = position of that content in curr relative to its prev position.
uniform highp sampler2D uPrev;      // frame N-1
uniform highp sampler2D uCurr;      // frame N
uniform highp sampler2D uGmeTex;    // 1x1 GME seed: rg = candidate, texel offsets

uniform ivec2 uInputSize;           // input texels
uniform ivec2 uMvSize;              // mv-map texels
uniform ivec2 uBlockGrid;           // inputSize / block
uniform int uBlock;                 // block size (16 direct / 8 fallback)
uniform ivec2 uSearchSpan;          // window radius around the GME seed
uniform ivec2 uGlobalMax;           // absolute search clamp
uniform float uSadScale;            // raw SAD -> per-texel mean normalization
uniform float uNoMotion;            // mean |delta| below which a block is static

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 oMv;

float quadSad(ivec2 origin, ivec2 currOrigin) {
    int b = uBlock;
    float s = SWP_SAD(uPrev, origin, uCurr, currOrigin, b);
    s += SWP_SAD(uPrev, origin + ivec2(b, 0), uCurr, currOrigin + ivec2(b, 0), b);
    s += SWP_SAD(uPrev, origin + ivec2(b, b), uCurr, currOrigin + ivec2(b, b), b);
    s += SWP_SAD(uPrev, origin + ivec2(0, b), uCurr, currOrigin + ivec2(0, b), b);
    return s;
}

void main() {
    ivec2 tC = clamp(ivec2(clamp(vUv, 0.0, 1.0) * vec2(uMvSize)), ivec2(0), uMvSize - ivec2(1));
    ivec2 block = min((tC * uBlockGrid) / uMvSize, uBlockGrid - ivec2(1));
    // Quadrant reach keeps candidates at origin + o + 2*block inside the frame.
    ivec2 reach = uGlobalMax;
    ivec2 origin = min(max(block * uBlock, reach), uInputSize - ivec2(2 * uBlock) - reach);

    ivec2 gme = clamp(ivec2(round(texelFetch(uGmeTex, ivec2(0), 0).rg)),
                      -uGlobalMax, uGlobalMax);
    ivec2 lo = max(gme - uSearchSpan, -uGlobalMax);
    ivec2 hi = min(gme + uSearchSpan, uGlobalMax);

    float center = quadSad(origin, origin);
    if (center / uSadScale < uNoMotion) {
        oMv = vec4(0.0);              // static block: no motion (conservative)
        return;
    }
    float bestSad = center;
    ivec2 bestO = ivec2(0);
    for (int oy = lo.y; oy <= hi.y; ++oy) {
        for (int ox = lo.x; ox <= hi.x; ++ox) {
            if ((ox == 0) && (oy == 0)) continue;
            ivec2 cand = origin + ivec2(ox, oy);
            if (any(lessThan(cand, ivec2(0))) ||
                any(greaterThan(cand + ivec2(2 * uBlock), uInputSize))) continue;
            float s = quadSad(origin, cand);
            if (s < bestSad) { bestSad = s; bestO = ivec2(ox, oy); }
        }
    }
    if ((bestO.x == 0) && (bestO.y == 0)) {
        oMv = vec4(0.0);              // strictly-better rule: no motion
        return;
    }
    oMv = vec4(vec2(bestO), bestSad / uSadScale, 0.0);   // texel offsets + quality
}
)GLSL"

// Coarse global motion estimate: 1x1 target; strided block-lattice mini-SADs
// over the candidate displacement grid; rg = best candidate (texel offsets),
// b = mean per-block SAD (quality). Composes behind either prefix.
#define SWP_GLSL_GME_CORE R"GLSL(precision highp int;
precision highp float;

uniform highp sampler2D uPrev;
uniform highp sampler2D uCurr;
uniform ivec2 uInputSize;
uniform int uBlock;
uniform ivec2 uGmeStep;            // candidate stride (hierarchical coarse pass)
uniform ivec2 uGmeOriginStride;    // block-lattice stride
uniform ivec2 uGlobalMax;
uniform float uSadScale;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 oGme;

void main() {
    float bestScore = 3.4e38;
    ivec2 best = ivec2(0);
    int blocks = 0;
    for (int oy = -uGlobalMax.y; oy <= uGlobalMax.y; oy += uGmeStep.y) {
        for (int ox = -uGlobalMax.x; ox <= uGlobalMax.x; ox += uGmeStep.x) {
            ivec2 o = ivec2(ox, oy);
            float score = 0.0;
            int seen = 0;
            // Every origin below is valid for every candidate by construction.
            for (int by = uGlobalMax.y; by + uBlock + uGlobalMax.y <= uInputSize.y;
                 by += uGmeOriginStride.y) {
                for (int bx = uGlobalMax.x; bx + uBlock + uGlobalMax.x <= uInputSize.x;
                     bx += uGmeOriginStride.x) {
                    score += SWP_SAD(uPrev, ivec2(bx, by), uCurr, ivec2(bx, by) + o, uBlock);
                    seen++;
                }
            }
            if (seen == 0) continue;
            score /= float(seen);
            blocks = 1;
            if (score < bestScore) { bestScore = score; best = o; }
        }
    }
    if (blocks == 0) { oGme = vec4(0.0); return; }
    oGme = vec4(vec2(best), bestScore / float(uBlock * uBlock), 0.0);
}
)GLSL"

// Fused median + H1 refinement + parabolic subpel + final write. The seed is
// the 9-element median (computed here over uMedianTex = the raw L1 map), then
// a 3x3 SAD refinement around it, then a per-axis quadratic subpel fit. Writes
// the FINAL caller-contract output: rg = uv displacement, ba = 0. Composes
// behind either prefix.
#define SWP_GLSL_H1_CORE R"GLSL(precision highp int;
precision highp float;

uniform highp sampler2D uPrev;      // frame N-1
uniform highp sampler2D uCurr;      // frame N
uniform highp sampler2D uMedianTex; // raw L1 map (rg = texel offsets)

uniform ivec2 uInputSize;
uniform ivec2 uMvSize;
uniform ivec2 uBlockGrid;
uniform int uBlock;
uniform ivec2 uGlobalMax;
uniform float uSadScale;
uniform float uNoMotionUv;          // zero floor on the stored uv vector

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 oMv;

float quadSad(ivec2 origin, ivec2 currOrigin) {
    int b = uBlock;
    float s = SWP_SAD(uPrev, origin, uCurr, currOrigin, b);
    s += SWP_SAD(uPrev, origin + ivec2(b, 0), uCurr, currOrigin + ivec2(b, 0), b);
    s += SWP_SAD(uPrev, origin + ivec2(b, b), uCurr, currOrigin + ivec2(b, b), b);
    s += SWP_SAD(uPrev, origin + ivec2(0, b), uCurr, currOrigin + ivec2(0, b), b);
    return s;
}

void main() {
    ivec2 tC = clamp(ivec2(clamp(vUv, 0.0, 1.0) * vec2(uMvSize)), ivec2(0), uMvSize - ivec2(1));
    ivec2 block = min((tC * uBlockGrid) / uMvSize, uBlockGrid - ivec2(1));
    ivec2 reach = uGlobalMax;
    ivec2 origin = min(max(block * uBlock, reach), uInputSize - ivec2(2 * uBlock) - reach);

    ivec2 seed = clamp(ivec2(round(texelFetch(uMedianTex, tC, 0).rg)), -uGlobalMax, uGlobalMax);

    float s[9];                       // s[by*3 + bx], offset (bx-1, by-1) around the seed
    for (int i = 0; i < 9; ++i) {
        ivec2 cand = origin + seed + ivec2((i % 3) - 1, (i / 3) - 1);
        if (any(lessThan(cand, ivec2(0))) ||
            any(greaterThan(cand + ivec2(2 * uBlock), uInputSize))) {
            s[i] = 3.4e38;            // out of range: never selected
        } else {
            s[i] = quadSad(origin, cand);
        }
    }
    int best = 4;
    for (int i = 0; i < 9; ++i) if (s[i] < s[best]) best = i;
    int bx = best % 3;
    int by = best / 3;

    vec2 sub = vec2(0.0);
    // Parabolic subpel through the winner's axis neighbors (VD subpel shape);
    // only when the winner is the minimum of the triple, else the fit is junk.
    int row = by * 3;
    float sxm = s[row + (bx > 0 ? bx - 1 : 0)];
    float sxp = s[row + (bx < 2 ? bx + 1 : 2)];
    if (min(min(sxm, sxp), s[best]) == s[best]) {
        sub.x = clamp(0.5 * ((sxp - sxm) / ((s[best] - max(sxm, sxp)) + 0.0001)), -0.5, 0.5);
    }
    int col = bx;
    float sym = s[(by > 0 ? by - 1 : 0) * 3 + col];
    float syp = s[(by < 2 ? by + 1 : 2) * 3 + col];
    if (min(min(sym, syp), s[best]) == s[best]) {
        sub.y = clamp(0.5 * ((syp - sym) / ((s[best] - max(sym, syp)) + 0.0001)), -0.5, 0.5);
    }

    vec2 refined = vec2(seed + ivec2(bx - 1, by - 1)) + sub;   // input-texel offsets
    vec2 uv = refined / vec2(uInputSize);                      // uv displacement
    if (max(abs(uv.x), abs(uv.y)) < uNoMotionUv) {
        oMv = vec4(0.0, 0.0, 0.0, 0.0);                        // contract BA = 0
        return;
    }
    oMv = vec4(uv, 0.0, 0.0);
}
)GLSL"

// 9-element (3x3 neighborhood at uMedianStride spacing) sorting-network median
// over the raw L1 map. The swap network is the standard 19-comparator median
// selection network (same structure VD ships at filterSize 9 / filterRange 3).
#define SWP_GLSL_MEDIAN_FRAG R"GLSL(#version 320 es
precision highp int;
precision highp float;

uniform highp sampler2D uL1Raw;
uniform ivec2 uMvSize;
uniform ivec2 uMedianStride;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 oMed;

void main() {
    vec2 v[9];
    ivec2 t = clamp(ivec2(clamp(vUv, 0.0, 1.0) * vec2(uMvSize)), ivec2(0), uMvSize - ivec2(1));
    int i = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 c = clamp(t + ivec2(dx, dy) * uMedianStride, ivec2(0), uMvSize - ivec2(1));
            v[i++] = texelFetch(uL1Raw, c, 0).rg;
        }
    }
    vec2 tmp;
    #define SWP_CX(a, b) tmp = a; a = min(a, b); b = max(tmp, b);
    SWP_CX(v[0], v[1]); SWP_CX(v[3], v[4]); SWP_CX(v[6], v[7]);
    SWP_CX(v[1], v[2]); SWP_CX(v[4], v[5]); SWP_CX(v[7], v[8]);
    SWP_CX(v[0], v[1]); SWP_CX(v[3], v[4]); SWP_CX(v[6], v[7]); SWP_CX(v[2], v[5]);
    SWP_CX(v[0], v[3]); SWP_CX(v[1], v[4]); SWP_CX(v[5], v[8]);
    SWP_CX(v[3], v[6]); SWP_CX(v[4], v[7]); SWP_CX(v[2], v[5]);
    SWP_CX(v[0], v[3]); SWP_CX(v[1], v[4]); SWP_CX(v[5], v[7]); SWP_CX(v[2], v[6]);
    SWP_CX(v[1], v[3]); SWP_CX(v[4], v[6]);
    SWP_CX(v[2], v[4]); SWP_CX(v[5], v[6]);
    SWP_CX(v[2], v[3]);
    #undef SWP_CX
    oMed = vec4(v[4], 0.0, 0.0);
}
)GLSL"

// Extrapolation warp: single-texture backward warp of srcTex by the stored
// motion field, scaled by normalized progress (time-shifted vectors; VD's
// ME_Extrapolate == warp with time-shifted vectors). Edge handling = VD
// sr.mode/sr.radius + clampMagnitude: clamp the vector magnitude, then (mode
// 0, default) attenuate it inside the last uSrRadius band at every border
// (content stretches as vectors ramp to zero), then clamp the sample point.
#define SWP_GLSL_EXTRAP_FRAG R"GLSL(#version 320 es
precision highp float;

uniform highp sampler2D uSrc;        // warped source frame
uniform highp sampler2D uMv;         // RGBA16F, rg = uv displacement, ba = 0
uniform float uScaleFactor;          // normalized progress, >= 1.0 (clamped CPU-side)
uniform float uFovScale;             // init fovScale fold (1.0 = same-FOV path)
uniform float uClampMag;             // clamp magnitude, uv units
uniform float uSrRadius;             // stretch radius, uv units
uniform int uSrMode;                 // 0 = stretch ramp then clamp (default), 1 = hard clamp

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 oColor;

void main() {
    vec2 d = texture(uMv, vUv).rg * uFovScale;
    d = clamp(d, vec2(-uClampMag), vec2(uClampMag));   // clamp magnitude
    d *= (1.0 - uScaleFactor);                          // time-shifted vectors
    if (uSrMode == 0) {
        vec2 room = min(vUv, vec2(1.0) - vUv) / max(uSrRadius, 1e-6);
        d *= min(room, vec2(1.0));                      // stretch with radius
    }
    vec2 suv = clamp(vUv + d, vec2(0.0), vec2(1.0));    // then clamps
    oColor = texture(uSrc, suv);                        // caller's filter/wrap params apply
}
)GLSL"

namespace gxr {
namespace swp_shaders {

// The brief's shader names; the composites the worker builds are exactly:
//   swp_sad_frag          (already version+QCOM-prefixed, self-contained)
//   swp_sad_fallback_frag (already version+plain-prefixed, self-contained)
//   swp_gme direct/fallback = swp_version_line + <prefix> + swp_gme_core
//   swp_h1  direct/fallback = swp_version_line + <prefix> + swp_h1_core
static const char* const swp_version_line = SWP_GLSL_VERSION;
static const char* const swp_sad_direct_prefix = SWP_GLSL_DIRECT_PREFIX;
static const char* const swp_sad_fallback_prefix = SWP_GLSL_FALLBACK_PREFIX;
static const char* const swp_sad_core = SWP_GLSL_SAD_CORE;
static const char* const swp_gme_core = SWP_GLSL_GME_CORE;
static const char* const swp_h1_core = SWP_GLSL_H1_CORE;
static const char* const swp_median_frag = SWP_GLSL_MEDIAN_FRAG;
static const char* const swp_extrap_frag = SWP_GLSL_EXTRAP_FRAG;
static const char* const swp_vert_quad = SWP_GLSL_VERSION SWP_GLSL_QUAD_VERT;
static const char* const swp_sad_frag =
    SWP_GLSL_VERSION SWP_GLSL_DIRECT_PREFIX SWP_GLSL_SAD_CORE;
static const char* const swp_sad_fallback_frag =
    SWP_GLSL_VERSION SWP_GLSL_FALLBACK_PREFIX SWP_GLSL_SAD_CORE;

}  // namespace swp_shaders
}  // namespace gxr

#undef SWP_GLSL_VERSION
#undef SWP_GLSL_QUAD_VERT
#undef SWP_GLSL_DIRECT_PREFIX
#undef SWP_GLSL_FALLBACK_PREFIX
#undef SWP_GLSL_SAD_CORE
#undef SWP_GLSL_GME_CORE
#undef SWP_GLSL_H1_CORE
#undef SWP_GLSL_MEDIAN_FRAG
#undef SWP_GLSL_EXTRAP_FRAG
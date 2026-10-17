// spacewarp_est.cpp — contract entries + orchestration for the spacewarp
// estimator core (gxr-spacewarp-layer).
//
// Owns: the shared interface contract (spacewarp_est.h), init validation with
// the brief's defaults (mv = input/2 clamped to >= 64), the status bitmask
// (bits 2/4/32 per call; 1/8/16 sticky), and the context-identity gate that
// turns any wrong/missing EGL context into sticky stand-down (status 16).
// All GL work lives in swp_est_worker.cpp.
//
// Every public entry point checks, then bails: no context assumptions, no
// exceptions, no partial writes. SwpInit is idempotent (teardown + rebuild).

#include <cmath>
#include <cstring>

#include "spacewarp_est.h"

// printf-style logging, overridden by the layer (B1b) via a compile flag:
//   -DSWP_EST_LOG(x)=__android_log_print(ANDROID_LOG_INFO, "GXRSwarp", __VA_ARGS__)
// or equivalent. The estimator itself carries no platform log dependency.
#ifndef SWP_EST_LOG
#define SWP_EST_LOG(...) ((void)0)
#endif

namespace {

using gxr::swp_internal::kSwpDefaultBudgetMicros;
using gxr::swp_internal::kSwpMaxInputDim;
using gxr::swp_internal::kSwpMaxMvDim;
using gxr::swp_internal::kSwpMinInputDim;
using gxr::swp_internal::kSwpMinMvDim;
using gxr::swp_internal::SwpEstConfig;
using gxr::swp_internal::SwpEstTickOut;

uint32_t g_status = 0;
bool g_tableSet = false;
bool g_inited = false;
SwpGlTable g_tbl{};
void* g_expectCtx = nullptr;
SwpEstConfig g_cfg{};
bool g_qcomArmed = false;
bool g_standdownLogged = false;

static void LogOnce(const char* what) {
    (void)what;  // consumed by the layer's SWP_EST_LOG override; silent here
    SWP_EST_LOG("GXRSwarp: stand-down: %s (status 0x%x)", what, (unsigned)g_status);
}

static void StandDown(const char* why) {
    g_status |= 0x10u;  // SWP_STATUS_STANDDOWN
    if (!g_standdownLogged) {
        g_standdownLogged = true;
        LogOnce(why);
    }
}

static bool ContextOk() {
    if (!g_tableSet) return false;
    if (!g_tbl.getCurrentContext) return true;  // identity unverifiable; B1b's dispatch gate
    void* cur = g_tbl.getCurrentContext();
    if (!cur) return false;                     // no context current => wrong tick
    if (g_expectCtx && cur != g_expectCtx) return false;
    return true;
}

static bool EntryGate() {
    if (!g_tableSet || !g_inited) return false;
    if (g_status & 0x10u) return false;  // sticky stand-down until re-init
    if (!ContextOk()) {
        StandDown("context identity wrong");
        return false;
    }
    return true;
}

static bool MatrixSane(const float* m) {
    if (!m) return false;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) return false;
    }
    return true;
}

static bool FovSane(const float* f) {
    if (!f) return false;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(f[i])) return false;
    }
    if (!(f[1] > f[0])) return false;  // angleLeft < angleRight
    if (!(f[2] > f[3])) return false;  // angleDown < angleUp (XrFov order: L, R, U, D)
    return true;
}

static bool ResolveConfig(const SwpInitInfo& info, SwpEstConfig* out) {
    SwpEstConfig c{};
    c.forceFallbackSad = (info.flags & SWP_INIT_FLAG_FORCE_SAD_FALLBACK) != 0;
    c.verboseLog = (info.flags & SWP_INIT_FLAG_VERBOSE_LOG) != 0;
    c.budgetMicros = (info.flags & SWP_INIT_FLAG_BUDGET_OFF) ? 0u : kSwpDefaultBudgetMicros;
    c.fovScale = (info.fovScale > 0.0f && std::isfinite(info.fovScale)) ? info.fovScale : 1.0f;

    c.inputW = static_cast<int>(info.inputWidth);
    c.inputH = static_cast<int>(info.inputHeight);
    if (c.inputW < kSwpMinInputDim || c.inputH < kSwpMinInputDim ||
        c.inputW > kSwpMaxInputDim || c.inputH > kSwpMaxInputDim) {
        SWP_EST_LOG("GXRSwarp: init: input size impossibly large or small: %dx%d",
                    c.inputW, c.inputH);
        return false;
    }
    if ((info.inputWidth % 16u) != 0u || (info.inputHeight % 16u) != 0u) {
        // VD parity: "Input width or height are not divisible by %d" (block size).
        SWP_EST_LOG("GXRSwarp: init: input size not divisible by %d: %dx%d", 16,
                    c.inputW, c.inputH);
        return false;
    }

    // mv defaults from brief 08: mvW/mvH = input/2, clamped to 64 min; an
    // explicit value is honored when sane.
    int mvW = info.mvWidth ? static_cast<int>(info.mvWidth)
                           : (c.inputW / 2 > 64 ? c.inputW / 2 : 64);
    int mvH = info.mvHeight ? static_cast<int>(info.mvHeight)
                            : (c.inputH / 2 > 64 ? c.inputH / 2 : 64);
    if (mvW < kSwpMinMvDim || mvH < kSwpMinMvDim || mvW > kSwpMaxMvDim ||
        mvH > kSwpMaxMvDim) {
        SWP_EST_LOG("GXRSwarp: init: mv map size unusable: %dx%d", mvW, mvH);
        return false;
    }
    c.mvW = mvW;
    c.mvH = mvH;
    c.blockGridW = c.inputW / 16;
    c.blockGridH = c.inputH / 16;

    // Conservative search geometry (input texels); the fallback (manual-SAD)
    // path shrinks the windows to keep its per-tick cost sane.
    if (c.forceFallbackSad) {
        c.block = gxr::swp_internal::kSwpFallbackBlock;
        c.globalMaxX = c.globalMaxY = 24;
        c.gmeStepX = c.gmeStepY = 8;
        c.gmeOriginStrideX = c.gmeOriginStrideY = 128;
        c.searchSpanX = c.searchSpanY = 1;
    } else {
        c.block = gxr::swp_internal::kSwpBlock;
        c.globalMaxX = c.globalMaxY = 96;
        c.gmeStepX = c.gmeStepY = 32;
        c.gmeOriginStrideX = c.gmeOriginStrideY = 256;
        c.searchSpanX = c.searchSpanY = 2;
    }
    c.medianStride = 3;  // VD filterRange = 3

    // Raw-SAD normalization: mean |delta| per texel over summed channels.
    c.sadScale = 4.0f * static_cast<float>(c.block * c.block);
    c.noMotion = 2.0f;   // ~0.67 per channel: static regions report no motion
    c.noMotionUv = 1.0f / static_cast<float>(
        (c.inputW > c.inputH ? c.inputW : c.inputH)) * 0.25f;  // quarter texel
    c.clampMagUv = 48.0f / static_cast<float>(
        (c.inputW > c.inputH ? c.inputW : c.inputH));          // ~48 texels
    c.srRadiusUv = 16.0f / static_cast<float>(
        (c.inputW > c.inputH ? c.inputW : c.inputH));          // 16-texel band
    c.srMode = 0;        // stretch ramp then clamp (conservative default)
    *out = c;
    return true;
}

}  // namespace

const char* SwpLogTag = "GXRSwarp";

bool SwpSetGlTable(const SwpGlTable* table) {
    if (!table) {
        g_tableSet = false;
        g_tbl = SwpGlTable{};
        SWP_EST_LOG("GXRSwarp: table cleared");
        return false;
    }
    std::memcpy(&g_tbl, table, sizeof(SwpGlTable));
    g_tableSet = true;
    if (g_inited) {
        // Table swapped mid-session: resources were built against the old
        // dispatch — require a fresh init (conservative, RE-SHARED-IMPL.md).
        SwpDestroy();
        StandDown("GL table replaced while initialized");
    }
    return true;
}

void SwpBindIdentityContext(void* currentCtx) {
    g_expectCtx = currentCtx;
}

bool SwpResolvedMvSize(u32* outW, u32* outH) {
    if (!g_inited) return false;
    if (outW) *outW = static_cast<u32>(g_cfg.mvW);
    if (outH) *outH = static_cast<u32>(g_cfg.mvH);
    return true;
}

bool SwpInit(const SwpInitInfo* info) {
    SwpDestroy();
    g_standdownLogged = false;
    if (!info) {
        g_status |= 0x10u;
        g_standdownLogged = true;
        SWP_EST_LOG("GXRSwarp: init: null info");
        return false;
    }
    if (!g_tableSet) {
        g_status |= 0x10u;
        g_standdownLogged = true;
        SWP_EST_LOG("GXRSwarp: init: no GL table set");
        return false;
    }
    if (!ContextOk()) {
        g_status |= 0x10u;
        g_standdownLogged = true;
        SWP_EST_LOG("GXRSwarp: init: no current context / identity mismatch");
        return false;
    }

    SwpEstConfig cfg{};
    if (!ResolveConfig(*info, &cfg)) {
        g_status |= 0x10u;
        g_standdownLogged = true;
        return false;
    }

    g_cfg = cfg;
    gxr::swp_internal::EstiSetTable(&g_tbl);
    bool qcomArmed = false;
    if (!gxr::swp_internal::EstiGlSetup(g_cfg, &qcomArmed)) {
        g_status |= 0x10u;
        g_standdownLogged = true;
        return false;
    }
    g_qcomArmed = qcomArmed;
    g_inited = true;

    g_status = 0x1u | (qcomArmed ? 0u : 0x8u);
    return true;
}

void SwpDestroy() {
    // Delete GL objects only while the estimator's bound context is current
    // (deleting with the wrong context = wrong namespace + leak); otherwise
    // drop the handles — the context teardown reclaims them.
    if (g_inited && ContextOk()) {
        gxr::swp_internal::EstiGlShutdown();
    } else {
        gxr::swp_internal::EstiDropHandles();
    }
    g_inited = false;
    g_status = 0;
    g_expectCtx = nullptr;
}

bool SwpGenerateVectorsStereo(u32 prevTexL, u32 currTexL, u32 prevTexR, u32 currTexR,
                          const float* viewMatrixL, const float* viewMatrixR,
                          const float* fovL, const float* fovR,
                          u32 mvTexL, u32 mvTexR) {
    g_status &= ~(0x2u | 0x20u);  // this call's result; budget bit is per-tick
    if (!EntryGate()) return false;
    if (!prevTexL || !currTexL || !prevTexR || !currTexR || !mvTexL || !mvTexR) {
        SWP_EST_LOG("GXRSwarp: mv: zero texture id");
        return false;
    }
    if (!MatrixSane(viewMatrixL) || !MatrixSane(viewMatrixR) ||
        !FovSane(fovL) || !FovSane(fovR)) {
        SWP_EST_LOG("GXRSwarp: mv: null/insane pose or fov input");
        return false;
    }

    SwpEstTickOut lOut{}, rOut{};
    const bool ok =
        gxr::swp_internal::EstiMvMapOneEye(g_cfg, prevTexL, currTexL, mvTexL, &lOut) &&
        gxr::swp_internal::EstiMvMapOneEye(g_cfg, prevTexR, currTexR, mvTexR, &rOut);
    if (ok) {
        g_status |= 0x2u;
    }
    if (lOut.budgetExceeded || rOut.budgetExceeded) {
        g_status |= 0x20u;
    }
    return ok;
}

bool SwpExtrapolateStereo(u32 mvTexL, u32 mvTexR, u32 srcTexL, u32 srcTexR,
                      u32 dstTexL, u32 dstTexR, float scaleFactor) {
    g_status &= ~(0x4u | 0x20u);
    if (!EntryGate()) return false;
    if (!mvTexL || !mvTexR || !srcTexL || !srcTexR || !dstTexL || !dstTexR) {
        SWP_EST_LOG("GXRSwarp: extrap: zero texture id");
        return false;
    }

    SwpEstTickOut lOut{}, rOut{};
    const bool ok =
        gxr::swp_internal::EstiExtrapolateOneEye(g_cfg, scaleFactor, mvTexL, srcTexL,
                                                 dstTexL, &lOut) &&
        gxr::swp_internal::EstiExtrapolateOneEye(g_cfg, scaleFactor, mvTexR, srcTexR,
                                                 dstTexR, &rOut);
    if (ok) {
        g_status |= 0x4u;
    }
    if ((lOut.budgetExceeded || rOut.budgetExceeded)) {
        g_status |= 0x20u;
    }
    return ok;
}

u32 SwpLastStatus() {
    return g_status;
}
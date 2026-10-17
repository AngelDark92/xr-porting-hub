// gxr-spacewarp: per-frame XR dispatch wrappers + missed-tick logic (09-LAYER-B1b).
//
// Steam Link VR 2.0.23/5002363 asynchronous spacewarp in the VD sense
// (RE-SHARED-IMPL.md): on a display tick where no fresh decoded frame arrived,
// extrapolate the projection content by warping the two most recent decoded frames
// with the on-device estimator, into the texture the compositor will read this tick,
// while preserving the client's composition layers exactly (projection headers and
// overlay layers are forwarded untouched — this layer only changes WHAT the
// projection texture contains, never the layer list).
//
// Frame flow:
//   1. xrBeginFrame — passthrough wrapper, used as a load-readiness trigger only
//      (OpenXR's xrBeginFrame carries a begin marker; the compositor deadline the
//      clock conversion uses is each tick's XrFrameEndInfo.displayTime).
//   2. xrLocateViews — captures both eyes' poses + fov (the locate pose of the tick).
//   3. xrEnumerateSwapchainImages / xrAcquireSwapchainImage — PASSIVE observers (full
//      passthrough) that learn the GL texture id behind each projection subimage
//      (XR_KHR_opengl_es_enablement: XrSwapchainImageOpenGLESKHR.image) and the
//      pending acquire index whose image the compositor reads. README + receipt
//      document why these two observers extend the brief's interceptor list: the
//      contract demands dstTex from "the projection view subimage data at THIS
//      xrEndFrame", and its GL texture is only reachable through swapchain
//      enumeration + acquisition state.
//   4. eglCreateImageKHR arrivals (GOT wrapper installed by swp_hooks.cpp, logic in
//      swp_egl.cpp) learn decoded-frame arrival epochs.
//   5. At xrEndFrame (missed-tick candidate 1 from the shared brief): when the newest
//      arrival epoch equals the one seen at the previous endframe, this tick is a
//      MISSED tick -> SwpExtrapolateStereo into this tick's pending dst textures;
//      otherwise it is a real tick -> SwpGenerateVectorsStereo (prev=older,
//      curr=newest, with the LocateViews pose of each tick) plus history record.
//      scaleFactor derives from arrival timestamps vs predictedDisplayTime the way
//      VD's C# does, clamped and capped (max 2 consecutive extrapolated ticks).
//   6. Brief-10 bridge gate: the VTS consumer (swp_vts.cpp) reads the host's
//      published carrier (ValueTransportSystem id 0x48, 4-byte int) every 60
//      endframes; ALL synthesis (MV-gen + extrapolation) runs only while the
//      carrier reads enabled AND the per-session entry is on — stock sessions
//      never auto-enable, and everything else stays the stock passthrough path
//      (observers only). Stand-down semantics unchanged.
//   7. ANY failure stands down this tick only (status 16), logged once per 30
//      occurrences; the already-rendered client content still reaches the compositor,
//      so the worst case equals stock.
//
// The estimator core (spacewarp_est.h/.cpp, swp_shaders.h, swp_est_worker.cpp) is
// owned by the sibling builder: consumed strictly through the shared interface
// contract. When it is absent from a skeleton build this TU compiles with
// fail-safe stubs that never arm; the parent resolves the real symbols at its
// build. Drawing uses ONLY the client's current EGL context (verified against the
// frame-asset context); this TU never creates or makes current its own context
// (CRITICAL shared-brief constraint).

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <jni.h>  // openxr_platform.h's Android extension blocks name jni types

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// Estimator consumption — the shared interface contract (RE-SHARED-IMPL.md).
// ---------------------------------------------------------------------------
#if defined(__has_include)
#if __has_include("spacewarp_est.h")
#define SWP_HAVE_ESTIMATOR 1
#endif
#endif

#ifdef SWP_HAVE_ESTIMATOR
#include "spacewarp_est.h"
#else
// Contract mirror for skeleton builds + host tests (exact shared-contract
// signatures; identity is enforced by the compiler once the header lands).
struct SwpInitInfo {
    uint32_t inputWidth;
    uint32_t inputHeight;
    uint32_t mvWidth;
    uint32_t mvHeight;
    float fovScale;
    uint32_t flags;
};
extern "C" {
bool SwpInit(const SwpInitInfo*);
void SwpDestroy();
bool SwpGenerateVectorsStereo(uint32_t prevTexL, uint32_t currTexL, uint32_t prevTexR,
                              uint32_t currTexR, const float* viewMatrixL, const float* viewMatrixR,
                              const float* fovL, const float* fovR, uint32_t mvTexL,
                              uint32_t mvTexR);
bool SwpExtrapolateStereo(uint32_t mvTexL, uint32_t mvTexR, uint32_t srcTexL, uint32_t srcTexR,
                          uint32_t dstTexL, uint32_t dstTexR, float scaleFactor);
uint32_t SwpLastStatus();
extern const char* SwpLogTag;
}  // extern "C"
#endif

// ---------------------------------------------------------------------------
// Internal ABI (this .so only; hidden visibility):
//   swp_egl.cpp  — decoded-frame arrival learning (the GOT wrapper target + clock).
//   swp_hooks.cpp — libvrlink_scene.so PLT GOT installation for that import.
// ---------------------------------------------------------------------------
extern "C" {
void SwpEglSetOriginal(void* originalCreateImageKHR);
bool SwpEglArrivalSnapshot(uint64_t* epoch, void** arrivalCtx, void** arrivalDpy);
int SwpEglArrivalHistory(uint64_t* outEpochs, uint64_t* outMonoNs, int maxEntries);
bool SwpEglLearningDisabled();
uint64_t SwpEglMonotonicNs();
void SwpEglArrivalObserved(void* image, void* ctx, void* dpy, uint64_t monoNs);
bool SwpHooksArm();
bool SwpHooksArmed();
// Brief-10 bridge consumer (swp_vts.cpp): the host-published carrier boolean
// over the vrlink ValueTransportSystem channel; the synthesis gate below.
void SwpVtsResetSession();
void SwpVtsSessionEnded();
void SwpVtsNoteEndFrame();
bool SwpVtsSynthesisAllowed();
bool SwpVtsArm();
}  // extern "C"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "GXRSwarp", __VA_ARGS__)

namespace {

constexpr uint32_t kStatusStandDown = 16u;   // contract bit; sticky until re-init
constexpr const char* kDisableEnv = "GXR_DISABLE_GXR_SPACEWARP";
constexpr size_t kRealHistoryCap = 4;        // prev/curr window for vector generation
constexpr size_t kArrivalHistoryCap = 8;     // newest arrivals used for the scale factor
constexpr int kClockPairsCap = 8;            // mono<->XrTime anchors (newest-first ring)
constexpr int kMaxConsecutiveMissed = 2;     // beyond this: pass stock (conservative)
constexpr float kMinScaleFactor = 0.05f;
constexpr float kMaxScaleFactor = 3.0f;      // one skipped display tick ~= 2.0

enum class TickKind { Real, Missed, Stalled };

struct FrameRecord {
    uint32_t texL = 0, texR = 0;  // projection subimage textures presented this tick
    uint64_t predDispTime = 0;    // XrTime of this tick
    uint64_t epoch = 0;           // arrival epoch at endframe
    float matL[16] = {};          // column-major view matrices (LocateViews pose)
    float matR[16] = {};
    float fovL[4] = {};           // left, right, up, down — radians, as XrFov carries them
    float fovR[4] = {};
};

struct SwapchainImages {
    std::vector<uint32_t> textures;  // index-aligned GL ids (XrSwapchainImageOpenGLESKHR.image)
    bool glesValid = false;
};

struct DispatchState {
    bool decisionMade = false;    // one-shot enable decision per session
    bool enabledForSession = false;
    bool glesBinding = false;
    bool estimatorInited = false;
    bool estimatorFailed = false;  // sticky: no per-tick retry after a failed init
    bool mvAllocTried = false;
    bool haveTickEpoch = false;
    bool haveLocate = false;
    int consecutiveMissed = 0;
    int standDownTicks = 0;       // rate-limited logging: one line per 30
    uint32_t inputW = 0, inputH = 0;
    uint32_t mvW = 0, mvH = 0;
    uint32_t mvTexL = 0, mvTexR = 0;
    bool mvValid = false;
    bool synthesisWasAllowed = false;  // bridge-gate transition guard (stale-map reset)
    void* creationCtx = nullptr;  // EGL context the frame assets were created with
    uint64_t tickEpoch = 0;       // arrival epoch snapshot of the previous endframe
    std::map<XrSwapchain, SwapchainImages> swapchains;
    std::map<XrSwapchain, uint32_t> acquiredIndex;
    std::vector<FrameRecord> realHistory;
    // mono<->XrTime anchor ring (index 0 = newest, increasing index = older):
    // arrivals convert within the pair window that contains them; entries older
    // than every window stand down (never guessed).
    uint64_t pairMono[kClockPairsCap] = {0};
    uint64_t pairXr[kClockPairsCap] = {0};
    int pairCount = 0;
    float locMatL[16] = {}, locMatR[16] = {};
    float locFovL[4] = {}, locFovR[4] = {};
};

std::recursive_mutex g_mutex;
std::map<XrSession, DispatchState> g_sessions;
std::atomic<bool> g_userEnabled{true};  // the ONE boolean enable entry's storage
std::atomic<int> g_armState{0};         // 0 = pending, 1 = armed, 2 = disabled
std::atomic<uint32_t> g_lastEstimatorStatus{0};
bool g_glTableSet = false;  // GL routing table handed to the estimator (once per process)

// Next-chain forward targets, installed once at layer-instance creation.
PFN_xrGetInstanceProcAddr g_nextGipa = nullptr;
PFN_xrCreateSession g_nextCreateSession = nullptr;
PFN_xrDestroySession g_nextDestroySession = nullptr;
PFN_xrBeginFrame g_nextBeginFrame = nullptr;
PFN_xrLocateViews g_nextLocateViews = nullptr;
PFN_xrEndFrame g_nextEndFrame = nullptr;
PFN_xrEnumerateSwapchainImages g_nextEnumerateImages = nullptr;
PFN_xrAcquireSwapchainImage g_nextAcquireImage = nullptr;

// ---------------------------------------------------------------------------
// Pure helpers (tests/swp_host_tests.cpp includes this TU and drives these).
// ---------------------------------------------------------------------------

// Column-major 4x4 view (world->camera) matrix from an eye pose (XrPosef layout:
// orientation w,x,y,z then position x,y,z).
void SwpPoseToViewMatrix(const float pos[3], const float quat[4], float out[16]) {
    const float w = quat[0], x = quat[1], y = quat[2], z = quat[3];
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    const float r00 = 1.0f - 2.0f * (yy + zz), r01 = 2.0f * (xy - wz),
                r02 = 2.0f * (xz + wy);
    const float r10 = 2.0f * (xy + wz), r11 = 1.0f - 2.0f * (xx + zz),
                r12 = 2.0f * (yz - wx);
    const float r20 = 2.0f * (xz - wy), r21 = 2.0f * (yz + wx),
                r22 = 1.0f - 2.0f * (xx + yy);
    out[0] = r00;  out[1] = r10;  out[2] = r20;  out[3] = 0.0f;
    out[4] = r01;  out[5] = r11;  out[6] = r21;  out[7] = 0.0f;
    out[8] = r02;  out[9] = r12;  out[10] = r22; out[11] = 0.0f;
    out[12] = -(r00 * pos[0] + r01 * pos[1] + r02 * pos[2]);
    out[13] = -(r10 * pos[0] + r11 * pos[1] + r12 * pos[2]);
    out[14] = -(r20 * pos[0] + r21 * pos[1] + r22 * pos[2]);
    out[15] = 1.0f;
}

// Missed-tick scaleFactor (VD C# derivation): normalized display progress past the
// newest decoded arrival, normalized by the inter-arrival span:
//   scale = (predictedDisplayTime - arrivalNewest) / (arrivalNewest - arrivalPrev)
// One fully skipped tick gives ~2.0. Returns false (tick stands down) for
// degenerate or out-of-range inputs; never silently clamps.
bool SwpScaleFactorForMissedTick(uint64_t predDisp, uint64_t arrivalPrev, uint64_t arrivalNewest,
                                 float* outScale) {
    if (!outScale || !predDisp || !arrivalPrev || !arrivalNewest) return false;
    if (arrivalNewest <= arrivalPrev || predDisp <= arrivalNewest) return false;
    const double numer = static_cast<double>(predDisp - arrivalNewest);
    const double denom = static_cast<double>(arrivalNewest - arrivalPrev);
    const double scale = numer / denom;
    if (std::isnan(scale) || scale < kMinScaleFactor || scale > kMaxScaleFactor) return false;
    *outScale = static_cast<float>(scale);
    return true;
}

// Linear arrival-clock conversion from two BeginFrame anchor pairs into XrTime;
// returns false outside the anchor span (never guesses beyond it).
bool SwpMonoToXrTime(uint64_t monoNewest, uint64_t xrNewest, uint64_t monoOlder, uint64_t xrOlder,
                     uint64_t monoArrival, uint64_t* outXr) {
    if (!outXr || monoNewest <= monoOlder || xrNewest <= xrOlder) return false;
    if (monoArrival < monoOlder || monoArrival > monoNewest) return false;
    const double spanMono = static_cast<double>(monoNewest - monoOlder);
    const double spanXr = static_cast<double>(xrNewest - xrOlder);
    const double offset = static_cast<double>(monoArrival - monoOlder);
    const double xr = static_cast<double>(xrOlder) + (offset / spanMono) * spanXr;
    *outXr = static_cast<uint64_t>(xr);
    return true;
}

// Shared-brief candidate 1: the newest arrival epoch unchanged since the previous
// endframe => the same decoded arrival is being presented again => MISSed tick.
// Learning disabled => STALLED (never extrapolate). First observed tick => Real.
TickKind SwpClassifyTick(uint64_t epochNow, uint64_t epochPrev, bool haveEpochPrev,
                         bool learningDisabled) {
    if (learningDisabled) return TickKind::Stalled;
    if (!haveEpochPrev) return TickKind::Real;
    if (epochNow == epochPrev) return TickKind::Missed;
    return TickKind::Real;
}

bool EnvDisabled() {
    static const bool disabled = std::getenv(kDisableEnv) != nullptr;
    return disabled;
}

}  // namespace

// ---------------------------------------------------------------------------
// Skeleton stubs for the estimator contract (built only when spacewarp_est.h is
// absent AND the host tests do not supply fakes). All fail safe: the layer stays
// a passthrough and the status bit reports stand-down.
// ---------------------------------------------------------------------------
#if !defined(SWP_HAVE_ESTIMATOR)
#ifndef SWP_HOST_TESTS
namespace {
std::atomic<bool> g_stubLogged{false};  // one skeleton log (unused outside this block)
}  // namespace
extern "C" {
bool SwpInit(const SwpInitInfo*) {
    if (!g_stubLogged.exchange(true)) {
        LOG("estimator not linked (skeleton build); standing down (worst case = stock)");
    }
    g_lastEstimatorStatus.store(kStatusStandDown);
    return false;
}
void SwpDestroy() {}
bool SwpGenerateVectorsStereo(uint32_t, uint32_t, uint32_t, uint32_t, const float*, const float*,
                              const float*, const float*, uint32_t, uint32_t) {
    g_lastEstimatorStatus.store(kStatusStandDown);
    return false;
}
bool SwpExtrapolateStereo(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float) {
    g_lastEstimatorStatus.store(kStatusStandDown);
    return false;
}
uint32_t SwpLastStatus() { return g_lastEstimatorStatus.load(); }
}  // extern "C"
const char* SwpLogTag = "GXRSwarp";
#endif  // SWP_HOST_TESTS
#endif  // !SWP_HAVE_ESTIMATOR

namespace {

DispatchState* StateForLocked(XrSession session) {
    auto it = g_sessions.find(session);
    return it == g_sessions.end() ? nullptr : &it->second;
}

void RateLimitedLog(DispatchState& state, const char* what) {
    state.standDownTicks++;
    if (state.standDownTicks % 30 == 1) {
        LOG("%s (stand down this tick; occurrences=%d status=0x%x)", what, state.standDownTicks,
            g_lastEstimatorStatus.load());
    }
}

// Walk the create-info chain for the GLES graphics binding
// (XR_KHR_opengl_es_enablement: the only binding this layer can draw with).
bool HasGlesBinding(const XrSessionCreateInfo* info) {
    for (const XrBaseInStructure* node = static_cast<const XrBaseInStructure*>(info->next); node;
         node = node->next) {
        if (node->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR) return true;
    }
    return false;
}

// One-shot estimator bring-up: GL table (context-agnostic routing per the
// sibling header's contract), init from live projection extents, resolved mv-map
// size for the caller-owned mv textures. Failure stands this session down
// PERMANENTLY (no per-tick retry/log storm); the contract status bit 16 persists
// until the next re-init (= next session).
bool EnsureEstimatorReadyLocked(DispatchState& state) {
    if (state.estimatorInited) return true;
    if (state.estimatorFailed) return false;
    if (state.inputW == 0 || state.inputH == 0) return false;

#ifdef SWP_HAVE_ESTIMATOR
    // The GL routing table is set once per process BEFORE the first SwpInit, with
    // the client's context current (the dispatch runs inside the verified context
    // path). The table keeps a stable address (sibling contract, EstiSetTable).
    if (!g_glTableSet) {
        static SwpGlTable table{};
        static bool tableBuilt = false;
        if (!tableBuilt) {
            table.glCreateShader = &glCreateShader;
            table.glShaderSource = &glShaderSource;
            table.glCompileShader = &glCompileShader;
            table.glGetShaderiv = &glGetShaderiv;
            table.glGetShaderInfoLog = &glGetShaderInfoLog;
            table.glDeleteShader = &glDeleteShader;
            table.glCreateProgram = &glCreateProgram;
            table.glAttachShader = &glAttachShader;
            table.glLinkProgram = &glLinkProgram;
            table.glGetProgramiv = &glGetProgramiv;
            table.glGetProgramInfoLog = &glGetProgramInfoLog;
            table.glDeleteProgram = &glDeleteProgram;
            table.glGetUniformLocation = &glGetUniformLocation;
            table.glUniform1i = &glUniform1i;
            table.glUniform2i = &glUniform2i;
            table.glUniform1f = &glUniform1f;
            table.glUseProgram = &glUseProgram;
            table.glDrawArrays = &glDrawArrays;
            table.glViewport = &glViewport;
            table.glFlush = &glFlush;
            table.glGetError = &glGetError;
            table.glGenFramebuffers = &glGenFramebuffers;
            table.glDeleteFramebuffers = &glDeleteFramebuffers;
            table.glBindFramebuffer = &glBindFramebuffer;
            table.glFramebufferTexture2D = &glFramebufferTexture2D;
            table.glCheckFramebufferStatus = &glCheckFramebufferStatus;
            table.glGenTextures = &glGenTextures;
            table.glDeleteTextures = &glDeleteTextures;
            table.glBindTexture = &glBindTexture;
            table.glActiveTexture = &glActiveTexture;
            table.glTexStorage2D = &glTexStorage2D;
            table.glTexParameteri = &glTexParameteri;
            table.glGetString = &glGetString;
            table.glGetIntegerv = &glGetIntegerv;
            table.getCurrentContext = []() -> void* {
                return reinterpret_cast<void*>(eglGetCurrentContext());
            };
            table.nowMicros = []() -> unsigned long long {
                return SwpEglMonotonicNs() / 1000ull;
            };
            tableBuilt = true;
        }
        if (!SwpSetGlTable(&table)) {
            state.estimatorFailed = true;
            g_lastEstimatorStatus.store(SwpLastStatus() | kStatusStandDown);
            LOG("GL routing table rejected; standing down for this session");
            tableBuilt = false;
            return false;
        }
        g_glTableSet = true;
    }
#endif

    SwpInitInfo init{};
    init.inputWidth = state.inputW;
    init.inputHeight = state.inputH;
#ifdef SWP_HAVE_ESTIMATOR
    init.mvWidth = 0;                       // 0 => the estimator's documented default
    init.mvHeight = 0;
    init.fovScale = 0.0f;                   // 0 => 1.0 (same-FOV path)
    init.flags = 0;                         // QCOM when present; conservative budget
    if (!SwpInit(&init)) {
        state.estimatorFailed = true;
        g_lastEstimatorStatus.store(SwpLastStatus() | kStatusStandDown);
        LOG("estimator init failed (status=0x%x); standing down for this session",
            SwpLastStatus());
        return false;
    }
    uint32_t mvW = 0, mvH = 0;
    if (!SwpResolvedMvSize(&mvW, &mvH) || mvW == 0 || mvH == 0) {
        state.estimatorFailed = true;
        g_lastEstimatorStatus.store(SwpLastStatus() | kStatusStandDown);
        LOG("estimator did not resolve an mv-map size; standing down");
        SwpDestroy();
        return false;
    }
    state.mvW = mvW;
    state.mvH = mvH;
#else
    // Skeleton (no estimator in the build): fail safe like a failed init.
    state.estimatorFailed = true;
    g_lastEstimatorStatus.store(kStatusStandDown);
    LOG("estimator core absent (skeleton build); standing down (worst case = stock)");
    return false;
#endif

    state.estimatorInited = true;
    g_lastEstimatorStatus.store(SwpLastStatus());
    LOG("estimator initialized input=%ux%u mv=%ux%u (fovScale default 1.0)", state.inputW,
        state.inputH, state.mvW, state.mvH);
    return true;
}

// Allocate the MV-map textures in the client's CURRENT context (RGBA16F per the
// contract's mv map format; immutable storage via TexStorage2D for the FBO path).
// Never creates or binds a context; fails (-> tick stand-down) without one.
bool AllocateMvTextures(DispatchState& state) {
    if (state.mvAllocTried) return state.mvTexL != 0 && state.mvTexR != 0;
    state.mvAllocTried = true;
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) return false;
    if (state.mvW == 0 || state.mvH == 0) return false;
    glGenTextures(1, &state.mvTexL);
    glGenTextures(1, &state.mvTexR);
    if (!state.mvTexL || !state.mvTexR) return false;
    for (uint32_t tex : {state.mvTexL, state.mvTexR}) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, state.mvW, state.mvH);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    LOG("mv textures allocated %ux%u RGBA16F (ids %u/%u)", state.mvW, state.mvH, state.mvTexL,
        state.mvTexR);
    return true;
}

// GL texture id behind one projection view of THIS endframe: subImage.swapchain
// plus the pending (acquired) index -> the image the compositor will read.
bool ResolvePendingTextureLocked(DispatchState& state, const XrCompositionLayerProjectionView& view,
                                 uint32_t* outTex) {
    auto imagesIt = state.swapchains.find(view.subImage.swapchain);
    auto indexIt = state.acquiredIndex.find(view.subImage.swapchain);
    if (imagesIt == state.swapchains.end() || indexIt == state.acquiredIndex.end() ||
        !imagesIt->second.glesValid || indexIt->second >= imagesIt->second.textures.size()) {
        return false;
    }
    *outTex = imagesIt->second.textures[indexIt->second];
    return true;
}

bool ArrivalXrTime(DispatchState& state, uint64_t arrivalMono, uint64_t* outXr) {
    if (!outXr) return false;
    if (state.pairCount < 2) return false;  // need a window (two anchors) to convert
    // Try the pair windows newest-first: [mono(i+1) .. mono(i)] with xr mapped the
    // same way. An arrival older than every window stands down (never guessed).
    for (int i = 0; i + 1 < state.pairCount; i++) {
        if (SwpMonoToXrTime(state.pairMono[i], state.pairXr[i], state.pairMono[i + 1],
                            state.pairXr[i + 1], arrivalMono, outXr)) {
            return true;
        }
    }
    return false;
}

void RecordRealTickLocked(DispatchState& state, uint32_t texL, uint32_t texR, uint64_t epoch,
                          uint64_t predDispTime) {
    FrameRecord record;
    record.texL = texL;
    record.texR = texR;
    record.predDispTime = predDispTime;
    record.epoch = epoch;
    std::memcpy(record.matL, state.locMatL, sizeof(record.matL));
    std::memcpy(record.matR, state.locMatR, sizeof(record.matR));
    std::memcpy(record.fovL, state.locFovL, sizeof(record.fovL));
    std::memcpy(record.fovR, state.locFovR, sizeof(record.fovR));
    state.realHistory.push_back(record);
    if (state.realHistory.size() > kRealHistoryCap) {
        state.realHistory.erase(state.realHistory.begin());
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Forwarding install (xr_layer_entry.cpp calls this once, at instance creation).
// A null pointer aborts layer creation so the app falls back to its stock path —
// mirrors codecs-support-layer's LOAD discipline.
// ---------------------------------------------------------------------------
extern "C" bool SwpDispatchSetNext(PFN_xrGetInstanceProcAddr gipa,
                                   PFN_xrCreateSession createSession,
                                   PFN_xrDestroySession destroySession,
                                   PFN_xrBeginFrame beginFrame,
                                   PFN_xrLocateViews locateViews, PFN_xrEndFrame endFrame,
                                   PFN_xrEnumerateSwapchainImages enumerateSwapchainImages,
                                   PFN_xrAcquireSwapchainImage acquireSwapchainImage) {
    if (!gipa || !createSession || !destroySession || !beginFrame || !locateViews || !endFrame ||
        !enumerateSwapchainImages || !acquireSwapchainImage) {
        return false;
    }
    g_nextGipa = gipa;
    g_nextCreateSession = createSession;
    g_nextDestroySession = destroySession;
    g_nextBeginFrame = beginFrame;
    g_nextLocateViews = locateViews;
    g_nextEndFrame = endFrame;
    g_nextEnumerateImages = enumerateSwapchainImages;
    g_nextAcquireImage = acquireSwapchainImage;
    return true;
}

// ---------------------------------------------------------------------------
// Public dispatch entry points (routed by xr_layer_entry.cpp; every path forwards —
// the layer never swallows a call).
// ---------------------------------------------------------------------------
extern "C" {

void SwpDispatchTryArm() {
    if (g_armState.load(std::memory_order_relaxed) != 0) return;
    if (EnvDisabled()) {
        g_armState.store(2, std::memory_order_relaxed);
        LOG("disabled by %s", kDisableEnv);
        return;
    }
    if (SwpHooksArm()) {
        g_armState.store(1, std::memory_order_relaxed);
    }
    // Otherwise the scene library is not loaded yet: retried from later XR calls
    // (implicit layers mount before the scene lib; a constructor-only scan misses it).
    // The bridge is armed INDEPENDENTLY (swp_vts.cpp owns its own capture slots +
    // pins): an egl-hook failure never blocks it and vice versa.
    SwpVtsArm();
}

XrResult SwpDispatchCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo,
                                  XrSession* session) {
    SwpDispatchTryArm();
    const XrResult result = g_nextCreateSession(instance, createInfo, session);
    if (XR_FAILED(result) || !session || !*session || !createInfo) return result;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    DispatchState state;
    state.glesBinding = HasGlesBinding(createInfo);
    // One-shot enable decision per session: the single boolean entry is read here.
    // A mid-session flip applies to the NEXT session; never mid-frame. The brief-10
    // bridge gate composes on top of this per tick (synthesis requires BOTH this
    // entry AND the host-published VTS carrier; stock sessions never auto-enable).
    state.enabledForSession = state.glesBinding && g_userEnabled.load() && !EnvDisabled();
    if (!state.glesBinding) {
        LOG("session carries no XrGraphicsBindingOpenGLESAndroidKHR; spacewarp inactive");
    }
    state.decisionMade = true;
    // Per-session bridge state: one-line bridge logs + carrier cache restart
    // (the layer never carries an enable assumption across sessions).
    SwpVtsResetSession();
    g_sessions[*session] = state;
    return result;
}

XrResult SwpDispatchDestroySession(XrSession session) {
    const XrResult result = g_nextDestroySession(session);
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    auto it = g_sessions.find(session);
    if (it == g_sessions.end()) return result;
    DispatchState& state = it->second;
    // This session's SVLRoot must never be read again (it may be destroyed with
    // the session; the next live session re-captures its own root).
    SwpVtsSessionEnded();
    // MV textures live in the client's context: delete only while that exact context
    // is still current, else leave the two small RGBA16F textures (a few KB once per
    // session; destroying GL names without their context is invalid GL usage).
    if (state.mvTexL && state.mvTexR && state.creationCtx &&
        eglGetCurrentContext() == state.creationCtx) {
        glDeleteTextures(1, &state.mvTexL);
        glDeleteTextures(1, &state.mvTexR);
    }
    if (state.estimatorInited) {
        SwpDestroy();
        state.estimatorInited = false;
    }
    g_sessions.erase(it);
    return result;
}

XrResult SwpDispatchBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo) {
    // Pure readiness trigger + passthrough: no state capture (the predicted display
    // time is taken from each tick's XrFrameEndInfo.displayTime below — OpenXR's
    // xrBeginFrame carries only a begin marker, the predictions live in WaitFrame
    // results the app re-validates, and endframe's displayTime is this tick's
    // compositor deadline).
    SwpDispatchTryArm();
    return g_nextBeginFrame(session, frameBeginInfo);
}

XrResult SwpDispatchLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo,
                                XrViewState* viewState, uint32_t viewCapacityInput,
                                uint32_t* viewCountOutput, XrView* views) {
    const XrResult result = g_nextLocateViews(session, viewLocateInfo, viewState,
                                              viewCapacityInput, viewCountOutput, views);
    if (XR_FAILED(result) || !views || !viewState || !viewCountOutput || *viewCountOutput < 2 ||
        viewCapacityInput < 2 ||
        (viewState->viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT |
                                      XR_VIEW_STATE_ORIENTATION_VALID_BIT)) == 0) {
        return result;
    }
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    DispatchState* state = StateForLocked(session);
    if (!state) return result;
    for (int eye = 0; eye < 2; eye++) {
        const XrView& view = views[eye];
        const float pos[3] = {view.pose.position.x, view.pose.position.y, view.pose.position.z};
        const float quat[4] = {view.pose.orientation.w, view.pose.orientation.x,
                               view.pose.orientation.y, view.pose.orientation.z};
        float* mat = eye == 0 ? state->locMatL : state->locMatR;
        float* fov = eye == 0 ? state->locFovL : state->locFovR;
        SwpPoseToViewMatrix(pos, quat, mat);
        fov[0] = view.fov.angleLeft;
        fov[1] = view.fov.angleRight;
        fov[2] = view.fov.angleUp;
        fov[3] = view.fov.angleDown;
    }
    state->haveLocate = true;
    return result;
}

XrResult SwpDispatchEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    SwpDispatchTryArm();
    std::lock_guard<std::recursive_mutex> lock(g_mutex);

    DispatchState* state = StateForLocked(session);
    if (!state || !state->enabledForSession) return g_nextEndFrame(session, frameEndInfo);

    // Clock anchor pair for arrival<->XrTime conversion: this tick's compositor
    // deadline (XrFrameEndInfo.displayTime) paired with the monotonic now.
    for (int i = state->pairCount - 1; i > 0 && i < kClockPairsCap; i--) {
        state->pairMono[i] = state->pairMono[i - 1];
        state->pairXr[i] = state->pairXr[i - 1];
    }
    if (state->pairCount < kClockPairsCap) state->pairCount++;
    state->pairMono[0] = SwpEglMonotonicNs();
    state->pairXr[0] = frameEndInfo ? frameEndInfo->displayTime : 0;
    const uint64_t predDispTime = state->pairXr[0];

    uint64_t epochNow = 0;
    void* arrivalCtx = nullptr;
    void* arrivalDpy = nullptr;
    const bool haveArrival = SwpEglArrivalSnapshot(&epochNow, &arrivalCtx, &arrivalDpy);
    const bool learningDisabled = SwpEglLearningDisabled();

    if (!haveArrival || learningDisabled) {
        RateLimitedLog(*state, learningDisabled ? "arrival learning disabled"
                                                : "arrival snapshot unavailable");
        return g_nextEndFrame(session, frameEndInfo);
    }

    const TickKind kind = SwpClassifyTick(epochNow, state->tickEpoch, state->haveTickEpoch, false);
    state->tickEpoch = epochNow;
    state->haveTickEpoch = true;

    // This endframe's projection subimage data = exactly what the compositor reads.
    const XrCompositionLayerProjection* proj = nullptr;
    if (frameEndInfo) {
        for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
            if (frameEndInfo->layers[i] &&
                frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                proj = reinterpret_cast<const XrCompositionLayerProjection*>(
                    frameEndInfo->layers[i]);
                break;
            }
        }
    }
    if (proj && proj->viewCount != 2) {
        // Unsupported projection shape (e.g. single-eye): pass stock forever, log once.
        state->enabledForSession = false;
        static std::atomic<bool> loggedViewCount{false};
        bool expected = false;
        if (loggedViewCount.compare_exchange_strong(expected, true)) {
            LOG("projection viewCount=%u unsupported; spacewarp inactive for the session",
                proj->viewCount);
        }
        proj = nullptr;
    }
    if (!proj) return g_nextEndFrame(session, frameEndInfo);

    // (Re)arm the estimator extents from the live subimage rect; a mid-session
    // size change re-inits the estimator and drops the maps (fail-safe).
    if (!state->inputW || !state->inputH ||
        state->inputW != static_cast<uint32_t>(proj->views[0].subImage.imageRect.extent.width) ||
        state->inputH !=
            static_cast<uint32_t>(proj->views[0].subImage.imageRect.extent.height)) {
        state->inputW = proj->views[0].subImage.imageRect.extent.width;
        state->inputH = proj->views[0].subImage.imageRect.extent.height;
        if (state->estimatorInited) {
            SwpDestroy();
            state->estimatorInited = false;
            state->mvValid = false;
            state->mvAllocTried = false;
            state->mvTexL = 0;
            state->mvTexR = 0;
            state->realHistory.clear();
        }
    }

    uint32_t texL = 0, texR = 0;
    if (!ResolvePendingTextureLocked(*state, proj->views[0], &texL) ||
        !ResolvePendingTextureLocked(*state, proj->views[1], &texR)) {
        RateLimitedLog(*state, "pending swapchain textures unknown");
        return g_nextEndFrame(session, frameEndInfo);
    }

    // -----------------------------------------------------------------------
    // Brief-10 bridge gate: ALL synthesis (MV generation + extrapolation) runs
    // only while the bridge carrier reads enabled; otherwise the tick is plain
    // stock passthrough (the layer's observers keep learning: arrival learning,
    // tick classification, real-tick history).
    // -----------------------------------------------------------------------
    SwpVtsNoteEndFrame();
    const bool synthesisAllowed = SwpVtsSynthesisAllowed();
    if (synthesisAllowed && !state->synthesisWasAllowed) {
        // Fresh enablement (session start or a mid-session bridge flip): stale
        // maps must never warp old content; require a fresh real-tick generation
        // before any extrapolation, and restart the missed-tick count.
        state->mvValid = false;
        state->consecutiveMissed = 0;
    }
    state->synthesisWasAllowed = synthesisAllowed;
    if (!synthesisAllowed) {
        if (kind == TickKind::Real) {
            // Observation continues without synthesis: history keeps the presented
            // pair so a later enablement is bounded by the fresh-generation rule
            // above (no extrapolation ever runs from pre-disable content).
            RecordRealTickLocked(*state, texL, texR, epochNow, predDispTime);
            state->consecutiveMissed = 0;
        }
        return g_nextEndFrame(session, frameEndInfo);
    }

    // CRITICAL: draw only with the client's current EGL context, and only when it
    // matches the context the frame assets were created with (when the arrival was
    // created with an explicit context; context-less images are checked for simply
    // "a non-null context is current"). This layer never creates or currents its
    // own context in this iteration (worst case = stock).
    const EGLContext currentCtx = eglGetCurrentContext();
    if (currentCtx == EGL_NO_CONTEXT || (arrivalCtx && currentCtx != arrivalCtx)) {
        RateLimitedLog(*state, "current EGL context does not match the frame-asset context");
        return g_nextEndFrame(session, frameEndInfo);
    }
    if (!state->creationCtx) state->creationCtx = arrivalCtx;
#ifdef SWP_HAVE_ESTIMATOR
    // Identity anchor: the estimator re-checks against THIS binding on every call
    // (sibling header: never assume a context).
    SwpBindIdentityContext(reinterpret_cast<void*>(currentCtx));
#endif

    if (!EnsureEstimatorReadyLocked(*state) || !AllocateMvTextures(*state)) {
        return g_nextEndFrame(session, frameEndInfo);
    }

    if (kind == TickKind::Real) {
        // Real tick: prev=older, curr=newest, with the LocateViews pose of the tick.
        if (!state->realHistory.empty()) {
            const FrameRecord& prev = state->realHistory.back();
            if (prev.texL && prev.texL != texL && prev.texR != texR) {
                if (SwpGenerateVectorsStereo(prev.texL, texL, prev.texR, texR, prev.matL,
                                             prev.matR, prev.fovL, prev.fovR, state->mvTexL,
                                             state->mvTexR)) {
                    state->mvValid = true;
                    g_lastEstimatorStatus.store(SwpLastStatus());
                } else {
                    state->mvValid = false;
                    g_lastEstimatorStatus.store(SwpLastStatus());
                    RateLimitedLog(*state, "vector generation failed");
                }
            }
        }
        RecordRealTickLocked(*state, texL, texR, epochNow, predDispTime);
        state->consecutiveMissed = 0;
        return g_nextEndFrame(session, frameEndInfo);
    }
    if (kind == TickKind::Stalled) return g_nextEndFrame(session, frameEndInfo);

    // Missed tick: extrapolate the most recent presented pair into the pending dst.
    state->consecutiveMissed++;
    if (state->consecutiveMissed > kMaxConsecutiveMissed) {
        RateLimitedLog(*state, "consecutive missed-tick cap; passing stock");
        return g_nextEndFrame(session, frameEndInfo);
    }
    if (!state->mvValid || state->realHistory.size() < 2) {
        RateLimitedLog(*state, "no valid maps for extrapolation");
        return g_nextEndFrame(session, frameEndInfo);
    }

    uint64_t arrivals[kArrivalHistoryCap] = {0};
    uint64_t arrivalMono[kArrivalHistoryCap] = {0};
    // The display moment this missed tick is composing for = THIS tick's
    // predictedDisplayTime (captured at its xrBeginFrame).
    const uint64_t predForScale = state->pairXr[0];
    const int historyCount = SwpEglArrivalHistory(arrivals, arrivalMono, kArrivalHistoryCap);
    uint64_t arrNewestXr = 0, arrPrevXr = 0;
    if (historyCount < 2) {
        RateLimitedLog(*state, "insufficient arrival history");
        return g_nextEndFrame(session, frameEndInfo);
    }
    if (!ArrivalXrTime(*state, arrivalMono[0], &arrNewestXr) ||
        !ArrivalXrTime(*state, arrivalMono[1], &arrPrevXr)) {
        RateLimitedLog(*state, "arrival clock conversion unavailable");
        return g_nextEndFrame(session, frameEndInfo);
    }
    float scale = 0.0f;
    if (!SwpScaleFactorForMissedTick(predForScale, arrPrevXr, arrNewestXr, &scale)) {
        RateLimitedLog(*state, "scale factor out of sane range");
        return g_nextEndFrame(session, frameEndInfo);
    }
    const FrameRecord& src = state->realHistory.back();
    if (SwpExtrapolateStereo(state->mvTexL, state->mvTexR, src.texL, src.texR, texL, texR, scale)) {
        g_lastEstimatorStatus.store(SwpLastStatus());
    } else {
        g_lastEstimatorStatus.store(SwpLastStatus());
        RateLimitedLog(*state, "extrapolation failed");
    }
    return g_nextEndFrame(session, frameEndInfo);
}

// Passive observers for texture resolution (never mutate, full passthrough):
// XrSwapchainImageOpenGLESKHR.image ids per swapchain + the pending acquire index.
XrResult SwpDispatchEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t countInput,
                                             uint32_t* countOutput,
                                             XrSwapchainImageBaseHeader* images) {
    const XrResult result = g_nextEnumerateImages(swapchain, countInput, countOutput, images);
    if (XR_SUCCEEDED(result) && images && countOutput && *countOutput > 0) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        // XrSwapchainImageOpenGLESKHR (openxr_platform.h): (type, next, image: uint32_t),
        // 24 bytes with tail padding. Layout-mirrored locally so this TU does not
        // depend on which XR_USE_* defines the build passes.
        struct SwapchainImageGles {
            XrStructureType type;
            const void* next;
            uint32_t image;
            uint32_t pad;
        };
        static_assert(sizeof(SwapchainImageGles) == 24, "GLES swapchain image stride mismatch");
        for (auto& sessionPair : g_sessions) {
            DispatchState& state = sessionPair.second;
            SwapchainImages& entry = state.swapchains[swapchain];
            entry.glesValid = true;
            entry.textures.clear();
            for (uint32_t i = 0; i < *countOutput; i++) {
                SwapchainImageGles probe{};
                std::memcpy(&probe, reinterpret_cast<const char*>(images) + i * sizeof(probe),
                            sizeof(probe));
                if (probe.type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) {
                    entry.glesValid = false;  // not a GLES swapchain: pass stock
                    break;
                }
                entry.textures.push_back(probe.image);
            }
        }
    }
    return result;
}

XrResult SwpDispatchAcquireSwapchainImage(XrSwapchain swapchain,
                                          const XrSwapchainImageAcquireInfo* acquireInfo,
                                          uint32_t* index) {
    const XrResult result = g_nextAcquireImage(swapchain, acquireInfo, index);
    if (XR_SUCCEEDED(result) && index) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        for (auto& sessionPair : g_sessions) {
            sessionPair.second.acquiredIndex[swapchain] = *index;
        }
    }
    return result;
}

// The ONE boolean enable entry (RE-SHARED-IMPL.md), now AND-gated with the
// brief-10 VTS bridge carrier per tick: synthesis requires BOTH the entry AND
// the host-published carrier to be on; stock sessions (carrier unpublished)
// never auto-enable. Flipping the entry applies to the NEXT session; the
// bridge's own flips take effect live within the 60-endframe poll cadence.
// Exported (default visibility) so a bridge workstream can dlsym it.
#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif
EXPORT void SwpSetEnabled(bool enabled) {
    g_userEnabled.store(enabled, std::memory_order_relaxed);
}

EXPORT bool SwpGetEnabled() { return g_userEnabled.load(std::memory_order_relaxed); }

}  // extern "C"
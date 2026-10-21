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
// glGetTexLevelParameteriv entered the core in ES 3.1 (the estimator requires
// 3.2), so the format probe uses the 3.2 header. The host-test stubs declare
// the same entry point in their gl3.h shim.
#if !defined(SWP_HOST_TESTS)
#include <GLES3/gl32.h>
#endif
#include <android/log.h>
#include <jni.h>  // openxr_platform.h's Android extension blocks name jni types

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// The NDK's GLES3/gl3.h does not export this pname (it is used by the format
// probe below); the value is the GLES 3.0 token.
#ifndef GL_TEXTURE_INTERNAL_FORMAT
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003
#endif

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
// Published carrier value (0 off / 1 late frames only / 2 half rate).
uint32_t SwpVtsMode();
// Monotonic carrier-change counter (per-session stale-state detection).
uint64_t SwpVtsGeneration();
bool SwpVtsArm();
}  // extern "C"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "GXRSwarp", __VA_ARGS__)

namespace {

constexpr uint32_t kStatusStandDown = 16u;   // contract bit; sticky until re-init
constexpr const char* kDisableEnv = "GXR_DISABLE_GXR_SPACEWARP";
constexpr size_t kRealHistoryCap = 4;        // prev/curr window for vector generation
constexpr int kMaxSlots = 2;                 // video projections: base + foveated
constexpr uint32_t kMaxProjectionLayers = 8; // layer-array scan bound
constexpr uint32_t kModeHalfRate = 2;        // carrier value for half-rate mode
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

// One selected video projection and its own arrival history. The two slots
// share the estimator's uv field (identical extent and view geometry — RE
// 2026-10-20) but never share textures, so each keeps its own prev/curr pair.
struct SlotState {
    XrSwapchain swapL = XR_NULL_HANDLE;
    XrSwapchain swapR = XR_NULL_HANDLE;
    uint32_t texL = 0, texR = 0;  // this tick's pending images
    std::vector<FrameRecord> history;
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
    bool arrivalUnavailableLogged = false;  // one-shot: no arrival source this session
    uint32_t inputW = 0, inputH = 0;
    uint32_t mvW = 0, mvH = 0;
    uint32_t mvTexL = 0, mvTexR = 0;
    bool mvValid = false;
    bool synthesisWasAllowed = false;  // bridge-gate transition guard (stale-map reset)
    void* creationCtx = nullptr;  // EGL context the frame assets were created with
    uint64_t tickEpoch = 0;       // arrival epoch snapshot of the previous endframe
    std::map<XrSwapchain, SwapchainImages> swapchains;
    std::map<XrSwapchain, uint32_t> acquiredIndex;
    SlotState slots[kMaxSlots];
    int slotCount = 0;
    // Presentation cadence (half-rate mode): did the previous presented tick
    // carry a real decoded frame, and has anything been presented yet?
    bool havePresented = false;
    bool lastPresentedWasReal = false;
    // Owned staging pair per slot. The warp lands here first and reaches the
    // swapchain only after EVERY selected slot succeeded, so a partial failure
    // can never leave mixed stereo or a mixed slot set.
    uint32_t staging[kMaxSlots][2] = {{0, 0}, {0, 0}};
    int stageW = 0, stageH = 0;
    GLint stageFormat = 0;   // internal format of the destination swapchain
    GLuint copyReadFbo = 0, copyDrawFbo = 0;
    // B1 (enabled-only ownership): every GL name this file creates is counted per
    // session, so the settled-Off state is a measured zero rather than a claim.
    // Estimator-owned names (programs plus its own textures/FBO) are torn down by
    // SwpDestroy and are not counted here.
    uint32_t liveTextures = 0;
    uint32_t liveFbos = 0;
    bool ownsCounted = false;       // this session is present in g_synthOwners
    // Last carrier mode seen, so a 1 <-> 2 flip also drops stale maps.
    uint32_t lastMode = 0;
    // Carrier generation at the last synthesis tick: a 1 -> 0 -> 1 cycle leaves
    // lastMode identical, so the generation is what marks the dormant gap.
    uint64_t modeGeneration = 0;
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

// Missed-tick scaleFactor (VD convention: normalized progress between the two
// history frame indexes, 1.0 == exactly at the newest arrival):
//   scale = (predictedDisplayTime - arrivalPrev) / (arrivalNewest - arrivalPrev)
// The extrapolation shader applies (1 - scale) to the stored uv field, so one fully
// skipped tick (~2.0) is one interval of displacement; a numerator measured
// from arrivalNewest would make the first skipped tick a zero-motion copy.
// Returns false (tick stands down) for degenerate or out-of-range inputs;
// never silently clamps.
bool SwpScaleFactorForMissedTick(uint64_t predDisp, uint64_t arrivalPrev, uint64_t arrivalNewest,
                                 float* outScale) {
    if (!outScale || !predDisp || !arrivalPrev || !arrivalNewest) return false;
    if (arrivalNewest <= arrivalPrev || predDisp <= arrivalNewest) return false;
    const double numer = static_cast<double>(predDisp - arrivalPrev);
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
            table.glIsEnabled = &glIsEnabled;
            table.glEnable = &glEnable;
            table.glDisable = &glDisable;
            table.glColorMask = &glColorMask;
            table.glGetBooleanv = &glGetBooleanv;
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

// --- B1: enabled-only ownership and context-safe release ---------------------
//
// Everything this file allocates (mv pair, staging quad, copy FBOs) is owned by
// the session that created it and by the EGL context current at that moment.
// Deletion is only valid while that same context is current, so a release that
// cannot run is deferred into a bounded pending set and drained from a later
// frame callback with the matching context - never silently dropped, and never
// issued against an unrelated current context.

std::atomic<uint32_t> g_synthOwners{0};  // sessions holding live ASW-owned GL names
std::atomic<bool> g_deferredPending{false};
std::atomic<uint32_t> g_leakedTextures{0};  // reported, unreclaimable (no matching context)
std::atomic<uint32_t> g_leakedFbos{0};

struct DeferredReclaim {
    void* ctx = nullptr;
    GLuint textures[24] = {};
    GLuint fbos[8] = {};
    uint32_t textureCount = 0;
    uint32_t fboCount = 0;
};
DeferredReclaim g_deferred;

bool ContextIsOwner(const DispatchState& state, void* ctx) {
    return ctx && ctx != EGL_NO_CONTEXT && state.creationCtx && ctx == state.creationCtx;
}

void NoteOwnedLocked(DispatchState& state, uint32_t textures, uint32_t fbos) {
    if (!state.ownsCounted) {
        state.ownsCounted = true;
        g_synthOwners.fetch_add(1, std::memory_order_relaxed);
    }
    state.liveTextures += textures;
    state.liveFbos += fbos;
}

void NoteReleasedLocked(DispatchState& state, uint32_t textures, uint32_t fbos) {
    state.liveTextures = textures > state.liveTextures ? 0u : state.liveTextures - textures;
    state.liveFbos = fbos > state.liveFbos ? 0u : state.liveFbos - fbos;
    if (state.liveTextures == 0 && state.liveFbos == 0 && state.ownsCounted) {
        state.ownsCounted = false;
        g_synthOwners.fetch_sub(1, std::memory_order_relaxed);
    }
}

// Move a session's still-live names into the bounded pending set. Used when a
// session ends while its context is not current: the names cannot be deleted from
// here, and discarding them would leak. Returns false when the queue cannot take
// them, so the caller can report the leak instead of stranding the owner counter.
bool DeferReclaimLocked(DispatchState& state) {
    if (state.liveTextures == 0 && state.liveFbos == 0) return true;
    if (!state.creationCtx) {
        LOG("cannot defer reclaim: no creating context was recorded (%u textures, %u framebuffers)",
            state.liveTextures, state.liveFbos);
        return false;
    }
    if (g_deferred.ctx && g_deferred.ctx != state.creationCtx) {
        LOG("deferred reclaim holds another context; %u textures, %u framebuffers cannot be queued",
            state.liveTextures, state.liveFbos);
        return false;
    }
    g_deferred.ctx = state.creationCtx;
    uint32_t tex = 0, fbo = 0;
    auto queueTexture = [&](GLuint& id) {
        if (!id || g_deferred.textureCount >= 24) return;
        g_deferred.textures[g_deferred.textureCount++] = id;
        id = 0;
        ++tex;
    };
    auto queueFbo = [&](GLuint& id) {
        if (!id || g_deferred.fboCount >= 8) return;
        g_deferred.fbos[g_deferred.fboCount++] = id;
        id = 0;
        ++fbo;
    };
    queueTexture(state.mvTexL);
    queueTexture(state.mvTexR);
    for (int s = 0; s < kMaxSlots; s++) {
        for (int e = 0; e < 2; e++) queueTexture(state.staging[s][e]);
    }
    queueFbo(state.copyReadFbo);
    queueFbo(state.copyDrawFbo);
    state.stageW = state.stageH = 0;
    state.stageFormat = 0;
    state.mvAllocTried = false;
    state.mvValid = false;
    NoteReleasedLocked(state, tex, fbo);
    g_deferredPending.store(true, std::memory_order_relaxed);
    LOG("queued %u textures, %u framebuffers for reclaim on their creating context", tex, fbo);
    return true;
}

// SwpDestroy tears down the process-wide estimator singleton that every session
// shares. Clearing only the calling session's flag would leave another session
// believing its estimator survived (EnsureEstimatorReadyLocked returns early on the
// stale flag, and every later estimator call fails the entry gate), so clear all.
void DestroyEstimatorForAllSessionsLocked() {
    bool anyInited = false;
    for (auto& entry : g_sessions) {
        if (entry.second.estimatorInited) {
            entry.second.estimatorInited = false;
            anyInited = true;
        }
    }
    if (anyInited) SwpDestroy();
}

void DrainDeferredLocked(void* ctx) {
    if (!g_deferredPending.load(std::memory_order_relaxed)) return;
    if (!ctx || ctx != g_deferred.ctx) return;
    uint32_t tex = 0, fbo = 0;
    for (uint32_t i = 0; i < g_deferred.textureCount; i++) {
        if (g_deferred.textures[i]) {
            glDeleteTextures(1, &g_deferred.textures[i]);
            ++tex;
        }
    }
    for (uint32_t i = 0; i < g_deferred.fboCount; i++) {
        if (g_deferred.fbos[i]) {
            glDeleteFramebuffers(1, &g_deferred.fbos[i]);
            ++fbo;
        }
    }
    g_deferred = DeferredReclaim{};
    g_deferredPending.store(false, std::memory_order_relaxed);
    LOG("reclaimed %u deferred textures, %u framebuffers", tex, fbo);
}

// Delete every ASW-owned GL name for one session. Returns false when names are
// still held because their creating context is not current (the caller retries on
// a later tick); returns true when the session owns nothing afterwards.
bool ReleaseSynthResourcesLocked(DispatchState& state, void* ctx) {
    if (state.liveTextures == 0 && state.liveFbos == 0) return true;
    if (!ContextIsOwner(state, ctx)) {
        return false;
    }
    uint32_t tex = 0, fbo = 0;
    if (state.mvTexL) {
        glDeleteTextures(1, &state.mvTexL);
        state.mvTexL = 0;
        ++tex;
    }
    if (state.mvTexR) {
        glDeleteTextures(1, &state.mvTexR);
        state.mvTexR = 0;
        ++tex;
    }
    for (int s = 0; s < kMaxSlots; s++) {
        for (int e = 0; e < 2; e++) {
            if (state.staging[s][e]) {
                glDeleteTextures(1, &state.staging[s][e]);
                state.staging[s][e] = 0;
                ++tex;
            }
        }
    }
    if (state.copyReadFbo) {
        glDeleteFramebuffers(1, &state.copyReadFbo);
        state.copyReadFbo = 0;
        ++fbo;
    }
    if (state.copyDrawFbo) {
        glDeleteFramebuffers(1, &state.copyDrawFbo);
        state.copyDrawFbo = 0;
        ++fbo;
    }
    state.stageW = state.stageH = 0;
    state.stageFormat = 0;
    state.mvAllocTried = false;
    state.mvValid = false;
    NoteReleasedLocked(state, tex, fbo);
    LOG("spacewarp resources released: %u textures, %u framebuffers (live %u/%u)", tex, fbo,
        state.liveTextures, state.liveFbos);
    return true;
}

// Lazily create the two copy FBOs, counted like every other owned name.
void EnsureCopyFbosLocked(DispatchState& state) {
    void* ctx = eglGetCurrentContext();
    if (!ctx || ctx == EGL_NO_CONTEXT) return;
    if (state.creationCtx && state.creationCtx != ctx) return;  // names live elsewhere
    if (!state.creationCtx) state.creationCtx = ctx;
    if (!state.copyReadFbo) {
        glGenFramebuffers(1, &state.copyReadFbo);
        if (state.copyReadFbo) NoteOwnedLocked(state, 0, 1);
    }
    if (!state.copyDrawFbo) {
        glGenFramebuffers(1, &state.copyDrawFbo);
        if (state.copyDrawFbo) NoteOwnedLocked(state, 0, 1);
    }
}

// Release the enabled-only set for a session that was disabled mid-session (for
// example an unsupported viewCount): without this the early return in EndFrame
// would retain the whole set until xrDestroySession while the carrier is still on.
void ReleaseDisabledSessionLocked(DispatchState& state) {
    void* ctx = eglGetCurrentContext();
    if (!ReleaseSynthResourcesLocked(state, ctx)) {
        RateLimitedLog(state, "disabled session: owned GL names await their creating context");
    }
    if (state.estimatorInited && ContextIsOwner(state, ctx)) DestroyEstimatorForAllSessionsLocked();
}

// Allocate the MV-map textures in the client's CURRENT context (RGBA16F per the
// contract's mv map format; immutable storage via TexStorage2D for the FBO path).
// Never creates or binds a context; fails (-> tick stand-down) without one.
bool AllocateMvTextures(DispatchState& state) {
    if (state.mvTexL && state.mvTexR) return true;
    if (state.mvAllocTried) return false;
    state.mvAllocTried = true;
    void* ctx = eglGetCurrentContext();
    if (!ctx || ctx == EGL_NO_CONTEXT) return false;
    // The creating context is immutable: names created here belong to it, and a
    // different current context means this session is looking at a dead namespace.
    if (state.creationCtx && state.creationCtx != ctx) return false;
    if (!state.creationCtx) state.creationCtx = ctx;
    if (state.mvW == 0 || state.mvH == 0) return false;
    glGenTextures(1, &state.mvTexL);
    glGenTextures(1, &state.mvTexR);
    if (!state.mvTexL || !state.mvTexR) {
        // A half-generated pair used to leave one name owned while mvAllocTried
        // stayed set, so it was never retried and never freed.
        if (state.mvTexL) {
            glDeleteTextures(1, &state.mvTexL);
            state.mvTexL = 0;
        }
        if (state.mvTexR) {
            glDeleteTextures(1, &state.mvTexR);
            state.mvTexR = 0;
        }
        return false;
    }
    for (uint32_t tex : {state.mvTexL, state.mvTexR}) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, state.mvW, state.mvH);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    NoteOwnedLocked(state, 2, 0);
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

void RecordSlotTickLocked(DispatchState& state, SlotState& slot, uint32_t texL, uint32_t texR,
                          uint64_t epoch, uint64_t predDispTime) {
    FrameRecord record;
    record.texL = texL;
    record.texR = texR;
    record.predDispTime = predDispTime;
    record.epoch = epoch;
    std::memcpy(record.matL, state.locMatL, sizeof(record.matL));
    std::memcpy(record.matR, state.locMatR, sizeof(record.matR));
    std::memcpy(record.fovL, state.locFovL, sizeof(record.fovL));
    std::memcpy(record.fovR, state.locFovR, sizeof(record.fovR));
    slot.history.push_back(record);
    if (slot.history.size() > kRealHistoryCap) {
        slot.history.erase(slot.history.begin());
    }
}

// Query the pending image's internal format. The client's format is not fixed
// (the OLED calibration patch can select 10-bit or FP16 video swapchains), and
// an ES colour blit requires identical read/draw formats, so staging must use
// whatever the destination actually carries.
bool DestinationFormat(uint32_t tex, GLint* outFormat) {
    if (!tex || !outFormat) return false;
    GLint prevActive = GL_TEXTURE0;
    GLint prevBinding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBinding);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    const GLenum err = glGetError();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevBinding));
    glActiveTexture(static_cast<GLenum>(prevActive));
    if (err != GL_NO_ERROR || format == 0) return false;
    *outFormat = format;
    return true;
}

// Owned staging pair in the DESTINATION's internal format, so the commit blit is
// format-identical (ES does not convert on blit). The caller's active unit and
// its binding are restored: this path runs outside the estimator's own guard.
bool EnsureStagingLocked(DispatchState& state, int w, int h, GLint format) {
    if (state.stageW == w && state.stageH == h && state.stageFormat == format &&
        state.staging[0][0] && state.staging[1][1]) {
        return true;
    }
    void* ctx = eglGetCurrentContext();
    if (!ctx || ctx == EGL_NO_CONTEXT) return false;
    if (state.creationCtx && state.creationCtx != ctx) return false;  // names live elsewhere
    if (!state.creationCtx) state.creationCtx = ctx;  // owner = allocation site
    GLint prevActive = GL_TEXTURE0;
    GLint prevBinding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevBinding);
    uint32_t deleted = 0;
    for (int s = 0; s < kMaxSlots; s++) {
        for (int e = 0; e < 2; e++) {
            if (state.staging[s][e]) {
                glDeleteTextures(1, &state.staging[s][e]);
                state.staging[s][e] = 0;
                ++deleted;
            }
        }
    }
    NoteReleasedLocked(state, deleted, 0);
    const int wanted = kMaxSlots * 2;
    int created = 0;
    bool failed = false;
    for (int s = 0; s < kMaxSlots && !failed; s++) {
        for (int e = 0; e < 2; e++) {
            glGenTextures(1, &state.staging[s][e]);
            if (!state.staging[s][e]) {
                failed = true;
                break;
            }
            glBindTexture(GL_TEXTURE_2D, state.staging[s][e]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexStorage2D(GL_TEXTURE_2D, 1, static_cast<GLenum>(format), w, h);
            ++created;
        }
    }
    const bool ok = !failed && created == wanted && glGetError() == GL_NO_ERROR;
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevBinding));
    glActiveTexture(static_cast<GLenum>(prevActive));
    if (!ok) {
        // Roll back this call's partial set: a failed realloc must not leave an
        // uncounted half-quad behind while the caller falls back to stock.
        for (int s = 0; s < kMaxSlots; s++) {
            for (int e = 0; e < 2; e++) {
                if (state.staging[s][e]) {
                    glDeleteTextures(1, &state.staging[s][e]);
                    state.staging[s][e] = 0;
                }
            }
        }
        state.stageW = state.stageH = 0;
        state.stageFormat = 0;
        return false;
    }
    NoteOwnedLocked(state, static_cast<uint32_t>(created), 0);
    state.stageW = w;
    state.stageH = h;
    state.stageFormat = format;
    LOG("staging pair allocated %dx%d internal-format 0x%x (slots %d)", w, h, (unsigned)format,
        state.slotCount);
    return true;
}

// Attach one (src, dst) pair and report whether both framebuffers are complete.
// Used to validate EVERY pair before the first write, so a rejected format or
// attachment cannot leave a half-committed composite.
bool ValidateCopyPair(uint32_t src, uint32_t dst, DispatchState& state) {
    if (!src || !dst) return false;
    EnsureCopyFbosLocked(state);
    if (!state.copyReadFbo || !state.copyDrawFbo) return false;
    GLint prevRead = 0, prevDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, state.copyReadFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.copyDrawFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
    const bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
                    glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDraw));
    return ok && glGetError() == GL_NO_ERROR;
}

// Blit one owned staging image into the pending swapchain image. Scissor is
// disabled for the copy and restored; the caller's read/draw bindings survive.
bool CopyStagingToSwapchain(uint32_t src, uint32_t dst, int w, int h, DispatchState& state) {
    if (!src || !dst) return false;
    EnsureCopyFbosLocked(state);
    if (!state.copyReadFbo || !state.copyDrawFbo) return false;
    GLint prevRead = 0, prevDraw = 0;
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, state.copyReadFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.copyDrawFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
              glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (glGetError() != GL_NO_ERROR) ok = false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDraw));
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return ok;
}

// Warp every selected slot into staging, then commit all of them. No swapchain
// image is touched until every eye of every slot rendered successfully.
bool SynthesizeSlotsLocked(DispatchState& state, float scale) {
    if (state.slotCount <= 0) return false;
    GLint format = 0;
    if (!DestinationFormat(state.slots[0].texL, &format)) {
        RateLimitedLog(state, "cannot resolve the projection image format");
        return false;
    }
    if (!EnsureStagingLocked(state, static_cast<int>(state.inputW),
                             static_cast<int>(state.inputH), format)) {
        RateLimitedLog(state, "staging allocation failed");
        return false;
    }
    for (int s = 0; s < state.slotCount; s++) {
        SlotState& slot = state.slots[s];
        if (slot.history.empty()) return false;
        const FrameRecord& src = slot.history.back();
        if (!src.texL || !src.texR) return false;
        if (src.texL == slot.texL || src.texR == slot.texR) {
            // The client re-presented the same image: sampling and rendering the
            // same texture is undefined. Stand this tick down to stock.
            RateLimitedLog(state, "warp source equals destination; passing stock");
            return false;
        }
        if (!SwpExtrapolateStereo(state.mvTexL, state.mvTexR, src.texL, src.texR,
                                  state.staging[s][0], state.staging[s][1], scale)) {
            RateLimitedLog(state, "extrapolation failed");
            return false;
        }
    }
    // Validate every pair BEFORE the first write; only then commit.
    for (int s = 0; s < state.slotCount; s++) {
        if (!ValidateCopyPair(state.staging[s][0], state.slots[s].texL, state) ||
            !ValidateCopyPair(state.staging[s][1], state.slots[s].texR, state)) {
            RateLimitedLog(state, "staging commit rejected before write");
            return false;
        }
    }
    for (int s = 0; s < state.slotCount; s++) {
        SlotState& slot = state.slots[s];
        if (!CopyStagingToSwapchain(state.staging[s][0], slot.texL,
                                    static_cast<int>(state.inputW), static_cast<int>(state.inputH),
                                    state) ||
            !CopyStagingToSwapchain(state.staging[s][1], slot.texR,
                                    static_cast<int>(state.inputW), static_cast<int>(state.inputH),
                                    state)) {
            // Pre-validated, so this is a driver-level failure mid-commit: log it
            // as a mixed-composite risk, never as a clean stand-down.
            RateLimitedLog(state, "staging commit failed after validation (composite may be mixed)");
            return false;
        }
    }
    return true;
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
    // MV textures, the staging quad and the copy FBOs live in the client's
    // context. Delete them only while that exact context is current; otherwise
    // queue the names for reclaim on a later tick with the matching context. The
    // previous code either skipped them silently or zeroed ids without deleting.
    void* ctx = eglGetCurrentContext();
    if (!ReleaseSynthResourcesLocked(state, ctx)) {
        const uint32_t stillTextures = state.liveTextures;
        const uint32_t stillFbos = state.liveFbos;
        if (DeferReclaimLocked(state)) {
            LOG("session destroy: %u textures, %u framebuffers queued (context not current)",
                stillTextures, stillFbos);
        } else {
            // Neither deletable now nor queueable: count a reported leak and drop the
            // ownership record with the session, so the dormant gate cannot stay
            // pinned by a counter nothing can clear.
            g_leakedTextures.fetch_add(stillTextures, std::memory_order_relaxed);
            g_leakedFbos.fetch_add(stillFbos, std::memory_order_relaxed);
            NoteReleasedLocked(state, state.liveTextures, state.liveFbos);
            LOG("session destroy: %u textures, %u framebuffers LEAKED (no creating context); "
                "total leaked %u/%u",
                stillTextures, stillFbos, g_leakedTextures.load(), g_leakedFbos.load());
        }
    }
    if (state.estimatorInited) {
        if (ContextIsOwner(state, ctx)) {
            DestroyEstimatorForAllSessionsLocked();
        } else {
            // The estimator singleton cannot delete names from a foreign context;
            // the next SwpInit (which starts with SwpDestroy) completes it.
            LOG("session destroy: estimator teardown deferred (its context is not current)");
        }
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
    // The pose only feeds synthesis: with the carrier off (or a session the
    // session-init decision already rejected) forward and stop — no lock, no
    // matrix math. The pose for the first synthesis tick is captured on that
    // tick's own LocateViews, which runs after the carrier has flipped.
    if (SwpVtsMode() == 0) return result;
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    DispatchState* state = StateForLocked(session);
    if (!state || !state->enabledForSession) return result;
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
    // Dormant fast path: with the host carrier off (or never published) the layer
    // owns nothing but the carrier cadence — one lock-free mode load, one counter
    // tick, then the client's own endframe call. No clock read, no lock, no
    // session lookup, no tick classification, no history. Everything synthesis
    // needs is rebuilt after the mode flips, and the flip itself clears the state
    // that went stale while dormant (see the transition block below).
    if (SwpVtsMode() == 0) {
        // Settled-Off release (B1). The gate is one relaxed load, so a dormant
        // tick in a run that never enabled ASW still performs no lock and no GL:
        // the release path is entered only while some session still owns names.
        if (g_synthOwners.load(std::memory_order_relaxed) != 0 ||
            g_deferredPending.load(std::memory_order_relaxed)) {
            std::lock_guard<std::recursive_mutex> offLock(g_mutex);
            void* offCtx = eglGetCurrentContext();
            DrainDeferredLocked(offCtx);
            auto offIt = g_sessions.find(session);
            if (offIt != g_sessions.end()) {
                DispatchState& offState = offIt->second;
                if (!ReleaseSynthResourcesLocked(offState, offCtx)) {
                    RateLimitedLog(offState, "ASW off: owned GL names await their creating context");
                }
                if (ContextIsOwner(offState, offCtx)) {
                    DestroyEstimatorForAllSessionsLocked();
                } else if (offState.estimatorInited) {
                    RateLimitedLog(offState, "ASW off: estimator teardown awaits its context");
                }
            }
        }
        SwpVtsNoteEndFrame();
        return g_nextEndFrame(session, frameEndInfo);
    }
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    if (g_deferredPending.load(std::memory_order_relaxed)) DrainDeferredLocked(eglGetCurrentContext());

    DispatchState* state = StateForLocked(session);
    if (!state || !state->enabledForSession) {
        // A session disabled mid-session (unsupported composition, failed enable)
        // must not keep its enabled-only set alive just because the carrier is on.
        if (state && (state->liveTextures || state->liveFbos || state->estimatorInited)) {
            ReleaseDisabledSessionLocked(*state);
        }
        return g_nextEndFrame(session, frameEndInfo);
    }

    // The carrier cadence must run before every other early return below: a
    // session whose arrival learning is unavailable still has to observe the
    // host toggle (otherwise it can never leave the dormant state).
    SwpVtsNoteEndFrame();

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
        // This session has no arrival source, so it can never classify a tick.
        // Say it once and pass stock for the session's life instead of writing a
        // line every 30 ticks (the previous behaviour, which the dormant fast
        // path no longer masks).
        if (!state->arrivalUnavailableLogged) {
            state->arrivalUnavailableLogged = true;
            LOG("%s; spacewarp stays stock for this session",
                learningDisabled ? "arrival learning disabled" : "arrival snapshot unavailable");
        }
        return g_nextEndFrame(session, frameEndInfo);
    }

    const TickKind kind = SwpClassifyTick(epochNow, state->tickEpoch, state->haveTickEpoch, false);
    state->tickEpoch = epochNow;
    state->haveTickEpoch = true;

    // -----------------------------------------------------------------------
    // Projection slots. The client submits one projection per video component;
    // audited submission order (RE 2026-10-20):
    //   2.0.20/5001712 : [base, foveated]                  -> both are video
    //   2.0.23/5002363 : [underside, base, foveated]       -> last two are video
    // The underside layer is never rendered (static/undefined content), so
    // warping it produced a composite where the real video stayed stock. Any
    // other layer count is not an audited composition: pass stock.
    // -----------------------------------------------------------------------
    const XrCompositionLayerProjection* projs[kMaxProjectionLayers] = {};
    uint32_t projCount = 0;
    if (frameEndInfo) {
        for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
            const XrCompositionLayerBaseHeader* layer = frameEndInfo->layers[i];
            if (!layer || layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
            if (projCount < kMaxProjectionLayers) {
                projs[projCount] =
                    reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            }
            projCount++;
        }
    }
    int firstSlot = -1;
    if (projCount == 2) firstSlot = 0;
    else if (projCount == 3) firstSlot = 1;
    if (firstSlot < 0 || projCount > kMaxProjectionLayers) {
        static std::atomic<bool> loggedShape{false};
        bool expected = false;
        if (loggedShape.compare_exchange_strong(expected, true)) {
            LOG("projection count %u is not an audited composition (2 or 3); "
                "spacewarp passes stock", projCount);
        }
        return g_nextEndFrame(session, frameEndInfo);
    }
    const int slotCount = 2;
    for (int s = 0; s < slotCount; s++) {
        const XrCompositionLayerProjection* p = projs[firstSlot + s];
        if (!p || p->viewCount != 2) {
            state->enabledForSession = false;
            static std::atomic<bool> loggedViewCount{false};
            bool expected = false;
            if (loggedViewCount.compare_exchange_strong(expected, true)) {
                LOG("projection viewCount=%u unsupported; spacewarp inactive for the session",
                    p ? p->viewCount : 0u);
            }
            return g_nextEndFrame(session, frameEndInfo);
        }
        const XrExtent2Di& e0 = p->views[0].subImage.imageRect.extent;
        const XrExtent2Di& e1 = p->views[1].subImage.imageRect.extent;
        if (e0.width != e1.width || e0.height != e1.height || e0.width <= 0 || e0.height <= 0) {
            RateLimitedLog(*state, "projection eyes disagree on extent");
            return g_nextEndFrame(session, frameEndInfo);
        }
    }

    // (Re)arm the estimator extents from the reference (base) slot; a mid-session
    // size change re-inits the estimator and drops the maps (fail-safe).
    const int32_t refW = projs[firstSlot]->views[0].subImage.imageRect.extent.width;
    const int32_t refH = projs[firstSlot]->views[0].subImage.imageRect.extent.height;
    if (!state->inputW || !state->inputH || state->inputW != static_cast<uint32_t>(refW) ||
        state->inputH != static_cast<uint32_t>(refH)) {
        // Retire the old owned set BEFORE the new extent is armed. The previous
        // code zeroed the mv ids without deleting them, so every mid-session size
        // change leaked two textures; staging and the copy FBOs were rebuilt
        // underneath the stale ones.
        void* resizeCtx = eglGetCurrentContext();
        if (!ReleaseSynthResourcesLocked(*state, resizeCtx)) {
            RateLimitedLog(*state, "extent change needs the creating EGL context; holding stock");
            return g_nextEndFrame(session, frameEndInfo);
        }
        if (state->estimatorInited) {
            if (!ContextIsOwner(*state, resizeCtx)) {
                RateLimitedLog(*state, "extent change: estimator teardown awaits its context");
                return g_nextEndFrame(session, frameEndInfo);
            }
            DestroyEstimatorForAllSessionsLocked();
        }
        state->inputW = static_cast<uint32_t>(refW);
        state->inputH = static_cast<uint32_t>(refH);
        state->mvValid = false;
        state->mvAllocTried = false;
        for (int s = 0; s < kMaxSlots; s++) state->slots[s].history.clear();
    }
    // One uv field serves every slot: their view geometry and extent are
    // identical (verified for both bases). A slot that disagrees passes stock.
    for (int s = 1; s < slotCount; s++) {
        const XrExtent2Di& e = projs[firstSlot + s]->views[0].subImage.imageRect.extent;
        if (static_cast<uint32_t>(e.width) != state->inputW ||
            static_cast<uint32_t>(e.height) != state->inputH) {
            RateLimitedLog(*state, "projection extent differs from the estimator input; passing stock");
            return g_nextEndFrame(session, frameEndInfo);
        }
    }

    // Resolve this tick's pending images per slot and keep the slot identity
    // stable (a changed swapchain pair drops that slot's history).
    state->slotCount = slotCount;
    for (int s = 0; s < slotCount; s++) {
        const XrCompositionLayerProjection* p = projs[firstSlot + s];
        SlotState& slot = state->slots[s];
        uint32_t texL = 0, texR = 0;
        if (!ResolvePendingTextureLocked(*state, p->views[0], &texL) ||
            !ResolvePendingTextureLocked(*state, p->views[1], &texR)) {
            RateLimitedLog(*state, "pending swapchain textures unknown");
            return g_nextEndFrame(session, frameEndInfo);
        }
        if (slot.swapL != p->views[0].subImage.swapchain ||
            slot.swapR != p->views[1].subImage.swapchain) {
            slot.swapL = p->views[0].subImage.swapchain;
            slot.swapR = p->views[1].subImage.swapchain;
            slot.history.clear();
        }
        slot.texL = texL;
        slot.texR = texR;
    }

    // -----------------------------------------------------------------------
    // Bridge gate: synthesis runs only while the carrier reads enabled. Dormant
    // ticks never reach this point (the fast path above returned already), so
    // what follows is the enable transition and the synthesis path itself.
    // -----------------------------------------------------------------------
    // Dormant ticks never reach here (the fast path above returns first), so this
    // is the enable transition: the first tick after the carrier reads nonzero, or
    // a 1 <-> 2 mode flip. Everything collected while dormant is stale by
    // definition — the clock anchors stopped advancing and the frame history
    // predates the gap — so drop it and restart from the next real tick.
    const uint32_t mode = SwpVtsMode();
    if (mode == 0) return g_nextEndFrame(session, frameEndInfo);  // raced with a flip off
    const uint64_t generation = SwpVtsGeneration();
    if (!state->synthesisWasAllowed || generation != state->modeGeneration ||
        mode != state->lastMode) {
        state->mvValid = false;
        state->consecutiveMissed = 0;
        state->havePresented = false;
        state->lastPresentedWasReal = false;
        state->pairCount = 0;  // pre-gap anchors would map fresh arrivals wrong
        for (int s = 0; s < kMaxSlots; s++) state->slots[s].history.clear();
    }
    state->synthesisWasAllowed = true;
    state->lastMode = mode;
    state->modeGeneration = generation;

    // CRITICAL: draw only with the client's current EGL context, and only when it
    // matches the context the frame assets were created with.
    const EGLContext currentCtx = eglGetCurrentContext();
    if (currentCtx == EGL_NO_CONTEXT || (arrivalCtx && currentCtx != arrivalCtx)) {
        RateLimitedLog(*state, "current EGL context does not match the frame-asset context");
        return g_nextEndFrame(session, frameEndInfo);
    }
    if (!state->creationCtx) state->creationCtx = arrivalCtx;
    // The pose is captured only while the carrier is on (LocateViews fast path).
    // A tick that reaches synthesis without one must pass stock: warping with the
    // zero matrix would silently draw garbage into the projection image.
    if (!state->haveLocate) {
        RateLimitedLog(*state, "no locate pose for this tick; passing stock");
        return g_nextEndFrame(session, frameEndInfo);
    }
#ifdef SWP_HAVE_ESTIMATOR
    SwpBindIdentityContext(reinterpret_cast<void*>(currentCtx));
#endif

    if (!EnsureEstimatorReadyLocked(*state) || !AllocateMvTextures(*state)) {
        return g_nextEndFrame(session, frameEndInfo);
    }

    if (kind == TickKind::Real) {
        // Motion field from the reference slot: prev (last arrival) -> curr.
        SlotState& base = state->slots[0];
        bool mvOk = false;
        if (!base.history.empty()) {
            const FrameRecord& prev = base.history.back();
            if (prev.texL && prev.texL != base.texL && prev.texR != base.texR) {
                mvOk = SwpGenerateVectorsStereo(prev.texL, base.texL, prev.texR, base.texR,
                                                prev.matL, prev.matR, prev.fovL, prev.fovR,
                                                state->mvTexL, state->mvTexR);
            }
        }
        state->mvValid = mvOk;
        g_lastEstimatorStatus.store(SwpLastStatus());
        if (!mvOk && !base.history.empty()) RateLimitedLog(*state, "vector generation failed");

        // Half-rate mode: every second presented tick is synthesized from the
        // previously presented frame instead of showing the fresh decode.
        bool synthesized = false;
        if (mode == kModeHalfRate && state->havePresented && state->lastPresentedWasReal) {
            uint64_t arrivalsArr[kArrivalHistoryCap] = {0};
            uint64_t arrivalMonoArr[kArrivalHistoryCap] = {0};
            const int historyCount =
                SwpEglArrivalHistory(arrivalsArr, arrivalMonoArr, kArrivalHistoryCap);
            uint64_t sourceXr = 0, beforeSourceXr = 0;
            float forcedScale = 0.0f;
            if (historyCount >= 3 &&
                ArrivalXrTime(*state, arrivalMonoArr[1], &sourceXr) &&
                ArrivalXrTime(*state, arrivalMonoArr[2], &beforeSourceXr) &&
                SwpScaleFactorForMissedTick(predDispTime, beforeSourceXr, sourceXr, &forcedScale)) {
                synthesized = SynthesizeSlotsLocked(*state, forcedScale);
            } else {
                RateLimitedLog(*state, "half-rate scale unavailable; presenting the decoded frame");
            }
        }
        for (int s = 0; s < slotCount; s++) {
            RecordSlotTickLocked(*state, state->slots[s], state->slots[s].texL,
                                 state->slots[s].texR, epochNow, predDispTime);
        }
        state->consecutiveMissed = 0;
        state->havePresented = true;
        state->lastPresentedWasReal = !synthesized;
        return g_nextEndFrame(session, frameEndInfo);
    }
    if (kind == TickKind::Stalled) return g_nextEndFrame(session, frameEndInfo);

    // Missed tick: extrapolate each slot's newest arrival into its pending image.
    state->consecutiveMissed++;
    if (state->consecutiveMissed > kMaxConsecutiveMissed) {
        RateLimitedLog(*state, "consecutive missed-tick cap; passing stock");
        return g_nextEndFrame(session, frameEndInfo);
    }
    if (!state->mvValid) {
        RateLimitedLog(*state, "no valid maps for extrapolation");
        return g_nextEndFrame(session, frameEndInfo);
    }
    for (int s = 0; s < slotCount; s++) {
        if (state->slots[s].history.empty()) {
            RateLimitedLog(*state, "no history for a selected projection");
            return g_nextEndFrame(session, frameEndInfo);
        }
    }

    const uint64_t predForScale = state->pairXr[0];
    uint64_t arrivals[kArrivalHistoryCap] = {0};
    uint64_t arrivalMono[kArrivalHistoryCap] = {0};
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
    if (SynthesizeSlotsLocked(*state, scale)) {
        g_lastEstimatorStatus.store(SwpLastStatus());
        state->havePresented = true;
        state->lastPresentedWasReal = false;
    } else {
        g_lastEstimatorStatus.store(SwpLastStatus());
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
#if defined(_MSC_VER)
// Host-test build: default visibility is an ELF concept; nothing dlsyms here.
#define EXPORT
#elif !defined(EXPORT)
#define EXPORT __attribute__((visibility("default")))
#endif
EXPORT void SwpSetEnabled(bool enabled) {
    g_userEnabled.store(enabled, std::memory_order_relaxed);
}

EXPORT bool SwpGetEnabled() { return g_userEnabled.load(std::memory_order_relaxed); }

}  // extern "C"
// spacewarp_est.h — on-device spacewarp estimator core for gxr-spacewarp-layer.
//
// Pure estimator (C++17 + GLES 3.2 fragment passes), independent of the
// OpenXR/PLT/EGL plumbing that B1b wires around it. Mechanism re-derived from
// the Virtual Desktop libMotionEngine REA (see RE-SHARED-IMPL.md and
// 04-rea-motionengine/REPORT.md §3-5 + EVIDENCE-shaders-deobfuscated.txt):
//
//   1. SwpGenerateVectorsStereo  — motion estimation from two consecutive
//      decoded COLOR frames per eye: coarse global-motion search (GME) seeds a
//      hierarchical block-match SAD (four uniform block-size quadrant SADs per
//      candidate, the VD L1 structure), a 9-element sorting-network median
//      smooths the raw field, and a fused H1 pass refines it around the median
//      with a 3x3 window plus parabolic subpel, writing an RGBA16F motion
//      vector map per eye into caller-owned textures.
//   2. SwpExtrapolateStereo      — single-texture warp (VD's sr.mode/radius/
//      clampMagnitude semantics): the newest decoded frame is warped onto the
//      projection swapchain image the client submits on a missed-tick frame by
//      the time-shifted motion field (extrapolation == warp with time-shifted
//      vectors, REPORT.md §5).
//
// Guided filtering, temporal statistics, scene-change detection and the
// TFLite "Smart" stage are documented gaps (RE-SHARED-IMPL.md) and are NOT
// implemented here.
//
// ---------------------------------------------------------------------------
// UNITS (pinned; both estimator sides obey this):
//   MV map texel .rg = (uv displacement) = (NDC displacement / 2.0), i.e. the
//   contract's "v.xy/scale" with v = the NDC delta and scale = 2.0. .ba = 0.
//   The extrapolation pass therefore multiplies the stored .rg by 2 in spirit
//   (it works directly in uv space) and folds init fovScale on top.
//   Internally the matcher works in INPUT-TEXEL offsets and converts at the
//   final write (uv = texel / inputSize). VD's finalize divides by a
//   configurable output scale; ours is self-consistent by construction.
//
// STATUS BITS (SwpLastStatus):
//   1  init ok (last successful SwpInit)
//   2  mv ok (last SwpGenerateVectorsStereo)
//   4  extrapolation ok (last SwpExtrapolateStereo)
//   8  QCOM block-match extension missing (plain-SAD fallback armed; sticky)
//   16 stand-down (any construction/GL error; sticky until a fresh SwpInit)
//   32 estimator budget exceeded during the last mv/extrapolation call
//      (bits 2/4 are then not set: the call returned false)
// ---------------------------------------------------------------------------

#pragma once
#include <cstdint>

#include <GLES3/gl3.h>  // GL entry-point typedefs + enums only; no GL calls here.

typedef uint32_t u32;  // contract's u32

extern "C" {

// ---- shared interface contract (exact; RE-SHARED-IMPL.md) -----------------

struct SwpInitInfo {
    u32 inputWidth;    // decoded-frame texels; % 16 == 0, >= 256, <= 8192
    u32 inputHeight;   // ditto
    u32 mvWidth;       // motion-vector map texels; 0 => default input/2 clamped to >= 64, <= 2048
    u32 mvHeight;      // ditto
    float fovScale;    // folds onto stored vectors at extrapolation; 0 => 1.0
                       // (compensates a fixed stream-FOV <-> swapchain-FOV ratio;
                       // 1.0 = same-FOV path, the validated default)
    u32 flags;         // SWP_INIT_FLAG_* below
};

bool SwpInit(const SwpInitInfo* info);
void SwpDestroy();

// viewMatrix = column-major 4x4 exactly as the client's LocateViews produce
// (the currTex frame's pose; validated, NaN-guarded); fov = 4 floats
// angleLeft, angleRight, angleUp, angleDown in radians exactly as XrFov
// passes them (validated, finite). The block matcher measures the actual
// image flow prev->curr, which carries camera + object motion; the VD
// per-frame reprojection matrices cannot be reproduced from one matrix per
// call by contract design (see receipt, conservative approximation note).
// History rotation between calls is the CALLER's duty.
bool SwpGenerateVectorsStereo(u32 prevTexL, u32 currTexL, u32 prevTexR, u32 currTexR,
                          const float* viewMatrixL, const float* viewMatrixR,
                          const float* fovL, const float* fovR,
                          u32 mvTexL, u32 mvTexR);

// scaleFactor = normalized progress between the history frame indexes
// (>= 1.0; values < 1.0 are clamped to 1.0 conservatively). dst = the
// projection swapchain image the client is submitting this tick.
bool SwpExtrapolateStereo(u32 mvTexL, u32 mvTexR, u32 srcTexL, u32 srcTexR,
                      u32 dstTexL, u32 dstTexR, float scaleFactor);

u32 SwpLastStatus();  // bitmask above

extern const char* SwpLogTag;  // "GXRSwarp"

// ---- B1b-owned dispatch plumbing ------------------------------------------

// GL function table (the estimator performs every GL call through it so B1b
// can route through its own dispatch — RE-SHARED-IMPL.md, context-agnostic).
// Optional entries may be NULL: getCurrentContext (then context identity
// cannot be verified from here), nowMicros (then no budget is enforced).
// 34 entries total (the brief's "~20"); all typed from GLES3 headers.
struct SwpGlTable {
    // Shaders / programs
    PFNGLCREATESHADERPROC glCreateShader;
    PFNGLSHADERSOURCEPROC glShaderSource;
    PFNGLCOMPILESHADERPROC glCompileShader;
    PFNGLGETSHADERIVPROC glGetShaderiv;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
    PFNGLDELETESHADERPROC glDeleteShader;
    PFNGLCREATEPROGRAMPROC glCreateProgram;
    PFNGLATTACHSHADERPROC glAttachShader;
    PFNGLLINKPROGRAMPROC glLinkProgram;
    PFNGLGETPROGRAMIVPROC glGetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
    PFNGLDELETEPROGRAMPROC glDeleteProgram;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
    PFNGLUNIFORM1IPROC glUniform1i;
    PFNGLUNIFORM2IPROC glUniform2i;
    PFNGLUNIFORM1FPROC glUniform1f;
    PFNGLUSEPROGRAMPROC glUseProgram;
    // Draw / state
    PFNGLDRAWARRAYSPROC glDrawArrays;
    PFNGLVIEWPORTPROC glViewport;
    PFNGLFLUSHPROC glFlush;
    PFNGLGETERRORPROC glGetError;
    // Framebuffers
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
    // Textures
    PFNGLGENTEXTURESPROC glGenTextures;
    PFNGLDELETETEXTURESPROC glDeleteTextures;
    PFNGLBINDTEXTUREPROC glBindTexture;
    PFNGLACTIVETEXTUREPROC glActiveTexture;
    PFNGLTEXSTORAGE2DPROC glTexStorage2D;
    PFNGLTEXPARAMETERIPROC glTexParameteri;
    // Query
    PFNGLGETSTRINGPROC glGetString;
    PFNGLGETINTEGERVPROC glGetIntegerv;
    // Hooks
    void* (*getCurrentContext)(void);  // eglGetCurrentContext route (optional; may be NULL)
    unsigned long long (*nowMicros)(void);  // monotonic microsecond clock (optional; NULL = no budget)
};

// Call in the GL thread BEFORE SwpInit, with the target context current.
// Ownership: the table is copied (a stack local is fine). Replacing the table
// while initialized forces stand-down until a fresh SwpInit.
bool SwpSetGlTable(const SwpGlTable* table);

// Identity anchor: B1b calls this with eglGetCurrentContext() (or its own
// dispatch's context id) whenever a context becomes current and the estimator
// must run on it. Every entry point then bails with status 16 if the current
// context differs or none is current (never assume a context — RE-SHARED-IMPL.md).
void SwpBindIdentityContext(void* currentCtx);

// Post-init getter for the resolved mv-map size (defaults applied when
// SwpInitInfo.mvWidth/Height were 0). False when not initialized.
bool SwpResolvedMvSize(u32* outW, u32* outH);

// ---- init flags ------------------------------------------------------------
#define SWP_INIT_FLAG_FORCE_SAD_FALLBACK 0x1u  // skip QCOM direct path even when present
#define SWP_INIT_FLAG_VERBOSE_LOG        0x2u  // extra (still rate-limited) log lines
#define SWP_INIT_FLAG_BUDGET_OFF         0x4u  // disable the per-call time budget

}  // extern "C"

// ---------------------------------------------------------------------------
// INTERNAL — B1a only (spacewarp_est.cpp <-> swp_est_worker.cpp).
// Not part of the shared contract; B1b must not call these.
// ---------------------------------------------------------------------------
namespace gxr {
namespace swp_internal {

static const int kSwpBlock = 16;              // direct-path block size (QCOM path)
static const int kSwpFallbackBlock = 8;       // fallback block size (cheaper manual SAD)
static const int kSwpMaxInputDim = 8192;      // "impossibly large" guard (VD: REPORT.md §1)
static const int kSwpMinInputDim = 256;       // keeps the origin clamp margin sane
static const int kSwpMaxMvDim = 2048;
static const int kSwpMinMvDim = 8;
static const uint32_t kSwpDefaultBudgetMicros = 12000;  // per-call hard budget

struct SwpEstConfig {
    bool forceFallbackSad;
    bool verboseLog;
    uint32_t budgetMicros;
    float fovScale;

    int inputW, inputH;    // input texels
    int mvW, mvH;          // mv-map texels
    int blockGridW, blockGridH;  // input / block
    int block;             // 16 direct, 8 fallback

    // search geometry (input texels; conservative defaults, receipt-documented)
    int globalMaxX, globalMaxY;   // absolute search half-extent
    int gmeStepX, gmeStepY;       // GME candidate stride
    int gmeOriginStrideX, gmeOriginStrideY;  // GME block-lattice stride
    int searchSpanX, searchSpanY; // L1 window around the GME seed
    int medianStride;             // median neighbor stride in mv texels (VD filterRange=3)

    float sadScale;    // texel normalization of raw SAD (per path)
    float noMotion;    // mean per-texel summed-channel |delta| below which a block is static
    float noMotionUv;  // uv floor: below this the stored vector is zeroed
    float clampMagUv;  // extrapolation clamp magnitude, uv units
    float srRadiusUv;  // stretch radius, uv units
    int srMode;        // 0 = stretch ramp then clamp (default), 1 = hard clamp
};

struct SwpEstTickOut {
    bool ok;               // the call's work completed
    bool budgetExceeded;   // budget bit 32 companion
};

// GL-side engine: compile + allocate (fails => stand-down, no partial state).
// qcomArmed reports whether the direct QCOM SAD path was armed.
bool EstiGlSetup(const SwpEstConfig& cfg, bool* qcomArmed);
void EstiSetTable(const SwpGlTable* table);  // pass a table with a stable address
void EstiGlShutdown();
void EstiDropHandles();  // teardown WITHOUT GL calls (wrong context at destroy)
bool EstiMvMapOneEye(const SwpEstConfig& cfg, u32 prevTex, u32 currTex, u32 mvTex,
                     SwpEstTickOut* out);
bool EstiExtrapolateOneEye(const SwpEstConfig& cfg, float scaleFactor, u32 mvTex,
                           u32 srcTex, u32 dstTex, SwpEstTickOut* out);

}  // namespace swp_internal
}  // namespace gxr
// Host tests for the gxr-spacewarp dispatch + arrival learning (B1b tests half).
//
// Compiles the PRODUCTION TUs (swp_dispatch.cpp + swp_egl.cpp — see the CMake
// target) with:
//   - estimator fakes implementing the shared interface contract (capturing args),
//   - fake next-chain XR functions (passthrough recorders),
//   - stubbed Android log / EGL / GLES3 (bodies here, recording calls),
//   - the host-controllable monotonic clock of swp_egl (SwpEglTestSetNowNs).
// The GOT-patch mechanism has its own TU (swp_hooks_tests.cpp).
//
// The round-robin arrival assumption from the shared brief (RE-SHARED-IMPL.md
// component 4) is exercised explicitly: buffer reuse bumps the arrival epoch once
// per decoded frame (recycled handles included, consecutive duplicates do not), and
// a pool wider than the documented cap disables the learning (conservative
// stand-down) instead of extrapolating blind.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/swp_dispatch.cpp"

#include <GLES3/gl3.h>  // (stub): bodies for the GL entries the table routes

// swp_egl test hooks (host-only, defined in that TU).
extern "C" {
uint64_t SwpEglTestSetNowNs(uint64_t nowNs);
void SwpEglResetArrivals();
}  // extern "C"

// ---------------------------------------------------------------------------
// Scaffolding.
// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond, label)                                                     \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            std::printf("FAIL: %s (line %d)\n", label, __LINE__);              \
        }                                                                      \
    } while (0)

// --- Android log stub body (records lines; swp_egl logs through the same) ---
std::vector<std::string> g_logLines;
extern "C" int __android_log_print(int, const char*, const char* format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_logLines.push_back(line);
    return 1;
}

// --- GLES3 stub bodies (record calls) ---
static GLuint g_nextTextureId = 500;
extern "C" GLuint glCreateShader(GLenum) { return ++g_nextTextureId; }
extern "C" void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*) {}
extern "C" void glCompileShader(GLuint) {}
extern "C" void glGetShaderiv(GLuint, GLenum, GLint*) {}
extern "C" void glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) {}
extern "C" void glDeleteShader(GLuint) {}
extern "C" GLuint glCreateProgram() { return ++g_nextTextureId; }
extern "C" void glAttachShader(GLuint, GLuint) {}
extern "C" void glLinkProgram(GLuint) {}
extern "C" void glGetProgramiv(GLuint, GLenum, GLint*) {}
extern "C" void glGetProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*) {}
extern "C" void glDeleteProgram(GLuint) {}
extern "C" GLint glGetUniformLocation(GLuint, const GLchar*) { return -1; }
extern "C" void glUniform1i(GLint, GLint) {}
extern "C" void glUniform2i(GLint, GLint, GLint) {}
extern "C" void glUniform1f(GLint, GLfloat) {}
extern "C" void glUseProgram(GLuint) {}
extern "C" void glDrawArrays(GLenum, GLint, GLsizei) {}
extern "C" void glViewport(GLint, GLint, GLsizei, GLsizei) {}
extern "C" void glFlush() {}
extern "C" GLenum glGetError() { return GL_NO_ERROR; }
extern "C" void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    for (GLsizei i = 0; i < n; i++) framebuffers[i] = ++g_nextTextureId;
}
extern "C" void glDeleteFramebuffers(GLsizei, const GLuint*) {}
extern "C" void glBindFramebuffer(GLenum, GLuint) {}
extern "C" void glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint) {}
extern "C" GLenum glCheckFramebufferStatus(GLenum) { return GL_FRAMEBUFFER_COMPLETE; }
extern "C" void glGenTextures(GLsizei n, GLuint* textures) {
    for (GLsizei i = 0; i < n; i++) textures[i] = ++g_nextTextureId;  // 501, 502 ...
}
extern "C" void glDeleteTextures(GLsizei, const GLuint*) {}
extern "C" void glBindTexture(GLenum target, GLuint texture) {
    (void)target;
    (void)texture;
}
extern "C" void glActiveTexture(GLenum) {}
extern "C" void glTexStorage2D(GLenum, GLsizei, GLenum, GLsizei, GLsizei) {}
extern "C" void glTexParameteri(GLenum, GLenum, GLint) {}
extern "C" const GLubyte* glGetString(GLenum) {
    return reinterpret_cast<const GLubyte*>("gxr-stub-gl");
}
extern "C" void glGetIntegerv(GLenum, GLint*) {}

// --- EGL stub body ---
static void* g_currentCtx = reinterpret_cast<void*>(0xC0DE);
extern "C" EGLContext eglGetCurrentContext() { return g_currentCtx; }

// --- Estimator fakes (exact shared-contract signatures; capture + controllable) ---
struct EstimatorCalls {
    bool initCalled = false;
    int generateCalls = 0;
    int extrapolateCalls = 0;
    uint32_t lastPrevTexL = 0, lastCurrTexL = 0, lastPrevTexR = 0, lastCurrTexR = 0;
    uint32_t lastMvTexL = 0, lastMvTexR = 0;
    uint32_t lastSrcTexL = 0, lastSrcTexR = 0, lastDstTexL = 0, lastDstTexR = 0;
    float lastScale = 0.f;
    bool initResult = true;
    bool generateResult = true;
    bool extrapolateResult = true;
} g_est;
uint32_t g_estStatus = 0;

extern "C" {
bool SwpInit(const SwpInitInfo*) {
    g_est.initCalled = true;
    g_estStatus = 1u;  // init ok
    return g_est.initResult;
}
void SwpDestroy() { g_estStatus = 0; }
bool SwpGenerateVectorsStereo(uint32_t prevTexL, uint32_t currTexL, uint32_t prevTexR,
                              uint32_t currTexR, const float*, const float*, const float*,
                              const float*, uint32_t mvTexL, uint32_t mvTexR) {
    g_est.generateCalls++;
    g_est.lastPrevTexL = prevTexL;
    g_est.lastCurrTexL = currTexL;
    g_est.lastPrevTexR = prevTexR;
    g_est.lastCurrTexR = currTexR;
    g_est.lastMvTexL = mvTexL;
    g_est.lastMvTexR = mvTexR;
    g_estStatus = 2u;  // mv ok
    return g_est.generateResult;
}
bool SwpExtrapolateStereo(uint32_t mvTexL, uint32_t mvTexR, uint32_t srcTexL, uint32_t srcTexR,
                          uint32_t dstTexL, uint32_t dstTexR, float scaleFactor) {
    g_est.extrapolateCalls++;
    g_est.lastMvTexL = mvTexL;
    g_est.lastMvTexR = mvTexR;
    g_est.lastSrcTexL = srcTexL;
    g_est.lastSrcTexR = srcTexR;
    g_est.lastDstTexL = dstTexL;
    g_est.lastDstTexR = dstTexR;
    g_est.lastScale = scaleFactor;
    g_estStatus = 4u;  // extrap ok
    return g_est.extrapolateResult;
}
uint32_t SwpLastStatus() { return g_estStatus; }
const char* SwpLogTag = "GXRSwarp";

// B1a's dispatch plumbing beyond the shared contract (swp_dispatch.cpp fills the
// GL routing table + identity anchor per tick): fakes here capture the calls.
int g_glTableCalls = 0;
bool g_glTableResult = true;
void* g_boundIdentityCtx = nullptr;
int g_identityBindings = 0;
u32 g_fakeMvW = 1024, g_fakeMvH = 1024;
bool SwpSetGlTable(const SwpGlTable* table) {
    g_glTableCalls++;
    if (!g_glTableResult || !table) return false;
    // The routed entries must be the layer's own bodies (non-null; the hooks too).
    const void* entries[] = {
        (const void*)table->glCreateShader, (const void*)table->glShaderSource,
        (const void*)table->glCompileShader, (const void*)table->glGetShaderiv,
        (const void*)table->glGetShaderInfoLog, (const void*)table->glDeleteShader,
        (const void*)table->glCreateProgram, (const void*)table->glAttachShader,
        (const void*)table->glLinkProgram, (const void*)table->glGetProgramiv,
        (const void*)table->glGetProgramInfoLog, (const void*)table->glDeleteProgram,
        (const void*)table->glGetUniformLocation, (const void*)table->glUniform1i,
        (const void*)table->glUniform2i, (const void*)table->glUniform1f,
        (const void*)table->glUseProgram, (const void*)table->glDrawArrays,
        (const void*)table->glViewport, (const void*)table->glFlush,
        (const void*)table->glGetError, (const void*)table->glGenFramebuffers,
        (const void*)table->glDeleteFramebuffers, (const void*)table->glBindFramebuffer,
        (const void*)table->glFramebufferTexture2D, (const void*)table->glCheckFramebufferStatus,
        (const void*)table->glGenTextures, (const void*)table->glDeleteTextures,
        (const void*)table->glBindTexture, (const void*)table->glActiveTexture,
        (const void*)table->glTexStorage2D, (const void*)table->glTexParameteri,
        (const void*)table->glGetString, (const void*)table->glGetIntegerv,
    };
    for (const void* entry : entries) {
        if (!entry) return false;
    }
    return table->getCurrentContext && table->nowMicros;
}
void SwpBindIdentityContext(void* currentCtx) {
    g_identityBindings++;
    g_boundIdentityCtx = currentCtx;
}
bool SwpResolvedMvSize(u32* outW, u32* outH) {
    if (!outW || !outH) return false;
    *outW = g_fakeMvW;
    *outH = g_fakeMvH;
    return true;
}
}  // extern "C"

// --- Fake hooks (the real GOT module has its own test TU) ---
extern "C" {
bool g_hooksArmResult = false;
bool SwpHooksArm() { return g_hooksArmResult; }
bool SwpHooksArmed() { return g_hooksArmResult; }
}  // extern "C"

// --- Bridge mock (brief 10: the host-published VTS carrier via swp_vts) ---
using BridgeFn = bool (*)(void*, uint32_t, void*, uint32_t);
extern "C" {
void SwpVtsTestSetTable(BridgeFn fn);
void SwpVtsTestSetArmed(bool armed);
void SwpVtsTestSetRoot(void* root);
void SwpVtsTestPublish(uint32_t value);
void SwpVtsTestResetAll();
}  // extern "C"
static int g_mockBridgeReads = 0;
static bool g_mockBridgePresent = false;  // stock session: the carrier is unpublished
static uint32_t g_mockBridgeValue = 0;
extern "C" bool BridgeMock(void*, uint32_t, void* out, uint32_t) {
    g_mockBridgeReads++;
    if (!g_mockBridgePresent) return false;  // never published this session
    if (out) *static_cast<uint32_t*>(out) = g_mockBridgeValue;
    return true;
}

// --- Fake next-chain XR functions (passthrough recorders) ---
struct NextCalls {
    int createSession = 0, destroySession = 0, beginFrame = 0, locateViews = 0, endFrame = 0;
    int enumerate = 0, acquire = 0;
    uint32_t lastLayerCount = 0;
    const void* lastLayersPtr = nullptr;  // untouched layer-list passthrough evidence
} g_next;

static const XrSession kFakeSession = reinterpret_cast<XrSession>(0xA11);
static uint64_t g_fakePredDispTime = 0;
static const XrSwapchain kFakeSwapchain = reinterpret_cast<XrSwapchain>(0xBEEF);
static uint32_t g_swapchainImageIds[3] = {101, 102, 103};
static uint32_t g_acquiredIndex = 0;

static XrResult XRAPI_PTR FakeCreateSession(XrInstance, const XrSessionCreateInfo*,
                                            XrSession* session) {
    g_next.createSession++;
    *session = kFakeSession;
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR FakeDestroySession(XrSession) {
    g_next.destroySession++;
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR FakeBeginFrame(XrSession, const XrFrameBeginInfo*) {
    g_next.beginFrame++;
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR FakeLocateViews(XrSession, const XrViewLocateInfo*,
                                          XrViewState* viewState, uint32_t viewCapacityInput,
                                          uint32_t* viewCountOutput, XrView* views) {
    g_next.locateViews++;
    *viewCountOutput = viewCapacityInput > 1 ? 2 : 1;
    for (uint32_t i = 0; i < *viewCountOutput; i++) {
        views[i].type = XR_TYPE_VIEW;
        views[i].pose.orientation.w = 1.0f;
        views[i].pose.position.x = static_cast<float>(i);
        views[i].fov.angleLeft = -1.2f;
        views[i].fov.angleRight = 1.2f;
        views[i].fov.angleUp = 1.0f;
        views[i].fov.angleDown = -1.0f;
    }
    viewState->type = XR_TYPE_VIEW_STATE;
    viewState->viewStateFlags =
        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR FakeEndFrame(XrSession, const XrFrameEndInfo* frameEndInfo) {
    g_next.endFrame++;
    if (frameEndInfo) {
        g_next.lastLayerCount = frameEndInfo->layerCount;
        g_next.lastLayersPtr = frameEndInfo->layers;
    }
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR FakeEnumerateSwapchainImages(
    XrSwapchain, uint32_t, uint32_t* countOutput, XrSwapchainImageBaseHeader* images) {
    g_next.enumerate++;
    *countOutput = 3;
    struct SwapchainImageGles {
        XrStructureType type;
        const void* next;
        uint32_t image;
        uint32_t pad;
    };
    static_assert(sizeof(SwapchainImageGles) == 24, "GLES image stride");
    auto* out = reinterpret_cast<SwapchainImageGles*>(images);
    for (uint32_t i = 0; i < 3; i++) {
        out[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        out[i].next = nullptr;
        out[i].image = g_swapchainImageIds[i];
        out[i].pad = 0;
    }
    return XR_SUCCESS;
}
static XrResult XRAPI_PTR FakeAcquireSwapchainImage(XrSwapchain,
                                                    const XrSwapchainImageAcquireInfo*,
                                                    uint32_t* index) {
    g_next.acquire++;
    *index = g_acquiredIndex;
    return XR_SUCCESS;
}

static XrResult StubGipa(XrInstance, const char*, PFN_xrVoidFunction* function) {
    *function = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

static bool InstallNext() {
    return SwpDispatchSetNext(&StubGipa, &FakeCreateSession, &FakeDestroySession, &FakeBeginFrame,
                              &FakeLocateViews, &FakeEndFrame, &FakeEnumerateSwapchainImages,
                              &FakeAcquireSwapchainImage);
}

// Build a session-create chain with the GLES binding (like the Steam Link client).
static XrSessionCreateInfo GlesSessionInfo() {
    struct FakeBinding {
        XrStructureType type;
        const void* next;
        void* display;
        void* config;
        void* context;
    };
    static FakeBinding binding{};
    binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    binding.next = nullptr;
    XrSessionCreateInfo info{};
    info.type = XR_TYPE_SESSION_CREATE_INFO;
    info.next = &binding;
    return info;
}

static void DriveEnumerate() {
    struct SwapchainImageGles {
        XrStructureType type;
        const void* next;
        uint32_t image;
        uint32_t pad;
    };
    static SwapchainImageGles array[3];
    for (uint32_t i = 0; i < 3; i++) {
        array[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        array[i].next = nullptr;
        array[i].image = g_swapchainImageIds[i];
        array[i].pad = 0;
    }
    uint32_t count = 0;
    SwpDispatchEnumerateSwapchainImages(kFakeSwapchain, 3, &count,
                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(array));
}

// Projection frame-end builder: one projection layer (2 views on the configured
// swapchain) plus one overlay layer that MUST stay untouched.
struct ProjLayers {
    XrCompositionLayerProjection projection{};
    XrCompositionLayerProjectionView views[2]{};
    XrCompositionLayerQuad overlay{};
    const XrCompositionLayerBaseHeader* layers[2] = {nullptr, nullptr};
    XrFrameEndInfo info{};
};

static ProjLayers BuildProjectionLayers(XrSwapchain swapchain) {
    ProjLayers out;
    for (int eye = 0; eye < 2; eye++) {
        out.views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        out.views[eye].subImage.swapchain = swapchain;
        out.views[eye].subImage.imageArrayIndex = 0;
        out.views[eye].subImage.imageRect.offset.x = 0;
        out.views[eye].subImage.imageRect.offset.y = 0;
        out.views[eye].subImage.imageRect.extent.width = 2048;
        out.views[eye].subImage.imageRect.extent.height = 2048;
        out.views[eye].pose.orientation.w = 1.0f;
        out.views[eye].pose.position.x = static_cast<float>(eye);
        out.views[eye].fov.angleLeft = -1.2f;
        out.views[eye].fov.angleRight = 1.2f;
        out.views[eye].fov.angleUp = 1.0f;
        out.views[eye].fov.angleDown = -1.0f;
    }
    out.projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    out.projection.space = reinterpret_cast<XrSpace>(0x5E3);
    out.projection.viewCount = 2;
    out.projection.views = out.views;
    out.overlay.type = (XrStructureType)0x7777;  // non-projection overlay: forwarded untouched
    out.layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&out.projection);
    out.layers[1] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&out.overlay);
    out.info.type = XR_TYPE_FRAME_END_INFO;
    out.info.displayTime = g_fakePredDispTime;
    out.info.layerCount = 2;
    out.info.layers = out.layers;
    return out;
}

// One full tick: clock set outside; the tick records its endframe display time.
static void Tick(uint64_t predDispTime) {
    g_fakePredDispTime = predDispTime;
    XrFrameBeginInfo beginInfo{};
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
    CHECK(XR_SUCCEEDED(SwpDispatchBeginFrame(kFakeSession, &beginInfo)),
          "begin frame forwards");
    XrViewState viewState{};
    viewState.type = XR_TYPE_VIEW_STATE;
    XrView views[2]{};
    uint32_t count = 0;
    XrViewLocateInfo viewLocate{};
    viewLocate.type = XR_TYPE_VIEW_LOCATE_INFO;
    CHECK(XR_SUCCEEDED(SwpDispatchLocateViews(kFakeSession, &viewLocate, &viewState, 2, &count,
                                              views)),
          "locate views forwards");
    ProjLayers layers = BuildProjectionLayers(kFakeSwapchain);
    layers.info.displayTime = predDispTime;  // the compositor deadline this tick
    CHECK(XR_SUCCEEDED(SwpDispatchEndFrame(kFakeSession, &layers.info)), "end frame forwards");
}

static void ResetAll() {
    g_checks = 0;
    g_failures = 0;
    g_logLines.clear();
    g_next = NextCalls{};
    g_est = EstimatorCalls{};
    g_estStatus = 0;
    g_nextTextureId = 500;
    g_acquiredIndex = 0;
    g_fakePredDispTime = 0;
    g_glTableCalls = 0;
    g_glTableResult = true;
    g_boundIdentityCtx = nullptr;
    g_identityBindings = 0;
    g_fakeMvW = 1024;
    g_fakeMvH = 1024;
    g_hooksArmResult = false;  // the dispatch-flow suite re-arms deliberately
    g_glTableSet = false;      // same-TU internal: the estimator re-inits per suite
    // Brief-10 bridge: the flow suites assume a live, PUBLISHED-ON carrier; the
    // gating suite flips it explicitly (stock/unpublished is that suite's state).
    SwpVtsTestResetAll();
    SwpVtsTestSetArmed(true);
    SwpVtsTestSetRoot(reinterpret_cast<void*>(0x5A1E));
    SwpVtsTestSetTable(&BridgeMock);
    SwpVtsTestPublish(1);
    g_mockBridgePresent = true;
    g_mockBridgeValue = 1;
    g_mockBridgeReads = 0;
    SwpSetEnabled(true);
    SwpEglResetArrivals();
    SwpEglTestSetNowNs(0);
}

// ---------------------------------------------------------------------------
// Suites.
// ---------------------------------------------------------------------------
static void CheckPoseMatrix() {
    float mat[16];
    const float pos0[3] = {0, 0, 0};
    const float quatId[4] = {1, 0, 0, 0};
    SwpPoseToViewMatrix(pos0, quatId, mat);
    CHECK(mat[0] == 1 && mat[5] == 1 && mat[10] == 1 && mat[15] == 1 && mat[12] == 0 &&
              mat[13] == 0 && mat[14] == 0,
          "pose->view: identity orientation + zero position is the identity view");
    // 90-degree yaw around Y (w = z = 0.7071): view row 0 carries world +Z to +X.
    const float half = 0.70710678f;
    const float quatYaw[4] = {half, 0, half, 0};
    SwpPoseToViewMatrix(pos0, quatYaw, mat);
    CHECK(mat[8] == 1.0f, "pose->view: yaw carries +Z into view +X");
    // Position (1,0,0) under 90-degree yaw: translation column zeroes x and applies
    // -R*p = -(0,0,-1)... the x-rotated basis maps p.x into -view.z: mat[14] = -(-1) = ...
    const float pos1[3] = {1, 0, 0};
    SwpPoseToViewMatrix(pos1, quatYaw, mat);
    CHECK(mat[12] == 0.0f && mat[13] == 0.0f && mat[14] == 1.0f,
          "pose->view: translation applied on the rotated basis");
}

static void CheckScaleFactor() {
    float scale = 0.f;
    CHECK(SwpScaleFactorForMissedTick(3000, 1000, 2000, &scale) && scale > 0.99f &&
              scale < 1.01f,
          "scale factor: one progress span -> 1.0");
    CHECK(SwpScaleFactorForMissedTick(4000, 1000, 2000, &scale) && scale > 1.99f &&
              scale < 2.01f,
          "scale factor: two progress spans -> 2.0");
    CHECK(!SwpScaleFactorForMissedTick(1500, 1000, 2000, &scale),
          "scale factor: pred earlier than the newest arrival stands down");
    CHECK(!SwpScaleFactorForMissedTick(100000, 1000, 2000, &scale),
          "scale factor: beyond the cap stands down");
    CHECK(!SwpScaleFactorForMissedTick(3000, 2000, 2000, &scale),
          "scale factor: non-increasing arrivals stand down");
    CHECK(!SwpScaleFactorForMissedTick(0, 1000, 2000, &scale),
          "scale factor: zero pred stands down");
}

static void CheckMonoToXr() {
    uint64_t out = 0;
    CHECK(SwpMonoToXrTime(2000, 20000, 1000, 10000, 1500, &out) && out == 15000,
          "mono->xr: linear inside the anchor span");
    CHECK(!SwpMonoToXrTime(2000, 20000, 1000, 10000, 999, &out),
          "mono->xr: before the anchor span refuses");
    CHECK(!SwpMonoToXrTime(2000, 20000, 1000, 10000, 2001, &out),
          "mono->xr: after the anchor span refuses");
    CHECK(!SwpMonoToXrTime(2000, 20000, 2000, 10000, 1500, &out),
          "mono->xr: degenerate anchor refuses");
}

static void CheckClassify() {
    CHECK(SwpClassifyTick(5, 0, false, false) == TickKind::Real,
          "classify: first observed tick is real");
    CHECK(SwpClassifyTick(5, 5, true, false) == TickKind::Missed,
          "classify: unchanged epoch is a missed tick");
    CHECK(SwpClassifyTick(6, 5, true, false) == TickKind::Real,
          "classify: bumped epoch is a real tick");
    CHECK(SwpClassifyTick(6, 5, true, true) == TickKind::Stalled,
          "classify: disabled learning never extrapolates");
}

// RE-SHARED-IMPL.md component 4 assumption (round-robin buffers, maxImages 20):
// one epoch bump per decoded frame — recycling a past handle (wrap-around) still
// counts because the previous call's handle differs; consecutive duplicates do not;
// a pool wider than the cap disables the learning (conservative) instead of
// extrapolating blind.
static void CheckArrivalLearning() {
    ResetAll();
    uint64_t epoch = 0;
    void* ctx = nullptr;
    void* dpy = nullptr;
    for (int loop = 0; loop < 2; loop++) {
        for (int i = 0; i < 20; i++) {
            SwpEglArrivalObserved(reinterpret_cast<void*>(static_cast<uintptr_t>(10 + i)),
                                  (void*)0xC0DE, (void*)0xD05, 1000 + static_cast<uint64_t>(i));
        }
    }
    CHECK(SwpEglArrivalSnapshot(&epoch, &ctx, &dpy), "arrival snapshot after round-robin");
    CHECK(epoch == 40, "arrival epoch: every decoded frame in a 20-image round-robin counts");
    CHECK(ctx == (void*)0xC0DE, "arrival context captured");
    CHECK(dpy == (void*)0xD05, "arrival display captured");
    const uint64_t before = epoch;
    SwpEglArrivalObserved((void*)25, (void*)0xC0DE, (void*)0xD05, 2000);
    SwpEglArrivalObserved((void*)25, (void*)0xC0DE, (void*)0xD05, 2500);  // consecutive duplicate
    SwpEglArrivalSnapshot(&epoch, &ctx, &dpy);
    CHECK(epoch == before + 1, "arrival epoch: consecutive duplicate not an arrival");
    uint64_t epochs[8] = {0};
    uint64_t monos[8] = {0};
    const int historyCount = SwpEglArrivalHistory(epochs, monos, 8);
    CHECK(historyCount == 8 && epochs[0] == epoch && monos[0] == 2500,
          "arrival history: newest first");
    for (int i = 0; i < 65; i++) {
        SwpEglArrivalObserved(reinterpret_cast<void*>(static_cast<uintptr_t>(1000 + i)),
                              (void*)0xC0DE, (void*)0xD05, 3000 + static_cast<uint64_t>(i));
    }
    CHECK(SwpEglLearningDisabled(), "arrival pool wider than the cap disables learning");
}

// Full missed-tick flow: real (vectors) -> missed (extrapolate) -> cap, with the
// fake estimator + fake next chain; textures come from the passive observers.
static void CheckDispatchFlow() {
    ResetAll();
    CHECK(InstallNext(), "next chain installs");
    g_hooksArmResult = true;

    XrSessionCreateInfo info = GlesSessionInfo();
    XrSession session = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info, &session)) &&
              session == kFakeSession && g_next.createSession == 1,
          "session create forwards and tracks the handle");

    g_acquiredIndex = 1;
    DriveEnumerate();  // swapchain 0xBEEF textures = {101, 102, 103}; pending index 1 -> 102
    CHECK(g_next.enumerate == 1, "enumerate forwards");

    // Tick 1: one arrival before it -> REAL tick: history record only.
    SwpEglTestSetNowNs(1000);   // BeginFrame#1 clock anchor (mono 1000, xr 100000)
    SwpEglArrivalObserved((void*)10, (void*)0xC0DE, nullptr, 1100);  // inside tick-1 window
    Tick(100000);
    CHECK(g_next.endFrame == 1, "endframe forwards");
    CHECK(g_est.initCalled, "estimator init on the first eligible tick");
    CHECK(g_est.generateCalls == 0, "the first real tick only records history");
    CHECK(!g_logLines.empty(), "init + mv allocation logs flow");
    g_logLines.clear();

    // Tick 2: fresh arrival + acquired index 2 -> REAL tick: vectors (102 -> 103).
    g_fakePredDispTime = 200000;
    SwpEglTestSetNowNs(2000);
    g_acquiredIndex = 2;
    ProjLayers layers2 = BuildProjectionLayers(kFakeSwapchain);
    XrFrameBeginInfo begin2{};
    begin2.type = XR_TYPE_FRAME_BEGIN_INFO;
    CHECK(XR_SUCCEEDED(SwpDispatchBeginFrame(kFakeSession, &begin2)), "begin frame 2");
    XrViewState viewState2{};
    viewState2.type = XR_TYPE_VIEW_STATE;
    XrView views2[2]{};
    uint32_t count2 = 0;
    XrViewLocateInfo viewLocate2{};
    viewLocate2.type = XR_TYPE_VIEW_LOCATE_INFO;
    CHECK(XR_SUCCEEDED(SwpDispatchLocateViews(kFakeSession, &viewLocate2, &viewState2, 2, &count2,
                                              views2)),
          "locate views 2");
    SwpEglArrivalObserved((void*)11, (void*)0xC0DE, nullptr, 2100);  // fresh arrival
    CHECK(XR_SUCCEEDED(SwpDispatchEndFrame(kFakeSession, &layers2.info)), "end frame 2");
    CHECK(g_est.generateCalls == 1, "vector generation on the second real tick");
    CHECK(g_est.lastPrevTexL == 102 && g_est.lastCurrTexL == 103,
          "vectors use prev=older, curr=newest textures");
    CHECK(g_est.lastMvTexL != 0 && g_est.lastMvTexR != 0, "mv textures from the client context");
    g_logLines.clear();

    // Tick 3: no fresh arrival -> MISSED tick -> extrapolation into the pending dst.
    g_fakePredDispTime = 300000;
    SwpEglTestSetNowNs(3000);
    g_acquiredIndex = 0;  // round-robin wrap: pending image index 0 -> texture 101
    ProjLayers layers3 = BuildProjectionLayers(kFakeSwapchain);
    XrFrameBeginInfo begin3{};
    begin3.type = XR_TYPE_FRAME_BEGIN_INFO;
    CHECK(XR_SUCCEEDED(SwpDispatchBeginFrame(kFakeSession, &begin3)), "begin frame 3");
    XrViewState viewState3{};
    viewState3.type = XR_TYPE_VIEW_STATE;
    XrView views3[2]{};
    uint32_t count3 = 0;
    XrViewLocateInfo viewLocate3{};
    viewLocate3.type = XR_TYPE_VIEW_LOCATE_INFO;
    CHECK(XR_SUCCEEDED(SwpDispatchLocateViews(kFakeSession, &viewLocate3, &viewState3, 2, &count3,
                                              views3)),
          "locate views 3");
    CHECK(XR_SUCCEEDED(SwpDispatchEndFrame(kFakeSession, &layers3.info)), "end frame 3");
    CHECK(g_est.extrapolateCalls == 1, "extrapolation on the missed tick");
    CHECK(g_est.lastDstTexL == 101 && g_est.lastDstTexR == 101,
          "extrapolation targets the pending subimage textures");
    CHECK(g_est.lastSrcTexL == 103 && g_est.lastSrcTexR == 103,
          "extrapolation sources the last presented pair");
    CHECK(g_est.lastScale >= 0.5f && g_est.lastScale <= 1.5f,
          "extrapolation scale is inside the sane range (synthetic anchors)");
    CHECK(g_next.lastLayerCount == 2, "the layer list reaches the runtime untouched");
    g_logLines.clear();

    // Ticks 4-6: pass stock once more than 2 consecutive missed ticks.
    uint64_t xr = 400000;
    for (int tick = 0; tick < 2; tick++) {
        SwpEglTestSetNowNs(4000 + tick * 1000);
        Tick(xr);
        xr += 100000;
    }
    const int extrapolated = g_est.extrapolateCalls;
    CHECK(extrapolated == 2, "the second consecutive missed tick still extrapolates");
    Tick(xr);  // third consecutive missed tick: the cap passes stock
    CHECK(g_est.extrapolateCalls == extrapolated,
          "the consecutive cap passes stock without estimator calls");
    bool capLogged = false;
    for (const std::string& line : g_logLines) {
        if (line.find("consecutive missed-tick cap") != std::string::npos) capLogged = true;
    }
    CHECK(capLogged, "the cap logs its (rate-limited) line");
}

static void CheckStandDowns() {
    ResetAll();
    CHECK(InstallNext(), "next chain installs (stand-down run)");

    XrSessionCreateInfo info = GlesSessionInfo();
    XrSession session = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info, &session)), "session create");
    g_acquiredIndex = 1;
    DriveEnumerate();
    // Context mismatch: the arrival's context differs from the current -> stand down.
    SwpEglTestSetNowNs(1000);
    SwpEglArrivalObserved((void*)10, (void*)0xBADF, nullptr, 1100);
    SwpEglTestSetNowNs(1200);
    Tick(100000);
    CHECK(g_est.generateCalls == 0 && g_est.extrapolateCalls == 0,
          "context mismatch stands the tick down");

    // Estimator init failure: session-wide stand-down (contract status 16).
    g_est.initResult = false;
    g_est.initCalled = false;
    SwpEglResetArrivals();
    SwpEglTestSetNowNs(2000);
    SwpEglArrivalObserved((void*)20, (void*)0xC0DE, nullptr, 2100);
    Tick(500000);
    CHECK(g_est.generateCalls == 0 && g_est.extrapolateCalls == 0,
          "failed estimator init stands the session down");
    g_est.initResult = true;

    // The ONE boolean enable entry: a disabled entry makes new sessions pass stock.
    SwpSetEnabled(false);
    XrSessionCreateInfo info2 = GlesSessionInfo();
    XrSession session2 = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info2, &session2)),
          "disabled-entry session create forwards");
    SwpEglResetArrivals();
    SwpEglTestSetNowNs(3000);
    SwpEglArrivalObserved((void*)30, (void*)0xC0DE, nullptr, 3100);
    g_acquiredIndex = 1;
    Tick(700000);
    CHECK(g_est.generateCalls == 0 && g_est.extrapolateCalls == 0,
          "the entry flipped to disabled keeps new sessions stock");
    // Re-enable applies to the NEXT session only.
    SwpSetEnabled(true);
    XrSessionCreateInfo info3 = GlesSessionInfo();
    XrSession session3 = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info3, &session3)),
          "re-enabled session create forwards");
    g_est.initCalled = false;
    SwpEglResetArrivals();
    SwpEglTestSetNowNs(4000);
    SwpEglArrivalObserved((void*)40, (void*)0xC0DE, nullptr, 4100);
    g_acquiredIndex = 1;
    Tick(900000);
    CHECK(g_est.initCalled, "the next session re-reads the enable entry and runs");
}

// Brief-10 enable gating: ALL synthesis (MV-gen + extrapolation) only while the
// bridge carrier reads enabled (published nonzero); unknown / never-published /
// zero sessions stay stock while the observers keep running; a mid-session flip
// to enabled never extrapolates from pre-enablement content (fresh-map rule).
static void CheckBridgeGating() {
    ResetAll();
    CHECK(InstallNext(), "next chain installs (bridge gating run)");

    // Stock session preset: armed + root captured + the publisher silent (the
    // mock answers not-present); the carrier cache restarts UNKNOWN (no poll
    // has run). This is the state every stock session is in.
    SwpVtsTestResetAll();
    SwpVtsTestSetArmed(true);
    SwpVtsTestSetRoot(reinterpret_cast<void*>(0x5A1E));
    SwpVtsTestSetTable(&BridgeMock);
    g_mockBridgePresent = false;

    // The (test) clock advances one unit per tick so the arrival-anchor ring
    // stays live for the late conversions (the flow suite's same shape; the
    // 8-entry ring survives because arrivals keep flowing in the flip phase).
    uint64_t clock = 1000;
    uint64_t pred = 100000;
    auto GateTick = [&clock, &pred]() {
        clock += 1000;
        SwpEglTestSetNowNs(clock);
        Tick(pred);
        pred += 100000;
    };

    XrSessionCreateInfo info = GlesSessionInfo();
    XrSession session = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info, &session)),
          "stock-carrier session create forwards");
    g_acquiredIndex = 1;
    DriveEnumerate();
    SwpEglTestSetNowNs(clock);
    SwpEglArrivalObserved((void*)10, (void*)0xC0DE, nullptr, clock + 100);
    GateTick();  // tick 1 = real (first observed epoch); the disabled gate is stock
    CHECK(g_next.endFrame == 1, "stock-carrier endframe still forwards");
    CHECK(!g_est.initCalled && g_est.generateCalls == 0 && g_est.extrapolateCalls == 0,
          "an unpublished carrier holds ALL synthesis down from tick 1");

    // Ticks 2..60 drive the 60-endframe cadence (missed ticks, all stock): the
    // poll reads at tick 60, treats 'not present' as disabled, logs the fallback
    // line ONCE for the session, and never touches the estimator.
    for (int i = 1; i < 60; i++) GateTick();
    CHECK(g_mockBridgeReads == 1, "the bridge poll ran once on the 60-endframe cadence");
    CHECK(!g_est.initCalled && g_est.generateCalls == 0 && g_est.extrapolateCalls == 0,
          "the never-published session stays stock through repeated polls");
    bool notPresentLogged = false;
    for (const std::string& line : g_logLines) {
        if (line.find("carrier value not present") != std::string::npos) {
            notPresentLogged = true;
        }
    }
    CHECK(notPresentLogged, "the not-present fallback logged its one line");

    // The host publishes nonzero: the flip lands via the NEXT cadence poll (60
    // ticks later). Fresh epochs every other tick keep the anchor ring dense;
    // the poll tick (missed, freshly allowed) must STILL not extrapolate (the
    // fresh-map rule) while the estimator arms (init + MV allocation) in the
    // allowed path.
    g_mockBridgePresent = true;
    for (int i = 0; i < 60; i++) {
        if (i % 2 == 0) {
            SwpEglArrivalObserved(reinterpret_cast<void*>(static_cast<uintptr_t>(20 + i)),
                                  (void*)0xC0DE, nullptr, clock + 100);
        }
        GateTick();
    }
    CHECK(g_mockBridgeReads >= 2, "the flip landed via the cadence poll");
    CHECK(g_est.initCalled,
          "estimator bring-up ran in the allowed path right after the flip");
    CHECK(g_est.generateCalls == 0,
          "the pre-poll ticks were gated stock (no generation, no extrapolation)");
    CHECK(g_est.extrapolateCalls == 0,
          "fresh enablement never extrapolates from pre-enablement maps");

    // A fresh real tick (new texture + fresh epoch) generates the maps...
    g_acquiredIndex = 2;
    SwpEglArrivalObserved((void*)40, (void*)0xC0DE, nullptr, clock + 100);
    GateTick();
    CHECK(g_est.initCalled, "estimator init stays in the allowed path");
    CHECK(g_est.generateCalls == 1, "the fresh real tick generated the maps");
    // ...and the next missed tick extrapolates into the pending dst textures.
    g_acquiredIndex = 0;  // round-robin wrap: the missed tick's dst is texture 101
    GateTick();
    CHECK(g_est.extrapolateCalls == 1, "the next missed tick extrapolates");
    CHECK(g_est.lastDstTexL == 101 && g_est.lastDstTexR == 101,
          "the extrapolated tick targets the pending subimage textures");
    CHECK(g_est.lastSrcTexL == 103 && g_est.lastSrcTexR == 103,
          "the extrapolation sources the post-enablement presented pair");

    // The per-session entry stays authoritative: entry off + carrier on = stock.
    SwpSetEnabled(false);
    XrSessionCreateInfo info2 = GlesSessionInfo();
    XrSession session2 = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info2, &session2)),
          "entry-off session create forwards");
    SwpEglResetArrivals();
    clock += 1000;
    SwpEglTestSetNowNs(clock);
    SwpEglArrivalObserved((void*)50, (void*)0xC0DE, nullptr, clock + 100);
    g_acquiredIndex = 1;
    const int generateCallsBefore = g_est.generateCalls;
    const int extrapolateCallsBefore = g_est.extrapolateCalls;
    pred += 100000;
    Tick(pred);
    CHECK(g_est.generateCalls == generateCallsBefore &&
              g_est.extrapolateCalls == extrapolateCallsBefore,
          "entry off + carrier on stays stock (the entry wins)");
    SwpSetEnabled(true);
}

static void CheckSessionTeardown() {
    ResetAll();
    CHECK(InstallNext(), "next chain installs (teardown run)");

    XrSessionCreateInfo info = GlesSessionInfo();
    XrSession session = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info, &session)), "session create");
    g_acquiredIndex = 1;
    DriveEnumerate();
    SwpEglTestSetNowNs(1000);
    SwpEglArrivalObserved((void*)10, (void*)0xC0DE, nullptr, 1100);
    SwpEglTestSetNowNs(1200);
    Tick(100000);
    CHECK(g_est.initCalled, "estimator initialized before teardown");
    CHECK(XR_SUCCEEDED(SwpDispatchDestroySession(kFakeSession)), "destroy session forwards");
    // After teardown, calls with the stale session: pure passthrough, no state.
    g_est.initCalled = false;
    ProjLayers layers2 = BuildProjectionLayers(kFakeSwapchain);
    Tick(300000);
    CHECK(g_est.generateCalls == 0 && g_est.extrapolateCalls == 0,
          "no estimator activity for the torn-down session");
    XrSessionCreateInfo info2 = GlesSessionInfo();
    XrSession recreated = nullptr;
    CHECK(XR_SUCCEEDED(SwpDispatchCreateSession((XrInstance)0x1, &info2, &recreated)),
          "session handle may be reused after teardown");
}

int main() {
    CheckPoseMatrix();
    CheckScaleFactor();
    CheckMonoToXr();
    CheckClassify();
    CheckArrivalLearning();
    CheckDispatchFlow();
    CheckBridgeGating();
    CheckStandDowns();
    CheckSessionTeardown();
    std::printf("gxr-spacewarp dispatch host tests: %d checks, %d failures\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
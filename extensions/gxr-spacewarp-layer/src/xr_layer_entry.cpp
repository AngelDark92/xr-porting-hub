// gxr-spacewarp: OpenXR implicit-layer entry (09-LAYER-B1b component 1).
//
// Mirrors the codecs-support-layer's loader discipline (09-LAYER-B1b: "mirror the
// loader-log discipline used by the other layers"):
//   - xrNegotiateLoaderApiLayerInterface as the only mandatory export, accepting the
//     loader's negotiation with the XR_APILAYER_local_GalaxyXR_gxr_spacewarp name.
//   - getInstanceProcAddr runs BEFORE the instance chain exists: the loader calls it
//     for every negotiated implicit layer with XR_NULL_HANDLE before any
//     createApiLayerInstance — the MANDATORY guard never forwards through an
//     unassigned chain (nullptr NEXT => *function=nullptr +
//     XR_ERROR_FUNCTION_UNSUPPORTED; returning an error is safe, forwarding through
//     null kills the app inside xrCreateInstance on every start).
//   - Interception is limited to the brief's list plus the two passive observers:
//     xrCreateSession (+ its adjacent lifecycle call xrDestroySession for per-session
//     state reset), xrBeginFrame, xrLocateViews, xrEndFrame, and the passthrough-only
//     xrEnumerateSwapchainImages / xrAcquireSwapchainImage observers (README +
//     receipt document this justified extension: the contract's dstTex demands the
//     GL texture behind the subimage, which is only reachable via enumeration +
//     acquisition state; both wrappers mutate nothing).
//   - One-shot enable decision per session (read in the dispatch at session init)
//     with a fail-safe stand-down model; logging discipline = one line on load, one
//     at init, one-line fail-closed messages, rate-limited failure lines only.

#include <android/log.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <cstring>

// Dispatch entry points (definitions in swp_dispatch.cpp) + the one-shot enable
// entry from the shared contract; routing + forwarding live there.
extern "C" {
bool SwpDispatchSetNext(PFN_xrGetInstanceProcAddr gipa, PFN_xrCreateSession createSession,
                        PFN_xrDestroySession destroySession, PFN_xrBeginFrame beginFrame,
                        PFN_xrLocateViews locateViews, PFN_xrEndFrame endFrame,
                        PFN_xrEnumerateSwapchainImages enumerateSwapchainImages,
                        PFN_xrAcquireSwapchainImage acquireSwapchainImage);
void SwpDispatchTryArm();
XrResult SwpDispatchCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo,
                                  XrSession* session);
XrResult SwpDispatchDestroySession(XrSession session);
XrResult SwpDispatchBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo);
XrResult SwpDispatchLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo,
                                XrViewState* viewState, uint32_t viewCapacityInput,
                                uint32_t* viewCountOutput, XrView* views);
XrResult SwpDispatchEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo);
XrResult SwpDispatchEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t countInput,
                                             uint32_t* countOutput,
                                             XrSwapchainImageBaseHeader* images);
XrResult SwpDispatchAcquireSwapchainImage(XrSwapchain swapchain,
                                          const XrSwapchainImageAcquireInfo* acquireInfo,
                                          uint32_t* index);
void SwpSetEnabled(bool enabled);
bool SwpGetEnabled();
}

#define EXPORT extern "C" __attribute__((visibility("default")))
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "GXRSwarp", __VA_ARGS__)

namespace {

constexpr const char* kLayerName = "XR_APILAYER_local_GalaxyXR_gxr_spacewarp";
constexpr const char* kBuild = "gxr-spacewarp-w2a-20261017"; // brief-10 wiring wave

// ---------------------------------------------------------------------------
// The next-chain GIPA (name forwarding only) and the wrapper functions routed
// by getInstanceProcAddr. The per-frame wrappers delegate to the dispatch and
// forward through the table installed into it (SwpDispatchSetNext).
// ---------------------------------------------------------------------------
struct Route {
    PFN_xrGetInstanceProcAddr gipa;
    PFN_xrCreateSession createSession;
    PFN_xrDestroySession destroySession;
    PFN_xrBeginFrame beginFrame;
    PFN_xrLocateViews locateViews;
    PFN_xrEndFrame endFrame;
    PFN_xrEnumerateSwapchainImages enumerateSwapchainImages;
    PFN_xrAcquireSwapchainImage acquireSwapchainImage;
} g{};

XrResult XRAPI_PTR WrapCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo,
                                     XrSession* session) {
    return SwpDispatchCreateSession(instance, createInfo, session);
}

XrResult XRAPI_PTR WrapDestroySession(XrSession session) {
    return SwpDispatchDestroySession(session);
}

XrResult XRAPI_PTR WrapBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo) {
    return SwpDispatchBeginFrame(session, frameBeginInfo);
}

XrResult XRAPI_PTR WrapLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo,
                                   XrViewState* viewState, uint32_t viewCapacityInput,
                                   uint32_t* viewCountOutput, XrView* views) {
    return SwpDispatchLocateViews(session, viewLocateInfo, viewState, viewCapacityInput,
                                  viewCountOutput, views);
}

XrResult XRAPI_PTR WrapEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    return SwpDispatchEndFrame(session, frameEndInfo);
}

XrResult XRAPI_PTR WrapEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t countInput,
                                                uint32_t* countOutput,
                                                XrSwapchainImageBaseHeader* images) {
    return SwpDispatchEnumerateSwapchainImages(swapchain, countInput, countOutput, images);
}

XrResult XRAPI_PTR WrapAcquireSwapchainImage(XrSwapchain swapchain,
                                             const XrSwapchainImageAcquireInfo* acquireInfo,
                                             uint32_t* index) {
    return SwpDispatchAcquireSwapchainImage(swapchain, acquireInfo, index);
}

}  // namespace

// ---------------------------------------------------------------------------
// Layer getInstanceProcAddr. MANDATORY GUARD: when the next chain is unassigned
// (g.gipa == nullptr — the loader may call this with XR_NULL_HANDLE before any
// createApiLayerInstance ran), never forward: return XR_ERROR_FUNCTION_UNSUPPORTED
// with *function = nullptr. The loader skips its init forward on the error; the
// app keeps its stock path. Forwarding through null is documented SIGSEGV in
// xrCreateInstance for every layer mount.
// ---------------------------------------------------------------------------
extern "C" XrResult XRAPI_PTR LayerGetInstanceProcAddr(XrInstance instance, const char* name,
                                                       PFN_xrVoidFunction* function) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
#define ROUTE(fnName, member)                              \
    if (std::strcmp(name, fnName) == 0) {                  \
        *function = reinterpret_cast<PFN_xrVoidFunction>(member); \
        return XR_SUCCESS;                                 \
    }
    ROUTE("xrCreateSession", WrapCreateSession);
    ROUTE("xrDestroySession", WrapDestroySession);
    ROUTE("xrBeginFrame", WrapBeginFrame);
    ROUTE("xrLocateViews", WrapLocateViews);
    ROUTE("xrEndFrame", WrapEndFrame);
    ROUTE("xrEnumerateSwapchainImages", WrapEnumerateSwapchainImages);
    ROUTE("xrAcquireSwapchainImage", WrapAcquireSwapchainImage);
#undef ROUTE
    // Unknown name: forward through the next chain only when it is assigned; the
    // mandatory guard applies exactly as for our own routes' fallbacks.
    if (g.gipa) return g.gipa(instance, name, function);
    *function = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

namespace {

XrResult LayerCreateInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer,
                             XrInstance* instance) {
    if (!info || !layer || !layer->nextInfo || !instance) return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo next = *layer;
    next.nextInfo = layer->nextInfo->next;
    const XrResult result = layer->nextInfo->nextCreateApiLayerInstance(info, &next, instance);
    if (XR_FAILED(result)) return result;
    g.gipa = layer->nextInfo->nextGetInstanceProcAddr;

#define LOAD(fnName, member)                                                               \
    if (XR_FAILED(g.gipa(*instance, fnName, reinterpret_cast<PFN_xrVoidFunction*>(&member))) || \
        !member) {                                                                         \
        LOG("required function %s unavailable; layer inactive", fnName);                   \
        g = Route{};                                                                       \
        return XR_ERROR_INITIALIZATION_FAILED;                                             \
    }
    PFN_xrDestroySession nextDestroySession = nullptr;
    PFN_xrBeginFrame nextBeginFrame = nullptr;
    PFN_xrLocateViews nextLocateViews = nullptr;
    PFN_xrEndFrame nextEndFrame = nullptr;
    PFN_xrEnumerateSwapchainImages nextEnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage nextAcquireSwapchainImage = nullptr;
    // Resolve through the NEXT chain's GIPA (never our own function pointers).
    LOAD("xrCreateSession", g.createSession);
    LOAD("xrDestroySession", nextDestroySession);
    LOAD("xrBeginFrame", nextBeginFrame);
    LOAD("xrLocateViews", nextLocateViews);
    LOAD("xrEndFrame", nextEndFrame);
    LOAD("xrEnumerateSwapchainImages", nextEnumerateSwapchainImages);
    LOAD("xrAcquireSwapchainImage", nextAcquireSwapchainImage);
#undef LOAD
    if (!SwpDispatchSetNext(g.gipa, g.createSession, nextDestroySession, nextBeginFrame,
                            nextLocateViews, nextEndFrame, nextEnumerateSwapchainImages,
                            nextAcquireSwapchainImage)) {
        LOG("next-chain incomplete; layer inactive");
        g = Route{};
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    LOG("initialized build=%s (enable entry: SwpSetEnabled; disable env: "
        "GXR_DISABLE_GXR_SPACEWARP)", kBuild);
    return result;
}

}  // namespace

EXPORT XrResult XRAPI_PTR xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char* name,
    XrNegotiateApiLayerRequest* request) {
    if (!loader || !name || !request || std::strcmp(name, kLayerName) != 0 ||
        loader->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loader->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loader->maxApiVersion < XR_MAKE_VERSION(1, 0, 0) ||
        loader->minApiVersion > XR_MAKE_VERSION(1, 1, 0)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = std::min<XrVersion>(loader->maxApiVersion, XR_MAKE_VERSION(1, 1, 0));
    request->getInstanceProcAddr = LayerGetInstanceProcAddr;
    request->createApiLayerInstance = LayerCreateInstance;
    return XR_SUCCESS;
}

__attribute__((constructor)) void OnLoad() {
    LOG("library loaded build=%s", kBuild);
    SwpDispatchTryArm();  // arm if the scene lib already loaded; retried per XR call
}
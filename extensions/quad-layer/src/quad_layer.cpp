#include <android/log.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#define GXR_EXPORT extern "C" __attribute__((visibility("default")))

#ifndef GXR_QUAD_ONLY
#define GXR_QUAD_ONLY 0
#endif

namespace {

#if GXR_QUAD_ONLY
constexpr char kLayerName[] = "XR_APILAYER_local_GalaxyXR_quad_only_v1";
constexpr char kModeName[] = "quad_only_composition_v1";
constexpr char kBuildId[] = "gxr-quad-only-v1.0-20261025";
#else
constexpr char kLayerName[] = "XR_APILAYER_local_GalaxyXR_fovea_quad_v1";
constexpr char kModeName[] = "fovea_quad_layers_v1";
constexpr char kBuildId[] = "gxr-fovea-quad-v1.0-20261025";
#endif
constexpr char kLogTag[] = "GXRQuadLayer";

// A one-metre quad plane in front of the eye. The size is computed from the
// projection view's tangent extents, so the angular coverage is independent of
// this distance.
constexpr float kQuadDistanceMeters = 1.0f;
constexpr float kMaxHalfAngleRadians = 1.50f;

struct Dispatch {
    XrInstance instance{XR_NULL_HANDLE};
    PFN_xrGetInstanceProcAddr getInstanceProcAddr{};
    PFN_xrDestroyInstance destroyInstance{};
    PFN_xrEndFrame endFrame{};
};

Dispatch g;
// Distinct pass-through reasons are each logged once per instance. Conversion is
// logged for the first frames so startup composition cannot consume the only
// diagnostic line.
std::atomic<uint32_t> loggedPassReasons{0};
std::atomic<uint32_t> convertedLogs{0};
// A runtime rejection switches this session back to the app's original frame
// submission. The rejected frame is returned to the app, never resubmitted.
std::atomic<bool> conversionDisabled{false};

bool finitePose(const XrPosef& pose) {
    return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
        std::isfinite(pose.position.z) && std::isfinite(pose.orientation.x) &&
        std::isfinite(pose.orientation.y) && std::isfinite(pose.orientation.z) &&
        std::isfinite(pose.orientation.w);
}

bool normalizedOrientation(const XrQuaternionf& in, XrQuaternionf& out) {
    const float lengthSquared = in.x * in.x + in.y * in.y + in.z * in.z + in.w * in.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f) return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    out = {in.x * inverseLength, in.y * inverseLength, in.z * inverseLength,
        in.w * inverseLength};
    return true;
}

XrVector3f rotate(const XrQuaternionf& orientation, const XrVector3f& value) {
    const XrVector3f axis{orientation.x, orientation.y, orientation.z};
    const XrVector3f twiceCross{
        2.0f * (axis.y * value.z - axis.z * value.y),
        2.0f * (axis.z * value.x - axis.x * value.z),
        2.0f * (axis.x * value.y - axis.y * value.x),
    };
    const XrVector3f axisCrossTwiceCross{
        axis.y * twiceCross.z - axis.z * twiceCross.y,
        axis.z * twiceCross.x - axis.x * twiceCross.z,
        axis.x * twiceCross.y - axis.y * twiceCross.x,
    };
    return {
        value.x + orientation.w * twiceCross.x + axisCrossTwiceCross.x,
        value.y + orientation.w * twiceCross.y + axisCrossTwiceCross.y,
        value.z + orientation.w * twiceCross.z + axisCrossTwiceCross.z,
    };
}

bool validFov(const XrFovf& fov) {
    const float angles[] = {fov.angleLeft, fov.angleRight, fov.angleDown, fov.angleUp};
    for (float angle : angles) {
        if (!std::isfinite(angle) || angle <= -kMaxHalfAngleRadians ||
            angle >= kMaxHalfAngleRadians) {
            return false;
        }
    }
    return fov.angleLeft < fov.angleRight && fov.angleDown < fov.angleUp;
}

bool validProjection(const XrCompositionLayerProjection* projection) {
    if (!projection || projection->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION ||
        projection->next || projection->viewCount != 2 || !projection->views ||
        projection->space == XR_NULL_HANDLE) {
        return false;
    }
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const auto& view = projection->views[eye];
        if (view.next || view.subImage.swapchain == XR_NULL_HANDLE ||
            view.subImage.imageRect.extent.width <= 0 ||
            view.subImage.imageRect.extent.height <= 0 || !validFov(view.fov) ||
            !finitePose(view.pose)) {
            return false;
        }
    }
    return true;
}

bool makeQuad(
    const XrCompositionLayerProjectionView& view,
    XrEyeVisibility eye,
    XrCompositionLayerFlags layerFlags,
    XrSpace space,
    XrCompositionLayerQuad& quad
) {
    const float tanLeft = std::tan(view.fov.angleLeft);
    const float tanRight = std::tan(view.fov.angleRight);
    const float tanDown = std::tan(view.fov.angleDown);
    const float tanUp = std::tan(view.fov.angleUp);
    if (!std::isfinite(tanLeft) || !std::isfinite(tanRight) ||
        !std::isfinite(tanDown) || !std::isfinite(tanUp)) {
        return false;
    }

    const float centerX = 0.5f * (tanLeft + tanRight);
    const float centerY = 0.5f * (tanDown + tanUp);
    const float width = (tanRight - tanLeft) * kQuadDistanceMeters;
    const float height = (tanUp - tanDown) * kQuadDistanceMeters;
    if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f) {
        return false;
    }

    XrQuaternionf orientation{};
    if (!normalizedOrientation(view.pose.orientation, orientation)) return false;
    const XrVector3f offset{
        centerX * kQuadDistanceMeters,
        centerY * kQuadDistanceMeters,
        -kQuadDistanceMeters,
    };
    const XrVector3f rotated = rotate(orientation, offset);

    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.next = nullptr;
    quad.layerFlags = layerFlags;
    quad.space = space;
    quad.eyeVisibility = eye;
    quad.subImage = view.subImage;
    quad.pose.orientation = orientation;
    quad.pose.position = {
        view.pose.position.x + rotated.x,
        view.pose.position.y + rotated.y,
        view.pose.position.z + rotated.z,
    };
    quad.size = {width, height};
    return finitePose(quad.pose) && std::isfinite(quad.size.width) &&
        std::isfinite(quad.size.height) && quad.size.width > 0.0f &&
        quad.size.height > 0.0f;
}

bool makeQuads(
    const XrCompositionLayerProjection& projection,
    std::array<XrCompositionLayerQuad, 2>& quads
) {
    for (uint32_t eye = 0; eye < 2; ++eye) {
        const XrEyeVisibility visibility = eye == 0 ?
            XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
        if (!makeQuad(projection.views[eye], visibility, projection.layerFlags,
                projection.space, quads[eye])) {
            return false;
        }
    }
    return true;
}

enum class PassReason : uint32_t {
    kLayerCount = 0,
    kProjectionShape,
    kSpace,
    kFoveaFlags,
    kBaseGeometry,
    kFoveaGeometry,
};

void logLine(
    const char* result,
    uint32_t sourceLayerCount,
    uint32_t outputLayerCount,
    XrCompositionLayerFlags baseFlags,
    XrCompositionLayerFlags foveaFlags
) {
    const std::string message =
        "{\"mode\":\"" + std::string(kModeName) + "\",\"buildId\":\"" +
        std::string(kBuildId) + "\",\"arm\":\"" +
        (GXR_QUAD_ONLY ? "quad_only" : "fovea_quad") + "\",\"result\":\"" +
        std::string(result) + "\",\"sourceLayerCount\":" +
        std::to_string(sourceLayerCount) + ",\"outputLayerCount\":" +
        std::to_string(outputLayerCount) + ",\"baseFlags\":" +
        std::to_string(static_cast<uint64_t>(baseFlags)) + ",\"foveaFlags\":" +
        std::to_string(static_cast<uint64_t>(foveaFlags)) + "}";
    __android_log_write(ANDROID_LOG_INFO, kLogTag, message.c_str());
}

void logPassOnce(
    PassReason reason,
    const char* result,
    uint32_t sourceLayerCount,
    uint32_t outputLayerCount,
    XrCompositionLayerFlags baseFlags,
    XrCompositionLayerFlags foveaFlags
) {
    const uint32_t bit = 1u << static_cast<uint32_t>(reason);
    if (loggedPassReasons.fetch_or(bit, std::memory_order_relaxed) & bit) return;
    logLine(result, sourceLayerCount, outputLayerCount, baseFlags, foveaFlags);
}

void logConverted(
    uint32_t sourceLayerCount,
    uint32_t outputLayerCount,
    XrCompositionLayerFlags baseFlags,
    XrCompositionLayerFlags foveaFlags
) {
    if (convertedLogs.fetch_add(1, std::memory_order_relaxed) >= 3) return;
    logLine("converted", sourceLayerCount, outputLayerCount, baseFlags, foveaFlags);
}

XrResult XRAPI_PTR layerEndFrame(XrSession session, const XrFrameEndInfo* info) {
    if (!g.endFrame) return XR_ERROR_FUNCTION_UNSUPPORTED;
    if (conversionDisabled.load(std::memory_order_relaxed)) {
        return g.endFrame(session, info);
    }
    // Only the exact 2-projection Steam Link topology is converted. Everything
    // else, including the app's own frame next chain and display timing, passes
    // through unchanged.
    if (!info || info->type != XR_TYPE_FRAME_END_INFO || !info->layers ||
        info->layerCount != 2) {
        logPassOnce(PassReason::kLayerCount, "pass_through_layer_count",
            info ? info->layerCount : 0, info ? info->layerCount : 0, 0, 0);
        return g.endFrame(session, info);
    }
    const auto* base = reinterpret_cast<const XrCompositionLayerProjection*>(
        info->layers[0]);
    const auto* fovea = reinterpret_cast<const XrCompositionLayerProjection*>(
        info->layers[1]);
    if (!validProjection(base) || !validProjection(fovea)) {
        logPassOnce(PassReason::kProjectionShape, "pass_through_projection_shape",
            info->layerCount, info->layerCount, 0, 0);
        return g.endFrame(session, info);
    }
    if (base->space != fovea->space) {
        logPassOnce(PassReason::kSpace, "pass_through_space_mismatch",
            info->layerCount, info->layerCount, base->layerFlags, fovea->layerFlags);
        return g.endFrame(session, info);
    }
    if (fovea->layerFlags != (XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
            XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT)) {
        logPassOnce(PassReason::kFoveaFlags, "pass_through_fovea_flags",
            info->layerCount, info->layerCount, base->layerFlags, fovea->layerFlags);
        return g.endFrame(session, info);
    }

    std::array<XrCompositionLayerQuad, 2> baseQuads{};
    std::array<XrCompositionLayerQuad, 2> foveaQuads{};
    if (GXR_QUAD_ONLY && !makeQuads(*base, baseQuads)) {
        logPassOnce(PassReason::kBaseGeometry, "pass_through_base_geometry",
            info->layerCount, info->layerCount, base->layerFlags, fovea->layerFlags);
        return g.endFrame(session, info);
    }
    if (!makeQuads(*fovea, foveaQuads)) {
        logPassOnce(PassReason::kFoveaGeometry, "pass_through_fovea_geometry",
            info->layerCount, info->layerCount, base->layerFlags, fovea->layerFlags);
        return g.endFrame(session, info);
    }

    std::array<const XrCompositionLayerBaseHeader*, 4> layers{};
    uint32_t layerCount = 0;
    if (GXR_QUAD_ONLY) {
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &baseQuads[0]);
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &baseQuads[1]);
    } else {
        // Keep Valve's base projection pointer/order; replace only the fovea.
        layers[layerCount++] = info->layers[0];
    }
    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
        &foveaQuads[0]);
    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
        &foveaQuads[1]);

    XrFrameEndInfo output = *info;
    output.layerCount = layerCount;
    output.layers = layers.data();
    const XrResult result = g.endFrame(session, &output);
    if (XR_SUCCEEDED(result)) {
        logConverted(info->layerCount, layerCount, base->layerFlags, fovea->layerFlags);
    } else {
        conversionDisabled.store(true, std::memory_order_relaxed);
        logLine("end_frame_rejected", info->layerCount, layerCount, base->layerFlags,
            fovea->layerFlags);
    }
    return result;
}

bool load(const char* name, PFN_xrVoidFunction* function) {
    *function = nullptr;
    if (!g.getInstanceProcAddr) return false;
    PFN_xrVoidFunction address{};
    const XrResult result = g.getInstanceProcAddr(g.instance, name, &address);
    *function = address;
    return XR_SUCCEEDED(result) && address;
}

XrResult XRAPI_PTR layerDestroyInstance(XrInstance instance) {
    if (!g.destroyInstance) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult result = g.destroyInstance(instance);
    if (XR_SUCCEEDED(result)) {
        g = {};
        conversionDisabled.store(false, std::memory_order_relaxed);
        loggedPassReasons.store(0, std::memory_order_relaxed);
        convertedLogs.store(0, std::memory_order_relaxed);
    }
    return result;
}

XrResult XRAPI_PTR layerGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function
) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;
    // The loader queries some names before createApiLayerInstance assigns the
    // next dispatch table. A null output with XR_ERROR_FUNCTION_UNSUPPORTED is
    // the loader-tolerated "not provided by this layer" answer.
    *function = nullptr;
#define ROUTE(n, f) if (std::strcmp(name, n) == 0) \
    *function = reinterpret_cast<PFN_xrVoidFunction>(f)
    ROUTE("xrGetInstanceProcAddr", layerGetInstanceProcAddr);
    else ROUTE("xrDestroyInstance", layerDestroyInstance);
    else ROUTE("xrEndFrame", layerEndFrame);
    else return g.getInstanceProcAddr ?
        g.getInstanceProcAddr(instance, name, function) :
        XR_ERROR_FUNCTION_UNSUPPORTED;
#undef ROUTE
    return XR_SUCCESS;
}

XrResult XRAPI_PTR layerCreateApiLayerInstance(
    const XrInstanceCreateInfo* createInfo,
    const XrApiLayerCreateInfo* layerInfo,
    XrInstance* instance
) {
    if (!createInfo || !layerInfo || !layerInfo->nextInfo || !instance) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    XrApiLayerCreateInfo next = *layerInfo;
    next.nextInfo = layerInfo->nextInfo->next;
    const XrResult result = layerInfo->nextInfo->nextCreateApiLayerInstance(
        createInfo, &next, instance);
    if (XR_FAILED(result)) return result;

    g.instance = *instance;
    g.getInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;
    conversionDisabled.store(false, std::memory_order_relaxed);
    loggedPassReasons.store(0, std::memory_order_relaxed);
    convertedLogs.store(0, std::memory_order_relaxed);
    PFN_xrVoidFunction destroy{};
    PFN_xrVoidFunction endFrame{};
    const bool hasDestroy = load("xrDestroyInstance", &destroy);
    const bool hasEndFrame = load("xrEndFrame", &endFrame);
    g.destroyInstance = reinterpret_cast<PFN_xrDestroyInstance>(destroy);
    g.endFrame = reinterpret_cast<PFN_xrEndFrame>(endFrame);
    if (!hasEndFrame) {
        if (hasDestroy) g.destroyInstance(*instance);
        g = {};
        *instance = XR_NULL_HANDLE;
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    return XR_SUCCESS;
}

}  // namespace

GXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* request
) {
    if (!loaderInfo || !layerName || !request ||
        std::strcmp(layerName, kLayerName) != 0) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = layerGetInstanceProcAddr;
    request->createApiLayerInstance = layerCreateApiLayerInstance;
    return XR_SUCCESS;
}

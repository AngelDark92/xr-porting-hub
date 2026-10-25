#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

// Exercise the actual shipping source. The Android log symbol is stubbed below.
#include "../src/quad_layer.cpp"

int __android_log_write(int, const char*, const char*) { return 0; }

#define CHECK(expression) do { if (!(expression)) { \
    std::cerr << "CHECK failed line " << __LINE__ << ": " #expression "\n"; \
    std::abort(); } } while (false)

namespace {

const XrSession kSession = reinterpret_cast<XrSession>(0xA11);
const XrInstance kInstance = reinterpret_cast<XrInstance>(0xB22);
const XrSpace kSpace = reinterpret_cast<XrSpace>(0xCAFE);
const XrSwapchain kBaseSwapchain = reinterpret_cast<XrSwapchain>(0xB001);
const XrSwapchain kFoveaSwapchain = reinterpret_cast<XrSwapchain>(0xF001);

XrCompositionLayerProjectionView makeView(
    XrSwapchain swapchain,
    float left,
    float right,
    float down,
    float up
) {
    XrCompositionLayerProjectionView view{};
    view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    view.next = nullptr;
    view.pose.orientation.w = 1.0f;
    view.fov = {left, right, up, down};
    view.subImage.swapchain = swapchain;
    view.subImage.imageRect = {{0, 0}, {512, 512}};
    view.subImage.imageArrayIndex = 0;
    return view;
}

XrCompositionLayerProjection makeProjection(
    std::array<XrCompositionLayerProjectionView, 2>& views,
    XrCompositionLayerFlags flags
) {
    XrCompositionLayerProjection projection{};
    projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    projection.next = nullptr;
    projection.layerFlags = flags;
    projection.space = kSpace;
    projection.viewCount = 2;
    projection.views = views.data();
    return projection;
}

const XrFrameEndInfo* lastInput{};
XrFrameEndInfo lastOutput{};
std::array<const XrCompositionLayerBaseHeader*, 4> lastLayerPointers{};
std::array<XrCompositionLayerQuad, 4> lastQuads{};
std::array<XrStructureType, 4> lastTypes{};
XrResult nextResult = XR_SUCCESS;
uint32_t destroyCalls = 0;

XrResult XRAPI_PTR fakeDestroy(XrInstance) {
    ++destroyCalls;
    return XR_SUCCESS;
}

XrResult XRAPI_PTR fakeEndFrame(XrSession, const XrFrameEndInfo* info) {
    lastInput = info;
    lastOutput = *info;
    lastLayerPointers = {};
    lastQuads = {};
    lastTypes = {};
    if (info && info->layers) {
        for (uint32_t i = 0; i < info->layerCount && i < 4; ++i) {
            lastLayerPointers[i] = info->layers[i];
            if (info->layers[i]) {
                lastTypes[i] = info->layers[i]->type;
                if (lastTypes[i] == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                    lastQuads[i] = *reinterpret_cast<const XrCompositionLayerQuad*>(
                        info->layers[i]);
                }
            }
        }
    }
    return nextResult;
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.0005f;
}

const XrCompositionLayerQuad& quad(uint32_t index) {
    CHECK(index < lastOutput.layerCount);
    CHECK(lastTypes[index] == XR_TYPE_COMPOSITION_LAYER_QUAD);
    return lastQuads[index];
}

struct Fixture {
    std::array<XrCompositionLayerProjectionView, 2> baseViews{
        makeView(kBaseSwapchain, -0.6f, 0.6f, -0.4f, 0.4f),
        makeView(kBaseSwapchain, -0.6f, 0.6f, -0.4f, 0.4f),
    };
    std::array<XrCompositionLayerProjectionView, 2> foveaViews{
        makeView(kFoveaSwapchain, -0.3f, 0.3f, -0.2f, 0.2f),
        makeView(kFoveaSwapchain, -0.3f, 0.3f, -0.2f, 0.2f),
    };
    XrCompositionLayerProjection base = makeProjection(baseViews, 0);
    XrCompositionLayerProjection fovea = makeProjection(foveaViews,
        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
        XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT);
    std::array<const XrCompositionLayerBaseHeader*, 2> layers{
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&base),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fovea),
    };
    XrFrameEndInfo info{};
    Fixture() {
        info.type = XR_TYPE_FRAME_END_INFO;
        info.next = nullptr;
        info.displayTime = 123456789;
        info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        info.layerCount = 2;
        info.layers = layers.data();
    }
};

void resetLayerState() {
    g = {};
    conversionDisabled.store(false, std::memory_order_relaxed);
    loggedPassReasons.store(0, std::memory_order_relaxed);
    convertedLogs.store(0, std::memory_order_relaxed);
    lastInput = nullptr;
    lastOutput = {};
    lastLayerPointers = {};
    lastQuads = {};
    lastTypes = {};
    nextResult = XR_SUCCESS;
}

void testConvertedTopology() {
    Fixture fixture;
    resetLayerState();
    g.endFrame = fakeEndFrame;
    const XrResult result = layerEndFrame(kSession, &fixture.info);
    CHECK(result == XR_SUCCESS);
    CHECK(lastInput != nullptr);
    CHECK(lastInput != &fixture.info);

#if GXR_QUAD_ONLY
    CHECK(lastOutput.layerCount == 4);
    const auto& baseLeft = quad(0);
    const auto& baseRight = quad(1);
    const auto& foveaLeft = quad(2);
    const auto& foveaRight = quad(3);
    CHECK(baseLeft.layerFlags == 0);
    CHECK(baseRight.layerFlags == 0);
    CHECK(baseLeft.eyeVisibility == XR_EYE_VISIBILITY_LEFT);
    CHECK(baseRight.eyeVisibility == XR_EYE_VISIBILITY_RIGHT);
    CHECK(baseLeft.subImage.swapchain == kBaseSwapchain);
    CHECK(near(baseLeft.size.width, 2.0f * std::tan(0.6f)));
    CHECK(near(baseLeft.size.height, 2.0f * std::tan(0.4f)));
    CHECK(near(baseLeft.pose.position.z, -1.0f));
    CHECK(foveaLeft.subImage.swapchain == kFoveaSwapchain);
    CHECK(near(foveaLeft.size.width, 2.0f * std::tan(0.3f)));
    CHECK(near(foveaLeft.size.height, 2.0f * std::tan(0.2f)));
#else
    CHECK(lastOutput.layerCount == 3);
    CHECK(lastLayerPointers[0] == fixture.layers[0]);
    const auto& foveaLeft = quad(1);
    const auto& foveaRight = quad(2);
    CHECK(foveaLeft.subImage.swapchain == kFoveaSwapchain);
    CHECK(foveaRight.subImage.swapchain == kFoveaSwapchain);
    CHECK(near(foveaLeft.size.width, 2.0f * std::tan(0.3f)));
    CHECK(near(foveaLeft.size.height, 2.0f * std::tan(0.2f)));
    CHECK(near(foveaLeft.pose.position.z, -1.0f));
#endif
    CHECK(foveaLeft.layerFlags == (XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
        XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT));
    CHECK(foveaLeft.eyeVisibility == XR_EYE_VISIBILITY_LEFT);
    CHECK(foveaRight.eyeVisibility == XR_EYE_VISIBILITY_RIGHT);
    CHECK(foveaLeft.space == kSpace);
    CHECK(foveaRight.space == kSpace);
    CHECK(foveaLeft.subImage.imageRect.extent.width == 512);
    CHECK(foveaLeft.pose.orientation.w > 0.99f);
}

void testAsymmetricFovea() {
    Fixture fixture;
    for (auto& view : fixture.foveaViews) {
        view.fov = {-0.4f, 0.2f, 0.3f, -0.1f};
    }
    resetLayerState();
    g.endFrame = fakeEndFrame;
    const XrResult result = layerEndFrame(kSession, &fixture.info);
    CHECK(result == XR_SUCCESS);
#if GXR_QUAD_ONLY
    const auto& foveaLeft = quad(2);
#else
    const auto& foveaLeft = quad(1);
#endif
    const float centerX = 0.5f * (std::tan(-0.4f) + std::tan(0.2f));
    const float centerY = 0.5f * (std::tan(-0.1f) + std::tan(0.3f));
    CHECK(near(foveaLeft.pose.position.x, centerX));
    CHECK(near(foveaLeft.pose.position.y, centerY));
    CHECK(near(foveaLeft.pose.position.z, -1.0f));
    CHECK(near(foveaLeft.size.width, std::tan(0.2f) - std::tan(-0.4f)));
    CHECK(near(foveaLeft.size.height, std::tan(0.3f) - std::tan(-0.1f)));
}

void expectPassThrough(const Fixture& fixture) {
    lastInput = nullptr;
    const XrResult result = layerEndFrame(kSession, &fixture.info);
    CHECK(result == XR_SUCCESS);
    CHECK(lastInput == &fixture.info);
    CHECK(lastOutput.layerCount == fixture.info.layerCount);
    CHECK(lastOutput.layers == fixture.info.layers);
}

void testPassThrough() {
    resetLayerState();
    g.endFrame = fakeEndFrame;
    {
        Fixture fixture;
        fixture.base.next = reinterpret_cast<const void*>(0x1);
        expectPassThrough(fixture);
    }
    {
        Fixture fixture;
        fixture.fovea.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        expectPassThrough(fixture);
    }
    {
        Fixture fixture;
        fixture.fovea.space = reinterpret_cast<XrSpace>(0x1);
        expectPassThrough(fixture);
    }
    {
        Fixture fixture;
        fixture.foveaViews[0].fov = {-1.6f, 0.2f, 0.3f, -0.1f};
        expectPassThrough(fixture);
    }
    {
        Fixture fixture;
        fixture.baseViews[0].subImage.swapchain = XR_NULL_HANDLE;
        expectPassThrough(fixture);
    }
    {
        Fixture fixture;
        fixture.info.layerCount = 1;
        expectPassThrough(fixture);
    }
    {
        Fixture fixture;
        fixture.base.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        expectPassThrough(fixture);
    }
    {
        Fixture fixture;
        fixture.fovea.viewCount = 1;
        expectPassThrough(fixture);
    }
}

void testDispatchLifecycle() {
    resetLayerState();
    PFN_xrVoidFunction function = reinterpret_cast<PFN_xrVoidFunction>(0x1);
    CHECK(layerGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", &function) ==
        XR_ERROR_FUNCTION_UNSUPPORTED);
    CHECK(function == nullptr);
    CHECK(layerGetInstanceProcAddr(XR_NULL_HANDLE, "xrEndFrame", &function) == XR_SUCCESS);
    CHECK(function == reinterpret_cast<PFN_xrVoidFunction>(layerEndFrame));
    CHECK(layerGetInstanceProcAddr(XR_NULL_HANDLE, "xrDestroyInstance", &function) ==
        XR_SUCCESS);
    CHECK(function == reinterpret_cast<PFN_xrVoidFunction>(layerDestroyInstance));

    g.instance = kInstance;
    g.destroyInstance = fakeDestroy;
    destroyCalls = 0;
    CHECK(layerDestroyInstance(kInstance) == XR_SUCCESS);
    CHECK(destroyCalls == 1);
    CHECK(g.instance == XR_NULL_HANDLE);
    CHECK(g.destroyInstance == nullptr);
}

void testRejectionDisablesConversion() {
    Fixture fixture;
    resetLayerState();
    g.endFrame = fakeEndFrame;
    nextResult = XR_ERROR_RUNTIME_FAILURE;
    CHECK(layerEndFrame(kSession, &fixture.info) == XR_ERROR_RUNTIME_FAILURE);
    CHECK(lastInput != &fixture.info);
#if GXR_QUAD_ONLY
    CHECK(lastOutput.layerCount == 4);
#else
    CHECK(lastOutput.layerCount == 3);
#endif
    // The next frame returns to the application's original submission; the
    // rejected frame is never resubmitted by the layer.
    nextResult = XR_SUCCESS;
    lastInput = nullptr;
    CHECK(layerEndFrame(kSession, &fixture.info) == XR_SUCCESS);
    CHECK(lastInput == &fixture.info);
    CHECK(lastOutput.layerCount == 2);
}

}  // namespace

int main() {
    testConvertedTopology();
    testAsymmetricFovea();
    testPassThrough();
    testDispatchLifecycle();
    testRejectionDisablesConversion();
    std::cout << "quad layer integration: "
              << (GXR_QUAD_ONLY ? "quad-only" : "fovea-quad") << " PASS\n";
    return 0;
}

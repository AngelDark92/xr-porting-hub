# Quad-layer composition experiments (2026-10-25)

Source of `libgxr_fovea_quad.so` and `libgxr_quad_only.so`, installed by two
default-off, experimental Morphe patches for exact Steam Link
**2.0.20/5001712** only:

| Patch | Output topology | Layer count |
|---|---|---:|
| **Fovea quad layers (experimental)** | Valve background projection + two per-eye fovea quads | 3 |
| **Quad-only composition (experimental)** | Two per-eye background quads + two per-eye fovea quads | 4 |

Both arms reuse Valve's existing swapchains and `subImage` regions. Nothing is
copied, upscaled or resampled, and no DEX or native renderer byte is changed. The
only mutation is an implicit OpenXR API layer (`libgxr_*.so` plus its manifest
under `assets/openxr/1/api_layers/implicit.d`).

## Why quads

The timkhronos recommendation submits `fovea quads` and `periphery quads`
instead of projection layers. Steam Link 2.0.20 submits two projection layers
from `QSVLRendererXR::GetProjectionLayers` (`0x10a410` on the exact 5001712
scene): projection 1 is the opaque background, projection 2 is the alpha-masked
fovea with flags `BLEND | UNPREMULTIPLIED` (`6`). These arms test whether the
Galaxy XR compositor prefers that quad representation for Steam Link's existing
images.

The retired Steam Link fovea-quad experiment used kilometre-scale quads. These
arms do not. Each quad is a one-metre plane whose angular size is derived from the
source view's tangent-space FOV, so it covers exactly the same view frustum.

## Conversion rules

The layer converts only this frame shape:

- exactly two projection layers, each with two views;
- both projections use the same `XrSpace`;
- the second projection has fovea flags `BLEND | UNPREMULTIPLIED`;
- no `next` chain on either projection or view, finite poses and FOVs.

Everything else passes through byte-for-byte. For arm 1 the first (background)
projection pointer is preserved unchanged and only the second projection is
replaced. For arm 2 both projections become two per-eye quads.

Quad geometry per source view:

```
d        = 1.0 m
tan*     = tan(view.fov.angle*)
centerX  = 0.5 * (tanLeft + tanRight)
centerY  = 0.5 * (tanDown + tanUp)
size     = ((tanRight - tanLeft) * d, (tanUp - tanDown) * d)
pose     = view.pose.position + normalize(view.pose.orientation) * (centerX*d, centerY*d, -d)
rotation = normalize(view.pose.orientation)
```

`subImage`, `space` and layer flags are copied from the source projection. The
fovea quads keep flags `6`; each quad uses `XR_EYE_VISIBILITY_LEFT` or
`XR_EYE_VISIBILITY_RIGHT`.

## Fail-safe behavior

- A shape mismatch (layer count, type, `next` chain, space, flags, FOV) passes the
  original frame through unchanged and logs one reason.
- A runtime rejection returns the runtime's error for that frame, disables
  conversion for the rest of the instance, and passes future frames through
  unchanged. The rejected frame is never resubmitted.
- `xrDestroyInstance` clears the dispatch table.
- `xrGetInstanceProcAddr` before `createApiLayerInstance` returns
  `XR_ERROR_FUNCTION_UNSUPPORTED` with a null output instead of calling a null
  dispatch pointer.

Logcat tag: `GXRQuadLayer`. Each instance logs the first three converted frames,
the first occurrence of each pass-through reason, and any rejection. One JSON line
records `mode`, `buildId`, `arm`, `result`, source/output layer counts and the
base/fovea flags. Useful results:

- `converted` - the quad frame reached the runtime;
- `end_frame_rejected` - the runtime rejected the quad frame;
- `pass_through_layer_count`, `pass_through_projection_shape`,
  `pass_through_space_mismatch`, `pass_through_fovea_flags`,
  `pass_through_base_geometry`, `pass_through_fovea_geometry` - the arm did not
  engage, with the reason named.

## Build

```powershell
$cmake = "..\builds\steamlink-patches\build\tooling\cmake\data\bin\cmake.exe"
$out = "..\builds\steamlink-patches\extensions\quad-layer\build-android"
& $cmake -S extensions\quad-layer -B $out -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=.android-sdk\ndk\27.2.12479018\build\cmake\android.toolchain.cmake" `
  "-DOPENXR_SDK_SOURCE_DIR=..\builds\steamlink-patches\extensions\controller-velocity-layer\build-android\_deps\openxr_headers-src" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static `
  -DCMAKE_BUILD_TYPE=Release
& $cmake --build $out --config Release
```

Host integration tests compile the shipping source for both arms:

```powershell
zig c++ -std=c++17 -DGXR_QUAD_ONLY=0 -I <stubs> -I <openxr>/include `
  extensions/quad-layer/tests/quad_layer_tests.cpp -o fovea.exe
zig c++ -std=c++17 -DGXR_QUAD_ONLY=1 -I <stubs> -I <openxr>/include `
  extensions/quad-layer/tests/quad_layer_tests.cpp -o quad-only.exe
```

## Selection and rollback

Select exactly one arm in the Morphe experimental catalog and **deselect
Alpha-0 high-resolution trigger**; the installers reject both directions. The two
arms are also mutually exclusive. Roll back by repatching with the arm
deselected. A patched APK must be rebuilt from a clean source APK if the
installer reports a modified or stale helper.

Headset validation is pending. This layer changes terminal composition only; it
does not create detail, change decoder output, or alter swapchain formats.

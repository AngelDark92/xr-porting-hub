# Steam Link quad-layer composition experiments (2026-10-25)

Two default-off experimental arms test the timkhronos-recommended quad
composition on the exact Steam Link **2.0.20/5001712** base. Neither arm creates
an APK in this repository; the deliverable is the Morphe `.mpp`, and the owner
patches the APK.

## Arms

| Arm | Patch | Frame submitted |
|---|---|---|
| 1 | **Fovea quad layers (experimental)** | Valve background projection + two per-eye fovea quads |
| 2 | **Quad-only composition (experimental)** | Two per-eye background quads + two per-eye fovea quads |

Both copy `subImage`, `XrSpace` and layer flags from the source projection views.
No texture is copied, resampled, upscaled or reconstructed. The two arms are
mutually exclusive and both require **Alpha-0 high-resolution trigger** to be
deselected; either install order fails closed.

## Rationale

- The timkhronos recommendation is to submit the fovea and periphery as per-eye
  quads rather than projection layers. The recommendation describes this as the
  fast composition path, with flags `2`/`6` for the quad layers.
- Steam Link 2.0.20 `QSVLRendererXR::GetProjectionLayers` is at `0x10a410` in
  `lib/arm64-v8a/libvrlink_scene.so` and appends exactly two projection layer
  pointers (`this+0x1998`, `this+0x19c8`).
- The existing 2.0.20 foveal-canvas record identifies projection 1 as the opaque
  background and projection 2 as the masked fovea with alpha flags `6`
  (`BLEND | UNPREMULTIPLIED`) on the same legacy layout family. The layer
  validates this shape and passes every other shape through unchanged.

The recommended quads are not kilometre-scale. Each arm uses a one-metre plane
whose size comes from the source view's tangent extents, so angular coverage
matches the original projection view exactly. The retired Steam Link
kilometre-scale fovea-quad experiment is not reused.

## CPU/GPU expectation

- **CPU:** arm 1 is slightly cheaper in the API layer: two quad fits and three
  submitted layers versus four quad fits and four layers. Both are negligible
  next to Steam Link's render, encode/decode and transport work.
- **GPU/compositor:** arm 1 keeps the full-view background on Valve's projection
  path and adds only two fovea quads; arm 2 also moves the full-view background
  to quad sampling. For a generic compositor arm 1 should be lighter, because it
  submits one fewer layer and less quad-sampled area. The timkhronos
  recommendation describes the quad-only topology as a fast path, so Galaxy XR
  may instead have a better-optimized all-quad path; only on-device profiling can
  settle this.

Neither arm adds application render work, so any difference is in runtime
composition, not in Steam Link's GPU pipeline.

## Artifacts

| File | Size | SHA-256 |
|---|---:|---|
| `patches/src/main/resources/steamlink/androidxr/libgxr_fovea_quad.so` | 339,256 | `5004336d2dfd1efcbde0274bff32400b32e43954dac7c1a01785c6ebc4437253` |
| `patches/src/main/resources/steamlink/androidxr/libgxr_quad_only.so` | 339,480 | `39078df8c18597e4188f2991d4a95dcede36ca53b6f135f0fab3d170709d15ea` |
| `XR_APILAYER_local_GalaxyXR_fovea_quad_v1.json` | 372 | `a38244e3e793e7856e022d838c0065f2681c743e9dc3eae263bb8869d66c904f` |
| `XR_APILAYER_local_GalaxyXR_quad_only_v1.json` | 344 | `d0420eea19794b7179cef49934b152695ec8deb0ed72474f6e22544f86aefec2` |

Build IDs: `gxr-fovea-quad-v1.0-20261025` and
`gxr-quad-only-v1.0-20261025`.

Both helpers are built from `extensions/quad-layer/src/quad_layer.cpp` with NDK
`27.2.12479018`, arm64-v8a, API 29, `c++_static`, CMake 4.4.3/Ninja, then
stripped. `extensions/quad-layer/README.md` has the commands and the conversion
rules.

## Evidence classes

| Class | Status |
|---|---|
| Source/static inspection | Complete: Steam Link 2-projection topology; timkhronos recommendation recorded as the design input |
| Host integration tests | PASS: both arms, conversion, asymmetric FOV, pass-through, GIPA lifecycle, rejection fallback |
| Kotlin/unit tests | PASS: `:patches:test` 219 executed, 16 skipped (missing decoded fixtures), 0 failed |
| Compilation/packaging | PASS: both `.so`, both manifests and patch classes present in `patches-2.2.1-dev.2.mpp` |
| CI | Not run for this change |
| Headset/runtime | **Pending.** No frame has been submitted to the Galaxy XR runtime yet. |

Runtime gaps to close: whether the runtime accepts per-eye quad layers over
Valve's projection swapchains; whether the mixed projection+quad path or the
all-quad path is faster; actual frame timing/thermal behavior; visual alignment
of the fovea overlay; and behavior across pause/resume and DFR transitions.

## Runtime check

```powershell
adb logcat -c
adb logcat -s GXRQuadLayer
```

Expected first line after a successful conversion:

```json
{"mode":"fovea_quad_layers_v1","buildId":"gxr-fovea-quad-v1.0-20261025","arm":"fovea_quad","result":"converted",...}
```

or the matching `quad_only_composition_v1` / `quad_only` line. Each arm logs the
first three converted frames and the first occurrence of each pass-through
reason. A `pass_through_*` result names the rejected shape;
`end_frame_rejected` means the runtime refused the converted frame and later
frames pass through unchanged.

## Selection

Use the Morphe experimental catalog. Select exactly one arm, leave **Alpha-0
high-resolution trigger** off, and use a clean source APK. Roll back by
repatching without the arm. Do not stack the two arms or Alpha-0.

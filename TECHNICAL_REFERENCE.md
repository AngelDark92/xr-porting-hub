# Technical reference — XR Porting Hub

Implementation notes, compatibility details, validation status, and build instructions for the Galaxy XR app patch set (Steam Link VR and 4XVR). For installation and patch selection, see the [README](README.md). The tables below cover both app patch sets; the 4XVR set is recommended by default — see its [plan](diagnostics/4xvr-galaxyxr/2026-10-11-4xvr-galaxyxr-diff-and-plan.md) and the README status and credits section.

## Steam Link 2.0.20 / 5001812 and 2.0.21 / 5001968

Both exact bases expose the same 17-patch recommended set and 21 standalone
patches; 2.0.20/5001712 exposes 22 — it alone adds **Codecs Support**, which is
recommended there and exact to that build. Native addresses, complete-function guards and shader
callers were derived independently for each build. Both use the verified
2-projection helper, mouse-only SDL routing and array-shaped HMD extension
configuration. The 2 blue-noise experiments remain default-off and outside the
bundle. See the [adaptation and validation record](diagnostics/steamlink-legacy-1812-1968/README.md).

## Steam Link 2.0.23 / 5002363

The new exact base exposes the 7 modern individual patches with independently verified native addresses and stock Java startup. Its 6-patch recommended set is default-enabled, and Morphe Manager auto-selects it for this exact build; **Device identity (2.0.23/5002363)** remains an optional one-step selection. The 14 legacy patches stay excluded. See the [5002363 audit](diagnostics/steamlink-5002363/README.md) for source APK provenance, exact addresses, mutation/option tests, APK patching and remaining runtime gaps.

## Compatibility and implementation notes

The Android-Surface Fovea experiment was **tried and retired on 2026-09-07** after
the user reported it did not work. It copied actual fovea pixels into a Surface,
kept 3 projections and omitted the dummy quad. Its failure cause is unknown; the
working terminal-quad fix is unchanged. See the [retirement record](diagnostics/steamlink-surface-fovea/README.md).

Steam Link VR (`com.valvesoftware.steamlinkvr`) was not built for Android XR. These patches adapt it to run on the Samsung Galaxy XR headset by injecting the missing OpenXR permissions and features, bundling the Galaxy XR XR-bridge native library, providing an optional standalone face-bridge layer for face-tracking, fixing broken permission flows, tuning the rendering pipeline, and optionally allowing the patched APK to coexist with the original install.

Target APK: `com.valvesoftware.steamlinkvr`. Exact compatibility metadata and guarded adaptations include v2.0.20/5001712, v2.0.20/5001812, v2.0.21/5001968, v2.0.22/5002244, and v2.0.23/5002363. The permission-free high-resolution fix accepts exactly those 5 builds; 5001712 and 5002244 have verified layouts, while 5002363 is a decoded-base adaptation with pending headset validation. Reconstruction, quad-view, permission-matrix, warm-up/omit, and DFR re-arm experiments are retired.

Use Morphe Manager 1.30.0 or newer with compatibility checks enabled for build-specific filtering. Manager 1.7 cannot distinguish APKs that share versionName `2.0.22`, and Expert mode may intentionally show incompatible patches. Morphe's `default` flag is one global value per patch, so the recommended defaults live on the individual patches: all 4 legacy bases (2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968 and 2.0.22/5002244) default-enable the [legacy recommended set](PATCH_CATALOG.md#recommended-set-per-exact-build) — 17 patches on 5001812, 5001968 and 5002244, and 18 on 5001712, which additionally recommends **Codecs Support** — including the 3 native force-gate patches, required XR foundation patches, Device identity with the Meta Quest Pro spoof, and both explicit older startup patches, and Morphe Manager auto-selects it for the detected build. 2.0.23/5002363 default-enables only GXR tongue bridge (native face tracking), Galaxy XR high-resolution fix, Microphone input preset (Voice Recognition), OLED color calibration (`final-balanced`), Unrestricted battery usage, Network Optimizations, and Visual Delay Fix (60 ms). The full GXR face bridge is restricted to the 4 legacy builds so it cannot replace Valve's native 5002363 face mappings. **Appear on top (legacy)** and **Change package name** are never recommended.

**Startup permission requests (before 5002322)** and **Startup splash and XR launch mode (before 5002322)** are members of the legacy recommended set, default-enabled on all 4 legacy bases and independently deselectable. The permission patch declares missing hand, eye, face, microphone and Bluetooth permissions and enables their startup requests. The splash patch enables the black startup screen and unmanaged VRLink start-mode override; older legacy builds also receive panel sizing. The shared helper defaults both behaviors off. Earlier battery/overlay settings flows use a transparent bootstrap unless those explicit patches are selected.

For exact 2.0.23/5002363, the recommended set preserves stock launcher, splash, XR start mode and runtime permission handling. The battery patch alone inserts a battery-settings helper call after stock `SteamLink.onCreate` calls its superclass, before parameter registers are reused. Face/tongue bridges and high resolution no longer pull in startup permission requests or splash changes; high resolution no longer forces unmanaged Full Space.

For the legacy bundles, **2.0.20/5001712**, **2.0.20/5001812**, **2.0.21/5001968**, and **2.0.22/5002244**, leave **HMD identity** on **Recommended for this build** or explicitly choose **Meta Quest Pro**. The spoof reports `Oculus Quest Pro` while retaining Galaxy XR tracking/controller routing. Existing saved explicit Samsung, Stock, or Pico choices are respected, so change those if necessary. On 2.0.23/5002363, **Device identity (2.0.23/5002363)** remains optional, is not part of the recommended set, and resolves to the Galaxy XR identity.

The recommended selection does not broaden native compatibility: the high-resolution edit safely skips instead of guessing a native layout, and unsupported builds remain unchanged. All 4 exact legacy targets have the complete legacy set available.

**The standalone Video dither patch remains retired.** The existing OLED patch now offers optional Comparison dithering (Off, Low, Standard), a Neutral calibration profile, and sRGB8/RGB10/experimental FP16 output. Defaults are Final balanced, 8-bit sRGB, and dithering Off. RGB10 and FP16 remain optional. See the [controlled comparison guide](PATCH_CATALOG.md#controlled-oled-comparison).

### Retired underside experiment — 2026-09-03

The user reported that the 2.0.22/5002322 Surface-backed underside experiment
"doesn't work" and requested retirement. Its option, implementation, build target
and bundled resources are removed. No new capture was reviewed, so the cause and
GPU effect remain undetermined. The tested terminal quad is the only active mode;
both existing helper binaries, including 2.0.20/5001712, remain unchanged.

### Terminal-quad helper CPU revision — 2026-09-03

The shared resolution patch now supplies 3-projection helper `v1.4-20260903` and 5001712 2-projection helper `v1.2-20260903`. Its generation-validated, non-owning render-thread cache removes steady-frame shared-ownership reference-count operations while the registry retains session ownership. It relies on OpenXR's externally synchronized destruction; lifecycle changes invalidate cached handles. Event readers still acquire ownership, and `xrPollEvent` dispatch processes data only for `XR_SUCCESS`, never `XR_EVENT_UNAVAILABLE`. Existing recommended selections automatically receive these helpers through their unchanged resolution-patch dependency.

Host CTest registry and actual 2-/3-projection helper integration checks passed, as did both Android rebuilds using NDK `27.2.12479018` and cached OpenXR `1.1.43` headers. These revisions have no headset result or measured speed gain yet. All 6 exact supported version/build pairs, continuous 2x2 quad submission, source formats, and GPU work remain unchanged; this does not enable 10-bit output. Compare against the previous artifact with the same base, scene, options, and host settings, checking cold launch, stop/restart, palm/DFR transitions, and loss/resume. See the [patch catalog](PATCH_CATALOG.md#galaxy-xr-high-resolution-3-projection-fix-xrgalaxyxrhighresolutionpatch) for validation boundaries and A/B guidance.


## Additional setup context

The previous README described 2.0.20/5001712 as more stable according to community reports and 2.0.22/5002244 as newer but less stable. This is historical community reporting, not a controlled comparison or a guarantee for a particular setup. Both builds come from the Meta store route in the [installation instructions](README.md#1-get-the-app-apk-meta-store).

No desktop IP, pairing token, APK hash, or native telemetry enrollment is required to apply the recommended patches.

## Full patch list

This section is generated from the patch catalog during releases.

<!-- PATCHES_START EXPANDED -->
> **[v1.29.0-dev.2](https://github.com/AngelDark92/xr-porting-hub/releases/tag/v1.29.0-dev.2)**&nbsp;&nbsp;•&nbsp;&nbsp;`dev`&nbsp;&nbsp;•&nbsp;&nbsp;49 patches total
<details open>
<summary>📦 4XVR&nbsp;&nbsp;•&nbsp;&nbsp;16 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.22 (20022) |
| :---: |
| 4XVR Galaxy XR adaptation for exact build 20022; decoded-base design, headset validation pending. |

#### Image quality

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [HDR pipeline payload (4XVR)](#hdr-pipeline-payload-4xvr) | Installs the modified libplacebo 7.360.1 (LGPL-2.1-or-later), the 4XVR HDR bridge native and the bundled open-source notices for exact 4XVR 2.0.22 build 20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [libplacebo HDR pipeline (4XVR)](#libplacebo-hdr-pipeline-4xvr) | Loads the HDR bridge, routes the decoder output surface through it for the FP16 hand-off, detects HLG from the video MediaFormat and keeps the bridge informed of movie/seek transitions for exact 4XVR 2.0.22 build 20022. Headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Tracking & audio

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [5.1-channel audio bridge (4XVR)](#5-1-channel-audio-bridge-4xvr) | Routes the AAudio calls of Vr4pMediaPlayer and its audio runnable through the bundled 5.1-channel bridge, initializes the bridge in Main4XActivity and refreshes the subtitle style on the playback-speed tick for exact 4XVR 2.0.22 build 20022. Headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Audio bridge payload (4XVR)](#audio-bridge-payload-4xvr) | Installs the bundled 5.1-channel AAudio bridge native for exact 4XVR 2.0.22 build 20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Galaxy XR controller models (4XVR)](#galaxy-xr-controller-models-4xvr) | Replaces the four Quest 3 controller assets with the Galaxy XR reference models (2048x2048 textures, re-authored meshes) for exact 4XVR 2.0.22 build 20022. Static asset swap; headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Startup & permissions

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Galaxy XR launch compatibility (4XVR)](#galaxy-xr-launch-compatibility-4xvr) | Installs the Khronos OpenXR loader 1.1.58, the 4XVR view-config API layer and the Galaxy XR manifest declarations (OpenXR 1.1, passthrough, hand and eye input) for exact 4XVR 2.0.22 build 20022. Decoded-base adaptation; headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Galaxy XR launcher icon (4XVR)](#galaxy-xr-launcher-icon-4xvr) | Re-exposes the stock 4XVR launcher artwork under the canonical ic_launcher/ic_launcher_round names (adaptive icon plus density PNGs) and points the manifest at them, matching the Steam Link icon shape for exact 4XVR 2.0.22 build 20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Popup refresh on resume (4XVR)](#popup-refresh-on-resume-4xvr) | Re-checks the 4XVR startup popups after the activity resumes or returns from system settings: hides the All-files-access prompt once external storage access is granted (the stock flow never re-checks the result of its 0x59E2 request) and hides the release-notes window when its saved version already covers the installed build, for exact 4XVR 2.0.22 build 20022. Headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Startup and UI continuity helper (4XVR)](#startup-and-ui-continuity-helper-4xvr) | Registers the bundled GxrToastContinuity helper, whose I610UEU2AZF3-gated memory-buildup fix and toast continuity keep the Galaxy XR UI pipeline warm across activity recreation, right after Main4XActivity.onCreate for exact 4XVR 2.0.22 build 20022. Headless validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Advanced XR compatibility

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Frame-synced render dispatch (4XVR)](#frame-synced-render-dispatch-4xvr) | Drains the render dispatch queue before every frame, hands scene switches to RenderDispatch.requestScene instead of the retired render runnable, and routes window teardown through the dispatch for exact 4XVR 2.0.22 build 20022. Headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Galaxy XR refresh profile (72 Hz, 4XVR)](#galaxy-xr-refresh-profile-72-hz-4xvr) | Clamps the InitializeDevice refresh-rate picker floors (121.0f/91.0f -> 73.0f) so exact 4XVR 2.0.22 build 20022 requests Galaxy XR's 72 Hz mode. Three guarded bytes in libvr4p-oculus.so; runtime application pending headset validation. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Meta platform decoupling (4XVR)](#meta-platform-decoupling-4xvr) | Stubs the 11 exported Oculus platform-init/entitlement/message functions in libovrplatformloader.so of exact 4XVR 2.0.22 build 20022. The reference Galaxy XR build requires the Meta platform channel to stay inert on a non-Meta runtime: without these stubs the app hangs on the splash — the OpenXR session is created but never presents its first frame (Galaxy XR on-device A/B, 2026-10-12). Meta-store VIP flows degrade. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Render dispatch payload (4XVR)](#render-dispatch-payload-4xvr) | Installs the 4XVR render dispatch native (frame-synced scene queue and adaptive theater background updates) for exact 4XVR 2.0.22 build 20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Premium & purchases

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Premium unlock (4XVR, experimental)](#premium-unlock-4xvr-experimental) | Removes the 4XVR premium gate on Galaxy XR, where the Meta Store cannot be reached to verify a purchase: every start forces the app's own VIP state on through ChangeCurVIPStateClear(true), the same state transition a store purchase applies, so IsMP() is true and the trial playback limiter stays off. The dead Meta-store Buy entry points re-assert the unlocked state instead of calling the native checkout; the Buy buttons are hidden in favor of the Sync slot. Exact 4XVR 2.0.22 build 20022. Headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Subtitles & playback

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Subtitle suite (4XVR)](#subtitle-suite-4xvr) | Adds the subtitle position controls row, the enlarged subtitle panel height, immediate config persistence on color-tab changes, the custom subtitle font load and the wrapped-cue size clamp for exact 4XVR 2.0.22 build 20022. Headset validation pending. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Subtitle suite payload (4XVR)](#subtitle-suite-payload-4xvr) | Installs the subtitle position native library used by the 4XVR subtitle suite controls on exact 4XVR 2.0.22 build 20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

</details>

<details open>
<summary>📦 Steam Link&nbsp;&nbsp;•&nbsp;&nbsp;23 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) | 2.0.23 (5002363) | 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Verified Steam Link 2.0.20 build 5001712. | Native and APK adaptation for exact Steam Link 2.0.20 build 5001812; headset validation pending. | Native and APK adaptation for exact Steam Link 2.0.21 build 5001968; headset validation pending. | Verified Steam Link 2.0.22 build 5002244. | Build 5002363 recommends Galaxy XR high-resolution 3-projection fix, GXR face and tongue bridge, Microphone input preset (Voice Recognition), Unrestricted battery usage, Visual Delay Fix (60 ms), OLED color calibration with the Final balanced tested profile, Blue-noise dithering with the default foveal layer, and Network Optimizations. The retired projection experiments are excluded. Decoded-base adaptation; headset validation pending. | Native and APK adaptation for exact Steam Link 2.0.23 build 5002363; headset validation pending. | Exact Steam Link 2.0.20/5001712 high-resolution target with its isolated 2-projection to 3-layer payload. The topology correction has prior user-reported startup and delayed-frame runtime evidence; this rebuilt binary remains uninstalled. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.20 build 5001812; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.21 build 5001968; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.22 build 5002244; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.23 build 5002363; headset validation pending. |

#### Image quality

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Blue-noise dithering](#blue-noise-dithering) | Adds static blue noise after video colour processing and fade, independently selectable on the foveal and the background/base layer; 8-bit or 10-bit input, always 8-bit sRGB output. Recommended on all exact bases with the foveal layer on by default. Turn OLED's VD-like fovea toggle off when foveal blue noise is used. Unknown host shaders pass through unchanged. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Foveal layer<br>• Background layer |
| [Galaxy XR high-resolution 3-projection fix](#galaxy-xr-high-resolution-3-projection-fix) | Permission-free high-resolution fix: appends a static 2x2 Android-surface compositor trigger and leaves each build's native projection layers and formats, including future RGB10_A2, untouched. No image copy. Exact builds 5001712, 5001812, 5001968, 5002244, 5002363. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [OLED color calibration](#oled-color-calibration) | Applies OLED gamma/saturation calibration with 8-bit sRGB output, plus optional VD-like SDR foveal processing and a separate foveal gamma. Exact builds 5001712, 5001812, 5001968, 5002244, 5002363; visible improvement needs headset verification. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Calibration profile<br>• Gamma<br>• Saturation<br>• Fovea VD-like SDR processing<br>• Foveal gamma adjustment |

#### Tracking & audio

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [GXR face and tongue bridge](#gxr-face-and-tongue-bridge) | Installs the Galaxy XR face transport for the VRCFT module, per exact build: the face-tracking bridge layer (XR_FB_face_tracking2 to XR_ANDROID_face_tracking) and the FACE_TRACKING permission on the legacy targets 5001712/5001812/5001968/5002244; the tongue out/left/right/up/down transport slots (keeping Valve's native face expressions and standard TongueOut) on 2.0.23/5002363. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [Microphone input preset](#microphone-input-preset) | Selects Steam Link's AAudio microphone processing mode. On Galaxy XR, Voice Recognition was clearer and louder than stock Voice Communication. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Microphone mode |
| [Visual Delay Fix](#visual-delay-fix) | Offsets the HMD OpenXR pose-query time and zeroes the exported HMD velocity fields, so head tracking leads the rendered frame. Controllers are unaffected. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Pose offset (ms) |
| [XR Input Routing Config](#xr-input-routing-config) | Installs ui_config.json so XR pointer and button input reaches the launcher UI. | 5001712, 5001812, 5001968, 5002244 |  |

#### Startup & permissions

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Appear on top (legacy)](#appear-on-top-legacy) | Legacy overlay fallback for older builds: adds SYSTEM_ALERT_WINDOW and the compositor signal window. | 5001712, 5001812, 5001968, 5002244 |  |
| [Startup permission requests (before 5002322)](#startup-permission-requests-before-5002322) | Requests hand, eye, face, microphone and Bluetooth permissions at startup on older builds. Battery settings and the splash are separate patches. | 5001712, 5001812, 5001968, 5002244 |  |
| [Startup splash and XR launch mode (before 5002322)](#startup-splash-and-xr-launch-mode-before-5002322) | Adds the Launching Steam Link splash and unmanaged VRLink startup on older builds. Requests no permissions; pick Startup permission requests for those. | 5001712, 5001812, 5001968, 5002244 |  |
| [Unrestricted battery usage](#unrestricted-battery-usage) | Opens Android's Battery usage page at startup so Unrestricted can be selected for XR streaming. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |

#### App & device identity

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Change package name](#change-package-name) | Renames the package and Steam Link's internal VR-launch component, so the patched app can be installed next to the original. | 5001712, 5001812, 5001968, 5002244 | • Package name |
| [Device identity](#device-identity) | Overrides the HMD identity reported to SteamVR. Recommended: Meta Quest Pro on the legacy targets through 5002244. Explicit profiles override that. | 5001712, 5001812, 5001968, 5002244 | • HMD identity |
| [Device identity (2.0.23/5002363)](#device-identity-2-0-23-5002363) | Overrides the HMD identity reported to SteamVR for exact 2.0.23/5002363. Optional; not part of the recommended set. Explicit profiles override that. | 5002363 | • HMD identity |
| [Galaxy XR SteamVR driver (galaxyxr)](#galaxy-xr-steamvr-driver-galaxyxr) | Reports the headset and controllers to SteamVR through the resource-only `galaxyxr` driver installed by Galaxy XR VRLink Setup on the PC: Galaxy XR identity, {galaxyxr} icons, input profiles and controller models, and the calibrated grip offset. Replaces the Meta Quest Pro spoof; the PC driver must be installed or the controllers show no model. Controllers keep the oculus_touch type because vrlink accepts no other. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Grip offset position (cm, x,y,z)<br>• Grip offset rotation (deg, x,y,z) |

#### Advanced XR compatibility

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Android XR native permission names](#android-xr-native-permission-names) | Rewrites native Oculus face/eye permission checks to the Android XR names, on exact 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968 and 2.0.22/5002244 builds. | 5001712, 5001812, 5001968, 5002244 |  |
| [Force HMD initialization gates](#force-hmd-initialization-gates) | Bypasses the 2 capability gates in QSVLDeviceHmd::Init, on exact 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968 and 2.0.22/5002244 builds. | 5001712, 5001812, 5001968, 5002244 |  |
| [Force lobby permission-state gate](#force-lobby-permission-state-gate) | Bypasses the permission-state gate in XrSceneLobby, on exact 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968 and 2.0.22/5002244 builds. | 5001712, 5001812, 5001968, 5002244 |  |
| [Force stream XR gates](#force-stream-xr-gates) | Bypasses the 3 XR gates in the stream path, on exact 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968 and 2.0.22/5002244 builds. | 5001712, 5001812, 5001968, 5002244 |  |
| [XR Core Runtime](#xr-core-runtime) | Installs the XR runtime bridge resources and extension DEX that the other XR patches need. | 5001712, 5001812, 5001968, 5002244 |  |
| [XR Device Config Baseline](#xr-device-config-baseline) | Installs baseline Galaxy XR HMD, controller and default config payloads plus the dashboard bootstrap assets. | 5001712, 5001812, 5001968, 5002244 |  |
| [XR Manifest Capability Pack](#xr-manifest-capability-pack) | Adds the Android XR/OpenXR permissions, features and app properties Steam Link needs. | 5001712, 5001812, 5001968, 5002244 |  |

#### Streaming & network

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Network Optimizations](#network-optimizations) | Improves Steam Link 2.0.23's streaming transport: a loss burst large enough to overflow the retransmission-request buffer now re-requests the accumulated missing packets instead of aborting the stream, the frame-reorder (backtrack) window widens from 6 to 12 frames, a momentarily-starving decoder keeps its buffered frames instead of flushing them, and the UDP send buffer for retransmission requests grows from 128 KiB to 1 MiB. No bitrate, resolution or quality setting is changed. Exact 2.0.23/5002363 only; headset validation pending. | 5002363 |  |

</details>

<details open>
<summary>📦 Steam Link Experimental&nbsp;&nbsp;•&nbsp;&nbsp;9 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.20 (5001712) | 2.0.23 (5002363) | 2.0.23 (5002363) | 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) | 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) | 2.0.23 (5002363) | 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.20 (5001812) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Verified Steam Link 2.0.20 build 5001712. | Native and APK adaptation for exact Steam Link 2.0.23 build 5002363; headset validation pending. | Angular adapter for corrected base-space velocities; requires active runtime correction. | Experimental patches for Steam Link 2.0.20 build 5001712. | Experimental patches for Steam Link 2.0.20 build 5001812. | Experimental patches for Steam Link 2.0.21 build 5001968. | Experimental patches for Steam Link 2.0.22 build 5002244. | Experimental patches for Steam Link 2.0.23 build 5002363. | Controller pose extrapolation layer for exact Steam Link 2.0.20/5001712; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.20/5001812; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.21/5001968; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.22/5002244; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.23/5002363; measured on a Galaxy XR headset. | Controller velocity frame layer for exact Steam Link 2.0.23/5002363; measured on a Galaxy XR headset. | Controller velocity frame layer for exact Steam Link 2.0.20/5001712; measured on 2.0.23/5002363 only, not run on this base. | Controller velocity frame layer for exact Steam Link 2.0.20/5001812; measured on 2.0.23/5002363 only, not run on this base. | Controller velocity frame layer for exact Steam Link 2.0.21/5001968; measured on 2.0.23/5002363 only, not run on this base. | Controller velocity frame layer for exact Steam Link 2.0.22/5002244; measured on 2.0.23/5002363 only, not run on this base. | Native and APK adaptation for exact Steam Link 2.0.20 build 5001812; headset validation pending. |

#### Image quality

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Full-FOV foveal canvas (experimental)](#full-fov-foveal-canvas-experimental) | Only for exact 2.0.20/5001812: draws just the foveal image on a transparent 5000x6000 canvas per eye and submits it as the 2nd projection, keeping the background projection and any static trigger quad. Requires headset validation. | 5001812 |  |

#### Controller

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Controller angular world to local](#controller-angular-world-to-local) | Adapts already corrected OpenXR angular velocity to Steam Link 2.0.23's local controller axes. Requires the active Galaxy XR OpenXR Velocity Frame runtime module; do not use with stock runtime velocities or old HAL/velocity/extrapolation APK layers. Changes angular velocity only, not poses, linear velocity or prediction. Exact 2.0.23/5002363 only; headset A/B validation pending. Controller tracking and haptics based on @4uva4ek's Galaxy XR modules: https://github.com/4uva4ek. | 5002363 |  |
| [Controller grip haptics through Shizuku](#controller-grip-haptics-through-shizuku) | Routes controller vibration to the grip vibrator instead of the trigger, which Galaxy XR uses for every OpenXR vibration. The grip one needs shell rights, so this needs Shizuku running with its permission granted to Steam Link; without it vibration is stock. Controller tracking and haptics based on @4uva4ek's Galaxy XR modules: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [Controller pose extrapolation](#controller-pose-extrapolation) | Makes the Galaxy XR runtime extrapolate controller poses to the time VRLink asks, instead of repeating one sample per display frame, and filters jitter out of the streamed pose. Adds an OpenXR API layer; no Steam Link code changes. Controller tracking and haptics based on @4uva4ek's Galaxy XR modules: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [Controller tracking from the controller HAL through Shizuku](#controller-tracking-from-the-controller-hal-through-shizuku) | Takes controller poses straight from the Galaxy XR controller HAL instead of the OpenXR runtime, removing the runtime's ~35 ms sample lag, and filters jitter out of pose and velocity. The HAL needs shell rights, so this needs Shizuku running with its permission granted to Steam Link; without Shizuku tracking stays as it is. Steam Link 2.0.23: angular velocity is reported local to the grip pose. Steam Link 2.0.20 - 2.0.22: angular velocity is reported in the base space. Controller tracking and haptics based on @4uva4ek's Galaxy XR modules: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [Controller velocity frame](#controller-velocity-frame) | Rotates the controller velocities the Galaxy XR runtime reports into the frames SteamVR reads, so thrown objects leave in the right direction. Stock, they arrive in a controller-attached frame. Adds an OpenXR API layer; no Steam Link code is changed. Steam Link 2.0.23: angular velocity is reported local to the streamed pose. Steam Link 2.0.20 - 2.0.22: angular velocity is reported in the base space. Controller tracking and haptics based on @4uva4ek's Galaxy XR modules: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |

#### Streaming & network

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Codecs Support](#codecs-support) | Exact 2.0.20/5001712: lets the headset client decode H.264 or AV1 streams selected by the GalaxyXR driver's Stream codec setting. A bundled OpenXR layer detects the host's codec from the first video frame and switches the decoder in place; stock HEVC streams are untouched. Recommended on this base; requires the matching driver setting. | 5001712 |  |
| [Codecs Support (2.0.23/5002363)](#codecs-support-2-0-23-5002363) | Optional on exact 2.0.23/5002363: lets the headset client decode H.264 or AV1 streams selected by the GalaxyXR driver's Stream codec setting. A bundled OpenXR layer detects the host's codec from the first video frame and switches the decoder in place; stock HEVC streams are untouched. Not part of the recommended set; headset validation pending. | 5002363 |  |
| [GXR spacewarp layer (experimental)](#gxr-spacewarp-layer-experimental) | Exact 2.0.20/5001712 and 2.0.23/5002363: synthesizes a projection frame from the two most recent decoded frames when the video stream misses a display tick, via a bundled OpenXR implicit layer gated on each base's pinned scene-library layout (per-base pin profiles selected at runtime; the layer itself re-verifies the base's identity anchor before any hook). The client's composition layers stay untouched; any construction or layout failure stands down to the stock renderer. Experimental: headset validation pending, so it is not part of the recommended set. | 5001712, 5002363 |  |

</details>

<details open>
<summary>📦 Steam Link Experimental&nbsp;&nbsp;•&nbsp;&nbsp;1 patch</summary>
<br>

**🎯 Supported versions:**

| 2.0.23 (5002363) |
| :---: |
| Angular adapter for corrected base-space velocities; requires active runtime correction. |

#### Controller

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Controller angular world to local](#controller-angular-world-to-local) | Adapts already corrected OpenXR angular velocity to Steam Link 2.0.23's local controller axes. Requires the active Galaxy XR OpenXR Velocity Frame runtime module; do not use with stock runtime velocities or old HAL/velocity/extrapolation APK layers. Changes angular velocity only, not poses, linear velocity or prediction. Exact 2.0.23/5002363 only; headset A/B validation pending. Controller tracking and haptics based on @4uva4ek's Galaxy XR modules: https://github.com/4uva4ek. | 5002363 |  |

</details>

<!-- PATCHES_END -->

## Building from source

On Windows, verify JVM tests and build the Android bundle with automatic cleanup:

```powershell
.\Verify-Build.ps1
.\Verify-Build.ps1 -Tasks ':patches:test', ':patches:auditDecodedSteamLinkPatches'
```

Each run uses a fresh workspace. Bundles remain under
`../builds/steamlink-patches/build/verification/<run>/artifacts/patches/libs`, alongside retained test reports,
the Gradle log and a cleanup receipt. Compiler files and decoded audit derivatives
are removed after Gradle exits, including on failure. Pass `-KeepBuildOutputs` to
retain intermediates. Exact fixtures, tools, caches and existing outputs are
preserved. See [verification and disk cleanup](diagnostics/build-verification/README.md).

Direct Gradle commands use the external output folders described below;
they do not perform the wrapper's automatic cleanup:

```
./gradlew buildAndroid
```

Output: `../builds/steamlink-patches/gradle/patches/libs/patches-*.mpp`

All generated output and project Gradle/Kotlin state live beneath
`../builds/steamlink-patches/`. Retained audit fixtures/tools/evidence use its
`build/` directory; Gradle uses separate `gradle/root` and `gradle/patches`
directories so `clean` preserves those inputs. Native CMake output uses
`extensions/<name>/build-*` under the same external root. See the
[2026-10-08 relocation record](diagnostics/build-layout/README.md).

No Android SDK is required. The extension DEX is assembled from smali sources directly by the build.

## Documentation maintenance

Release automation refreshes the full patch list in this file and commits it with the release metadata. To refresh it locally from an existing catalog:

```powershell
$env:PYTHONIOENCODING = 'utf-8'
python .github/scripts/generate_patches_readme.py AngelDark92/xr-porting-hub main patches-list-all.json TECHNICAL_REFERENCE.md
```

The generator also defaults to `TECHNICAL_REFERENCE.md` when no output path is supplied. Keep the README focused on user setup; update its bundle guidance when patch recommendations change.

## Further reference

- [Patch catalog](PATCH_CATALOG.md): per-patch edits, dependencies, guarded layouts, and controlled OLED comparison instructions.
- [Debugging guide](DEBUGGING.md): diagnostic capture procedures.
- [Changelog](CHANGELOG.md): release history.

# Technical reference — XR Porting Hub

Implementation notes, compatibility details, validation status, and build instructions for the Galaxy XR app patch set (Steam Link VR and 4XVR). For installation and patch selection, see the [README](README.md). The tables below cover both app patch sets; the 4XVR set is recommended by default — see its [plan](diagnostics/4xvr-galaxyxr/2026-10-11-4xvr-galaxyxr-diff-and-plan.md) and the README status and credits section.

## Steam Link 2.0.20 / 5001812 and 2.0.21 / 5001968

The core compatibility matrix exposes 21 standalone patches on each base. It recommends
19 on 2.0.20/5001812 (including BOOST) and 18 on 2.0.21/5001968 (BOOST optional).
2.0.20/5001712 exposes 22 core standalone patches and recommends 20, including BOOST and
Codecs Support. Codecs Support remains excluded on 5001812 and 5001968. Native addresses, complete-function guards and shader
callers were derived independently for each build. Both use the verified
2-projection helper, mouse-only SDL routing and array-shaped HMD extension
configuration. Blue-noise dithering is recommended with its default foveal layer. See the [adaptation and validation record](diagnostics/steamlink-legacy-1812-1968/README.md).

## Steam Link 2.0.23 / 5002363

Exact 2.0.23/5002363 exposes 11 core individual patches and recommends 10 by default, including Network Optimizations, OpenXR CPU and GPU BOOST, and the merged Codecs Support patch. Device identity remains optional. Native addresses and the existing decoder layout guards are unchanged; codec headset validation on this base is pending. See the [5002363 audit](diagnostics/steamlink-5002363/README.md) for source APK provenance, exact addresses, mutation/option tests, APK patching and remaining runtime gaps.

## Compatibility and implementation notes

The Android-Surface Fovea experiment was **tried and retired on 2026-09-07** after
the user reported it did not work. It copied actual fovea pixels into a Surface,
kept 3 projections and omitted the dummy quad. Its failure cause is unknown; the
working terminal-quad fix is unchanged. See the [retirement record](diagnostics/steamlink-surface-fovea/README.md).

Steam Link VR (`com.valvesoftware.steamlinkvr`) was not built for Android XR. These patches adapt it to run on the Samsung Galaxy XR headset by injecting the missing OpenXR permissions and features, bundling the Galaxy XR XR-bridge native library, providing an optional standalone face-bridge layer for face-tracking, fixing broken permission flows, tuning the rendering pipeline, and optionally allowing the patched APK to coexist with the original install.

Target APK: `com.valvesoftware.steamlinkvr`. Exact compatibility metadata and guarded adaptations include v2.0.20/5001712, v2.0.20/5001812, v2.0.21/5001968, v2.0.22/5002244, and v2.0.23/5002363. The permission-free high-resolution fix accepts exactly those 5 builds; 5001712 and 5002244 have verified layouts, while 5002363 is a decoded-base adaptation with pending headset validation. Reconstruction, quad-view, permission-matrix, warm-up/omit, and DFR re-arm experiments are retired.

Use Morphe Manager 1.30.0 or newer with compatibility checks enabled for build-specific filtering. Manager 1.7 cannot distinguish APKs that share versionName `2.0.22`, and Expert mode may intentionally show incompatible patches. Morphe's `default` flag is one global value per patch, so the recommended defaults live on the individual patches: all 4 legacy bases (2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968 and 2.0.22/5002244) default-enable the [legacy recommended set](PATCH_CATALOG.md#recommended-set-per-exact-build) — 18 patches on 5001968 and 5002244, 19 on 5001812 (adds BOOST), and 20 on 5001712 (adds BOOST and **Codecs Support**) — including the 3 native force-gate patches, required XR foundation patches, Device identity with the Meta Quest Pro spoof, and both explicit older startup patches, and Morphe Manager auto-selects it for the detected build. 2.0.23/5002363 default-enables GXR face and tongue bridge (native face tracking), Galaxy XR high-resolution fix, Microphone input preset (Voice Recognition), OLED color calibration (`final-balanced`), Blue-noise dithering (fovea default), Unrestricted battery usage, Network Optimizations, Visual Delay Fix (60 ms), OpenXR CPU and GPU BOOST, and Codecs Support. The merged codec patch selects the exact 5001712 or 5002363 layout; 5001812 remains excluded. BOOST stays optional on 2.0.21 and 2.0.22. The full GXR face bridge is restricted to the 4 legacy builds so it cannot replace Valve's native 5002363 face mappings. **Appear on top (legacy)** and **Change package name** are never recommended.

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
> **[v1.32.1](https://github.com/AngelDark92/xr-porting-hub/releases/tag/v1.32.1)**&nbsp;&nbsp;•&nbsp;&nbsp;`main`&nbsp;&nbsp;•&nbsp;&nbsp;49 patches total
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
| [HDR pipeline payload (4XVR)](#hdr-pipeline-payload-4xvr) | Adds the HDR video component and its open-source notices; built on libplacebo (LGPL-2.1-or-later). Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [libplacebo HDR pipeline (4XVR)](#libplacebo-hdr-pipeline-4xvr) | HDR videos play with their full colors and brightness. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Tracking & audio

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [5.1-channel audio bridge (4XVR)](#5-1-channel-audio-bridge-4xvr) | Switches 4XVR video playback to 5.1 surround sound on the headset speakers. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Audio bridge payload (4XVR)](#audio-bridge-payload-4xvr) | Adds the bundled sound engine that lets 4XVR play 5.1 surround sound on the headset speakers. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Galaxy XR controller models (4XVR)](#galaxy-xr-controller-models-4xvr) | Replaces the Quest 3 controller artwork and shapes with the Galaxy XR controller models. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Startup & permissions

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Galaxy XR launch compatibility (4XVR)](#galaxy-xr-launch-compatibility-4xvr) | Installs the standard XR files 4XVR needs to start on Galaxy XR instead of closing right after opening. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Galaxy XR launcher icon (4XVR)](#galaxy-xr-launcher-icon-4xvr) | Restores the proper 4XVR icon on the device's app launcher and in the patch manager. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Popup refresh on resume (4XVR)](#popup-refresh-on-resume-4xvr) | Re-checks the first-launch prompts after you return from system settings. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Startup and UI continuity helper (4XVR)](#startup-and-ui-continuity-helper-4xvr) | Keeps the startup greeting and on-screen notifications working when the app rebuilds its display. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Advanced XR compatibility

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Frame-synced render dispatch (4XVR)](#frame-synced-render-dispatch-4xvr) | Syncs scene changes and screen updates to the display's frame rhythm, keeping playback smooth. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Galaxy XR refresh profile (72 Hz, 4XVR)](#galaxy-xr-refresh-profile-72-hz-4xvr) | Runs 4XVR's picture at Galaxy XR's native 72 Hz rate instead of 60 Hz. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Meta platform decoupling (4XVR)](#meta-platform-decoupling-4xvr) | Keeps the Meta store calls out of the way, so 4XVR starts instead of hanging on its splash screen. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Render dispatch payload (4XVR)](#render-dispatch-payload-4xvr) | Adds the scene-timing engine that keeps 4XVR visuals and screens in step with the frames. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Premium & purchases

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Premium unlock (4XVR, experimental)](#premium-unlock-4xvr-experimental) | Unlocks premium playback without a store check on Galaxy XR, where the store cannot be reached. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

#### Subtitles & playback

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Subtitle suite (4XVR)](#subtitle-suite-4xvr) | Moves subtitles around, enlarges the subtitle panel and loads your own subtitle font. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |
| [Subtitle suite payload (4XVR)](#subtitle-suite-payload-4xvr) | Adds the subtitle engine used by the 4XVR subtitle controls. Builds: 4XVR 2.0.22/20022. Based on @logkim1's 4XVR-GalaxyXR patches: https://github.com/logkim1/4XVR-GalaxyXR. | 20022 |  |

</details>

<details open>
<summary>📦 Steam Link&nbsp;&nbsp;•&nbsp;&nbsp;26 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) | 2.0.23 (5002363) | 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Verified Steam Link 2.0.20 build 5001712. | Native and APK adaptation for exact Steam Link 2.0.20 build 5001812; headset validation pending. | Native and APK adaptation for exact Steam Link 2.0.21 build 5001968; headset validation pending. | Verified Steam Link 2.0.22 build 5002244. | Build 5002363 recommends Galaxy XR high-resolution 3-projection fix, GXR face and tongue bridge, Microphone input preset (Voice Recognition), Unrestricted battery usage, Visual Delay Fix (60 ms), OLED color calibration with the Final balanced tested profile, Blue-noise dithering with the default foveal layer, Network Optimizations, OpenXR CPU and GPU BOOST, Codecs Support, and Controller velocity frame. The retired projection experiments are excluded. Decoded-base adaptation; headset validation pending. | Native and APK adaptation for exact Steam Link 2.0.23 build 5002363; headset validation pending. | Exact Steam Link 2.0.20/5001712 high-resolution target with its isolated 2-projection to 3-layer payload. The topology correction has prior user-reported startup and delayed-frame runtime evidence; this rebuilt binary remains uninstalled. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.20 build 5001812; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.21 build 5001968; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.22 build 5002244; headset validation pending. | Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact Steam Link 2.0.23 build 5002363; headset validation pending. |

#### Image quality

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Blue-noise dithering](#blue-noise-dithering) | Adds fine grain that hides harsh color steps, with separate on/off choices for the center and the background. Builds: 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244, 2.0.23/5002363. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Center of the view<br>• Background |
| [Galaxy XR high-resolution 3-projection fix](#galaxy-xr-high-resolution-3-projection-fix) | Permission-free high-resolution fix: adds a small Android-surface trigger and leaves the renderer's own formats, including future RGB10_A2, untouched. No image copy. Builds: 5001712, 5001812, 5001968, 5002244, 5002363. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [OLED color calibration](#oled-color-calibration) | Tunes the OLED headset's color with gamma and saturation presets, plus optional center-of-view tweaks; visible improvement needs headset verification. Builds: 5001712, 5001812, 5001968, 5002244, 5002363. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Calibration profile<br>• Gamma<br>• Saturation<br>• VD-like center mode<br>• Center gamma adjustment |

#### Tracking & audio

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [GXR face and tongue bridge](#gxr-face-and-tongue-bridge) | Face and tongue feed for the VRCFT module: the face bridge on 5001712/5001812/5001968/5002244, tongue out/left/right/up/down slots on 2.0.23/5002363; Valve's own expressions and tongue-out keep working. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [Microphone input preset](#microphone-input-preset) | Switches the microphone's capture mode; Voice Recognition tested clearest and loudest on Galaxy XR. Builds: 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244, 2.0.23/5002363. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Microphone mode |
| [Visual Delay Fix](#visual-delay-fix) | Re-times the headset's position report so head movement shows up sooner; controller movement is untouched. Builds: 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244, 2.0.23/5002363. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Head motion offset (ms) |
| [XR Input Routing Config](#xr-input-routing-config) | Installs the settings file that routes pointer and button input to the launcher UI. | 5001712, 5001812, 5001968, 5002244 |  |

#### Startup & permissions

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Appear on top (legacy)](#appear-on-top-legacy) | Legacy overlay fallback for older builds: lets Steam Link draw its panel over other screens. | 5001712, 5001812, 5001968, 5002244 |  |
| [Startup permission requests (before 5002322)](#startup-permission-requests-before-5002322) | Requests hand, eye, face, microphone and Bluetooth permissions at startup on older builds; battery settings and the splash are separate patches. | 5001712, 5001812, 5001968, 5002244 |  |
| [Startup splash and XR launch mode (before 5002322)](#startup-splash-and-xr-launch-mode-before-5002322) | Adds the Launching Steam Link splash and full-space VR start mode on older builds; requests no permissions (pick Startup permission requests for those). | 5001712, 5001812, 5001968, 5002244 |  |
| [Unrestricted battery usage](#unrestricted-battery-usage) | Opens Android's Battery usage page at startup so Unrestricted can be selected for XR streaming. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |

#### App & device identity

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Change package name](#change-package-name) | Renames the app so the patched Steam Link can install next to the original. Builds: 5001712 to 5002244. | 5001712, 5001812, 5001968, 5002244 | • Package name |
| [Device identity](#device-identity) | Sets the headset name SteamVR sees, with Meta Quest Pro as the recommended profile. Builds: 5001712 to 5002244. | 5001712, 5001812, 5001968, 5002244 | • Headset identity |
| [Device identity (2.0.23/5002363)](#device-identity-2-0-23-5002363) | Sets the headset name SteamVR sees; optional, so it stays out of the recommended set. Builds: exact 2.0.23/5002363. | 5002363 | • Headset identity |
| [Galaxy XR SteamVR driver (galaxyxr)](#galaxy-xr-steamvr-driver-galaxyxr) | Sets Galaxy XR headset and controller names in SteamVR, replacing the Meta Quest Pro spoof. Builds: 5001712 to 5002363. Needs: Galaxy XR VRLink Setup on the PC, or controllers show no model. Details: https://github.com/4uva4ek/steamlink-patches. | 5001712, 5001812, 5001968, 5002244, 5002363 | • Grip offset position (cm, x,y,z)<br>• Grip offset rotation (deg, x,y,z) |
| [OpenXR CPU and GPU BOOST](#openxr-cpu-and-gpu-boost) | Asks OpenXR for the CPU and GPU BOOST hint at each session start; the headset still controls clocks and thermals, and unsupported ones continue without BOOST. No root needed; performance gain not yet measured. Details: https://github.com/4uva4ek/steamlink-patches. | 5001712, 5001812, 5002363 |  |
| [OpenXR CPU and GPU BOOST (2.0.21/2.0.22)](#openxr-cpu-and-gpu-boost-2-0-21-2-0-22) | Asks OpenXR for the CPU and GPU BOOST hint at each session start; the headset still controls clocks and thermals, and unsupported ones continue without BOOST. No root needed; performance gain not yet measured. Details: https://github.com/4uva4ek/steamlink-patches. | 5001968, 5002244 |  |

#### Advanced XR compatibility

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Android XR native permission names](#android-xr-native-permission-names) | Swaps the app's built-in face and eye permission names to the ones Android XR needs. Builds: 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244. | 5001712, 5001812, 5001968, 5002244 |  |
| [Force HMD initialization gates](#force-hmd-initialization-gates) | Removes 2 headset startup checks so tracking starts even when the app's own checks would stop it. Builds: 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244. | 5001712, 5001812, 5001968, 5002244 |  |
| [Force lobby permission-state gate](#force-lobby-permission-state-gate) | Skips a lobby permission check so the room screen loads normally. Builds: 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244. | 5001712, 5001812, 5001968, 5002244 |  |
| [Force stream XR gates](#force-stream-xr-gates) | Removes 3 XR checks on the streaming path so video can start. Builds: 2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244. | 5001712, 5001812, 5001968, 5002244 |  |
| [XR Core Runtime](#xr-core-runtime) | Installs the bridge files the other XR patches need. | 5001712, 5001812, 5001968, 5002244 |  |
| [XR Device Config Baseline](#xr-device-config-baseline) | Installs the default Galaxy XR headset and controller settings plus the dashboard files. | 5001712, 5001812, 5001968, 5002244 |  |
| [XR Manifest Capability Pack](#xr-manifest-capability-pack) | Adds the Android XR/OpenXR permissions, features and app properties Steam Link needs. | 5001712, 5001812, 5001968, 5002244 |  |

#### Controller

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Controller velocity frame](#controller-velocity-frame) | Fixes thrown-object directions by rotating controller speeds into the frame SteamVR expects. Builds: 2.0.23, angular velocity is reported local to the streamed pose; 2.0.20 - 2.0.22, it is reported in the base space. Angles measured on 2.0.23/5002363. Needs: nothing. The root Velocity Frame and Steam Link Pose Time modules do not run while this layer is loaded. Details: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |

#### Streaming & network

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Network Optimizations](#network-optimizations) | Keeps streaming smooth after Wi-Fi hiccups: lost packets are re-requested, recent video frames are kept, and bigger buffers absorb bursts; headset verification still pending. Builds: 2.0.23/5002363 only. | 5002363 |  |

</details>

<details open>
<summary>📦 Steam Link Experimental&nbsp;&nbsp;•&nbsp;&nbsp;7 patches</summary>
<br>

**🎯 Supported versions:**

| 2.0.20 (5001712) | 2.0.23 (5002363) | 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) | 2.0.20 (5001712) | 2.0.20 (5001812) | 2.0.21 (5001968) | 2.0.22 (5002244) | 2.0.23 (5002363) | 2.0.20 (5001812) |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Verified Steam Link 2.0.20 build 5001712. | Native and APK adaptation for exact Steam Link 2.0.23 build 5002363; headset validation pending. | Experimental patches for Steam Link 2.0.20 build 5001712. | Experimental patches for Steam Link 2.0.20 build 5001812. | Experimental patches for Steam Link 2.0.21 build 5001968. | Experimental patches for Steam Link 2.0.22 build 5002244. | Experimental patches for Steam Link 2.0.23 build 5002363. | Controller pose extrapolation layer for exact Steam Link 2.0.20/5001712; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.20/5001812; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.21/5001968; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.22/5002244; measured on 2.0.23/5002363 only, not run on this base. | Controller pose extrapolation layer for exact Steam Link 2.0.23/5002363; measured on a Galaxy XR headset. | Native and APK adaptation for exact Steam Link 2.0.20 build 5001812; headset validation pending. |

#### Image quality

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Full-FOV foveal canvas (experimental)](#full-fov-foveal-canvas-experimental) | Only for exact 2.0.20/5001812: draws just the foveal image on a transparent 5000x6000 canvas per eye, keeping the background view. Needs: headset validation. | 5001812 |  |

#### Controller

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Controller pose extrapolation](#controller-pose-extrapolation) | Makes Galaxy XR predict controller poses to the moment the app asks, instead of repeating one sample per frame, and smooths out jitter. Builds: 5001712 to 5002363 (measured on 2.0.23). Needs: nothing. Details: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [Controller pose time (experimental)](#controller-pose-time-experimental) | Sends each controller pose to SteamVR with the time it was sampled at instead of the packet's send time, so SteamVR predicts the controller from the right moment. The Galaxy XR runtime labels every controller pose 28.82 ms after the sample it carries; the compensation is subtracted from that label. Builds: exact 2.0.20/5001712 and 2.0.23/5002363, Galaxy XR firmware I610UEU2AZF3. Experimental: ported from the Steam Link Pose Time root module, not yet run as an APK layer on a headset. Needs: nothing; not with Controller pose extrapolation or the controller HAL pose patch. Details: https://github.com/4uva4ek. | 5001712, 5002363 | • Pose time compensation (ms) |

#### Controllers (Shizuku)

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Controller grip haptics through Shizuku](#controller-grip-haptics-through-shizuku) | Sends controller vibration to the grip motor instead of the trigger Galaxy XR uses by default. Builds: 5001712 to 5002363. Needs: Shizuku running with its permission granted to Steam Link; without it vibration stays stock. Details: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |
| [Controller tracking from the controller HAL through Shizuku](#controller-tracking-from-the-controller-hal-through-shizuku) | Reads controller poses straight from the headset's HAL, removing ~35 ms of lag and smoothing jitter. HAL access needs shell rights, so this needs Shizuku running with its permission granted to Steam Link; without it tracking stays stock. Builds: 2.0.23, angular velocity is reported local to the grip pose; 2.0.20 - 2.0.22, it is reported in the base space. Details: https://github.com/4uva4ek. | 5001712, 5001812, 5001968, 5002244, 5002363 |  |

#### Streaming & network

| 💊&nbsp;Patch | 📜&nbsp;Description | 🔢&nbsp;Builds | ⚙️&nbsp;Options |
|----------|----------------|----------------|-----------|
| [Codecs Support](#codecs-support) | Lets the headset decode the H.264 or AV1 video the GalaxyXR driver's Stream codec setting picks; stock HEVC streams are untouched. Builds: 2.0.20/5001712, 2.0.23/5002363. Needs: the matching driver codec setting. Headset validation pending on 2.0.23/5002363. | 5001712, 5002363 | • Enable telemetry |
| [GXR spacewarp layer (experimental)](#gxr-spacewarp-layer-experimental) | Makes up a frame from the two latest decoded video frames when the stream misses a display tick; any failure stands down to the stock renderer. Builds: 2.0.20/5001712, 2.0.23/5002363. Experimental: headset validation pending. | 5001712, 5002363 |  |

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

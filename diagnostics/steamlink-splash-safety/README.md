# 2363 splash-screen safety analysis (2026-10-22)

Scope: does any recommended 2.0.23/5002363 patch touch the pre-PCVR launch splash in a
way that can corrupt its aspect ratio?

## Static finding

- The splash shown before the PC list is the `SteamLink` 2D launcher panel
  (`android:defaultWidth="1422.0px"`, `defaultHeight="800.0px"` in the stock manifest).
  It is not the immersive `VRLink` `NativeActivity` (`android.app.lib_name=vrlink_scene`).
- No recommended 2363 patch edits `SteamLink` layout, theme, splash drawable, or
  `android:defaultWidth/Height`.
- High-resolution fix: the `libgxr_ast.so` layer's `valveProjectionShape()` accepts only
  `layerCount == kSourceProjectionCount`, every layer `XR_TYPE_COMPOSITION_LAYER_PROJECTION`
  and `viewCount == 2`; `layerEndFrame` additionally requires a visible/focused session.
  A 2D splash/panel composition does not match and is passed through unchanged.
- GXR face bridge: on 2363 the face-bridge library/manifest installer is skipped; the only
  manifest action is a permission already present in stock 2363.
- Microphone, Network, Visual Delay, OLED and Blue-noise edit `libvrlink_scene.so` /
  `libmain.so`, not the launcher panel.
- Codecs Support and Controller velocity frame are implicit OpenXR layers and only act in
  an active XR session.
- Unrestricted battery is the only remaining startup-time candidate: `GxrBatterySettings.request`
  is inserted after `SteamLink.onCreate` calls its superclass and may open the system battery
  settings page when the app is not yet exempt. That can cover the splash, but it does not
  resize the splash surface.

## Device A/B still required

No headset was attached during this analysis (`adb devices` empty). On a 2363 install:

1. Stock, full recommended, full minus battery, full minus high-resolution.
2. Cold start to the PC list, record `screenrecord`, `dumpsys window windows`,
   `dumpsys SurfaceFlinger --list` and `logcat` for each.
3. Compare the first splash frame bounds and effective aspect ratio.
4. If battery is implicated, confirm whether the settings activity is what appears over the
   splash; if high-resolution is implicated, capture the layer's `surface_trigger_*` and
   `layerEndFrame` passthrough logs.

Static analysis currently finds no patch that resizes the splash. Device evidence is still
needed before closing this item.

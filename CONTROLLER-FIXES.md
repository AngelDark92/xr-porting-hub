# Controller fixes — what to apply

**Controller velocity frame** is on by default on all 5 bases. Controller pose extrapolation and the Shizuku patches are optional and default-off: add them only if controller motion or haptics bother you. Use one path — do not mix an APK patch and a root module for the same fix.

Developer guidance (@4uva4ek, author of these controller fixes):

> For non root extrapolation and Velocity fix would be best options. Additionally all controllers shizuku for non root. For root no patch is needed, only kernelsu modules with controller fixes. For root optional galaxy driver for vrlink compatibility.

## Pick your path

| Setup | Apply | Notes |
|---|---|---|
| **Non-root, no Shizuku** | **Controller pose extrapolation** + **Controller velocity frame** | The best non-root pair. Velocity frame is on by default; add pose extrapolation. Builds 5001712, 5001812, 5001968, 5002244, 5002363; measured on 2.0.23/5002363. |
| **Non-root, Shizuku running** | The pair above + **Controller tracking from the controller HAL through Shizuku** + **Controller grip haptics through Shizuku** | Grant Shizuku's permission to Steam Link. Without Shizuku both pass through: tracking and vibration stay stock. |
| **Root (KernelSU)** | **No controller APK patch** — deselect the default **Controller velocity frame**: the Velocity Frame and Pose Time modules refuse to run while its layer is loaded. KernelSU modules: **Haptics MAIN** + **OpenXR Velocity Frame**, plus one pose layer — **Controller Extrapolation** (experimental; ZIP from Releases if it is not in Sources) or **Steam Link Pose Time** (Steam Link 2.0.20 only, needs Velocity Frame). Optional: **Controller Whitening**, **Controller Timestamp Precision**; **HAL Cache Bypass** is experimental. | Modules patch process memory as root, no Shizuku. Pose Time and Controller Extrapolation reject each other — enable one. "No patch needed" covers controller fixes only; the rest of the patch set still applies. |
| **Root + PC, optional** | **Galaxy XR SteamVR driver** (GalaxyXR-Driver / Galaxy XR Companion) on the PC | VRLink compatibility: Galaxy XR headset/controller identity and controller input profile. PC-side, works with or without root. Use it if SteamVR shows the generic vrlink/Quest Pro device instead of Galaxy XR, or if you want the Galaxy XR controller models and input profile. |

## When to apply

- **Symptom first.** Controllers trailing or jittering, thrown objects going sideways, vibration in the trigger instead of the grip. If stock tracking feels fine, apply nothing.
- **One layer at a time.** Non-root: add the pair, test, then add the Shizuku pair only if Shizuku is installed and granted. Root: add Haptics MAIN + Velocity Frame, test, then add one pose layer.
- **Experimental, exact 5001712 and 5002363:** **Controller pose time** dates each controller pose at the time the runtime sampled it, minus a compensation (option, 0 to 30 ms, default 5). It is the APK form of the root **Steam Link Pose Time** module and needs no root. Not with Controller pose extrapolation or the Shizuku HAL pose patch.
- **No angular adapter.** The former *Controller angular world to local* adapter for 2.0.23 is retired: **Controller velocity frame** already reports the angular velocity in the local axes Steam Link 2.0.23 reads.
- **Root modules install disabled.** Enable and restore per module instructions; the Action button reports state, it does not toggle. APK patches live in the installed APK — repatch to change them.

## Build coverage

The APK controller patches cover 5001712, 5001812, 5001968, 5002244 and 5002363. Pose extrapolation and velocity frame were measured on 2.0.23/5002363; the HAL pose layer also ran on 5001712; grip haptics was checked on 5002363. Root modules pin their own runtime/firmware compatibility — read each module README before installing (Controller Extrapolation was validated on firmware I610UEU2AZF3 with Steam Link 2.0.20/5001712).

## Links

- KernelSU modules: https://github.com/4uva4ek/galaxyxr-modules
- Shizuku: https://github.com/RikkaApps/Shizuku
- Galaxy XR SteamVR driver: https://github.com/AngelDark92/GalaxyXR-Driver

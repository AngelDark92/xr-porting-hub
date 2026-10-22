# Exact legacy adaptations: 2.0.20/5001812 and 2.0.21/5001968

> **Restored 2026-10-22.** 5001812, 5001968 and 5002244 are supported again. See
> [RESTORED-2026-10-22.md](RESTORED-2026-10-22.md). The 2026-10-21 retirement note remains
> below as history.

Validated 2026-09-28. Both exact pairs expose the same **17 direct recommended
patches** and **22 standalone patches** as 2.0.20/5001712. Each pair has its own
default-enabled selector. Individual patches remain default-off; the 2 blue-noise
patches remain experimental and outside the recommendation. Existing exact bases,
defaults, dependencies and option definitions are preserved in all 4 catalogs.

This includes actual per-build native addresses and bytecode adaptation. It is
not compatibility metadata alone. No APK installation, ADB, headset test, SteamVR
change or release publication was performed.

## Exact inputs and implementation

Both original APKs pass ZIP CRC and APK v2 signature verification with Valve
certificate SHA-256 `5dff6b05761447a5bdf919ea88fc6fdf20d301e30b2315415c4d368ec0fbda45`.
The package is `com.valvesoftware.steamlinkvr`; aapt2 verifies each exact pair.
All 23 native libraries in each decoded base match the corresponding original APK.
See [input provenance](input-provenance.json) and [verification script](Verify-Inputs.py).

| Input | 5001812 | 5001968 |
|---|---|---|
| Version name | 2.0.20 | 2.0.21 |
| Original APK bytes | 40,427,387 | 41,190,915 |
| Scene library bytes | 2,220,872 | 2,234,048 |
| Scene SHA-256 | `eebf7eabfb299ab7b9e5bba1612d4a32b27c51f451efc2bd206ba6fc6ac5205a` | `596b5680aa6c217daf5c151de517b1ad61b3999c6fd864ff59a718136ca40192` |
| Permission routine | `0x142c84` | `0x13ec7c` |
| HMD initialization gates | `0xffc5c`, `0xffc64` | `0xfbc04`, `0xfbc0c` |
| Lobby gate | `0x10dc70` | `0x109bf0` |
| Stream gates | `0x1166c4`, `0x1166cc`, `0x116780` | `0x112644`, `0x11264c`, `0x112700` |
| HMD pose hook | `0x101648` | `0xfd5f0` |
| Trampoline file offset | `0x21dad0` | `0x220e88` |
| Native segment alignment | `0x1000` | `0x4000` |
| Microphone preset | `0xf4484` | `0xefdb4` |
| Projection format sites | `0x10ab24`, `0x10ab94` | `0x106a94`, `0x106b04` |
| Video shader prefix | `0x9b3ca` | `0x94fdd` |
| Masked / opaque suffix | `0xa6503` / `0x9d182` | `0xa0677` / `0x96e5b` |
| Foveal / background draw return | `0xf1b48` / `0xf1c28` | `0xed478` / `0xed558` |

[Core native evidence](core-native-evidence.json) contains every permission string,
HMD velocity store, controller cadence instruction, ELF mapping and function hash.
[Color evidence](color-layouts.json) contains shader identities, swapchain sites,
11 import hooks and full constructor/render/ELF guards. The adjacent bounded
disassemblies and shader-reference reports establish the active callers.

New native mutations check the exact version pair, file size, complete-function
identities and original/already-patched instruction preconditions. Complete-function
hashing normalizes only owned instruction slots so supported patches can compose;
each owning helper validates its own slots. Unknown immutable code fails closed.
Checks precede writes; the audits cover idempotence and supported transitions.

Both renderers submit 2 projection structures, so exact dispatch selects the existing
`libgxr_ast_5001712.so` payload. Shader contracts also match the existing blue-noise
helper; the installer configures the new draw-return addresses and shader hashes.
No native payload binary needed rebuilding.

Both HMD configs use an array for `requestedExtensions`; the 5001712 device-map
conversion remains limited to 5001712. Both SDL builds have older pad/joystick JNI
signatures. Mouse-only wrappers avoid the incompatible modern synthetic-pad calls;
the packaged DEX audit checks both injected touch/generic routes. Existing startup
hooks and the 17-patch dependency closure remain intact.

## Validation

| Check | Result |
|---|---|
| Fresh cached Kotlin compilation and JUnit | 129/129 pass, 0 skipped |
| New native production helpers, actual decoded bytes | 2/2 bases pass: all gates/names/permission sites, 4 microphone presets/16 transitions, 6 HMD offsets/36 transitions, 3 cadence modes/9 transitions, 24 combined native orders, bad-site/function/identity checks |
| OLED production matrix | 5/5 bases pass; per base 63 variants, 567 transitions, 126 checkbox cases, 21 VD cases, 93 foveal-gamma cases |
| Foveal blue-noise native matrix | 5/5 bases pass; 24 cases, 72 transitions, 34 rejection cases per base |
| Combined background/foveal matrix | 5/5 bases pass; 99 layer/depth cases, 726 transitions, 32 gamma/hash transitions, 138 rejection cases per base |
| Assembled complete production shaders | 140/140 structural/semantic checks pass; no Android GPU compilation claim |
| Morphe checks loaded from final `.mpp` | 73/73 pass: 5 bundles, 40 standalone stable cases on the 2 new original APKs, 5 high-resolution boundaries, 1 excluded-startup case, 6 VD/gamma cases, 16 blue-noise cases |
| Local Android patch-bundle DEX | D8 9.4.17, Release/API26 pass; `classes.dex` 527,584 bytes |
| Generated catalogs | All 4 preserve existing targets/defaults/options/dependencies; each new pair has 20 stable / 22 experimental standalone patches and 1 default selector |

Detailed receipts are under [validation](validation/), with [catalog regression](catalog-regression.json).
All original APK and decoded-native input hashes remained unchanged. Old 5001712
evidence uses its analysis reconstruction, not a pristine installable source APK.
Older recommendation regressions use retained decoded fixtures; the new-base APK
cases use original signed Valve inputs and produce unsigned audit outputs.

Online and offline Gradle checks both fail before compilation because the pinned
`app.morphe.patches:1.3.3` plugin cannot resolve. Cached compilation, actual Morphe
patching and local D8 packaging succeeded; they do not establish a successful
Gradle/CI release. Android linking, live startup/input/tracking, GPU execution,
format acceptance, timing efficacy and panel output remain unverified.

## Local deliverable and reproduction

Local bundle: `patches/build/libs/patches-1.19.1-1812-1968-local.mpp`.
SHA-256: `227c8736f0bd61bc202a9dedd1a73ad9df5b3547e346c76b8fa337fcf43a908b`.
This file was used as the class/resource source for all 73 final Morphe cases.

From the repository root, using fresh output paths:

```powershell
python diagnostics/steamlink-legacy-1812-1968/Verify-Inputs.py
python diagnostics/steamlink-legacy-1812-1968/audit_core_native.py
python diagnostics/steamlink-legacy-1812-1968/derive_color_layouts.py
./diagnostics/steamlink-5002363/Compile-CachedAudit.ps1 -JavaHome F:/Runtimes/Java21 -OutputDirectory build/legacy-recheck/compiled
python diagnostics/steamlink-legacy-1812-1968/Package-Local.py build/legacy-recheck/compiled patches/build/libs/patches-legacy-recheck.mpp
python diagnostics/steamlink-legacy-1812-1968/Run-MorpheAudits.py build/legacy-recheck/compiled/archive-classpath.txt build/legacy-recheck/morphe
$legacyCp = (Get-Content build/legacy-recheck/compiled/archive-classpath.txt -Raw).Trim()
& F:/Runtimes/Java21/bin/java.exe -Xmx1g -cp $legacyCp util.LegacyAddedNativeAudit (Get-Location).Path
& F:/Runtimes/Java21/bin/java.exe -Xmx1g -cp $legacyCp util.OledDecodedCompatibilityAudit (Get-Location).Path
& F:/Runtimes/Java21/bin/java.exe -Xmx1g -cp $legacyCp util.BlueNoiseDecodedAudit (Get-Location).Path
& F:/Runtimes/Java21/bin/java.exe -Xmx1g -cp $legacyCp util.BackgroundBlueNoiseDecodedAudit (Get-Location).Path
& F:/Runtimes/Java21/bin/java.exe -Xmx1g -cp $legacyCp util.Sdr10ShaderAssembleAudit (Get-Location).Path build/legacy-recheck/glsl
python -X utf8 diagnostics/steamlink-colour/glsl_validate.py build/legacy-recheck/glsl
```

The color extractor uses the retained pyelftools/capstone cache at
`build/oled-native-audit/python`; core extraction uses the retained Android NDK
LLVM tools. Once plugin resolution works, use the standard Gradle test/build path
and `:patches:auditSteamLinkLegacyNative` alongside the existing OLED/APK audits.

## Artifact lifecycle

The final MPP, canonical source/payloads, original APKs, exact decoded fixtures and
tool caches are retained. Compact logs, JUnit reports, APK hashes, native traces
and reproduction scripts are saved here. [Cleanup receipt](cleanup.json) inventories
only `build/adapt-1812-1968`: 10,598 files / 15,102,268,063 bytes. Automatic approval
review rejected the scoped deletion before execution with "blocked by policy";
0 bytes were removed. Cleanup is deferred until deletion of that recorded target
is permitted and path/process checks are repeated. Other build directories, older
local bundles, unrelated uncommitted work and adjacent repositories are preserved.

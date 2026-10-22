# XR Porting Hub — Copilot context

Kotlin morphe-patcher patch library for several Galaxy XR apps. Steam Link VR
(`com.valvesoftware.steamlinkvr`) targets the exact `(versionName, versionCode)` bases
2.0.20/5001712, 2.0.20/5001812, 2.0.21/5001968, 2.0.22/5002244 and 2.0.23/5002363; 4XVR
(`cn.vr4p.oculus4xvrplayerov`) targets 2.0.22/20022. Read repository-root `AGENTS.md`
first — its app registry maps each app to its package, bases, sources, resources and
extension payload.

## Patch authoring rules

- No inline smali injection (`addInstructions(index, "smali string")`). Crashes in morphe-patcher 1.7.0.
- Typed dexlib2 bytecode edits and exact class/method lookups are allowed when validated against every declared base.
- Use `rawResourcePatch` for raw APK files (lib/, assets/, .so binaries).
- Use `resourcePatch` with `finalize {}` for AndroidManifest.xml / XML edits.
- Every top-level patch: `@Suppress("unused")` plus the appropriate exact compatibility list from `shared/Constants.kt` (`COMPATIBILITIES_STEAM_LINK*`, `COMPATIBILITIES_FOUR_XVR`).
- Preserve every existing version adaptation, global default, and build-aware dependency guard when adding a base.
- Prefer one patch object per feature with a per-build layout table. Do not duplicate a patch for another build; duplicate only when the global `default` recommendation scope genuinely differs.
- Generate every public description's `Builds:` line from the same compatibility list passed to `compatibleWith` (`shared/PatchBuilds.kt`). Keep the sentence short; The `assertPatchDescriptionContract` test helper enforces the mapping.
- Keep apps isolated: a patch declares `compatibleWith` only its own app's `Compatibility`, and an app's `extendWith` payload never references another app's `.mpe`, classes or resources.

## Extension DEX (smali)

- Steam Link sources: `patches/src/main/resources/steamlink/androidxr/smali/`; built by `assembleExtension` (plus `assembleMinimalExtension`, `assembleBatteryExtension`)
- 4XVR sources: `patches/src/main/resources/fourxvr/smali/`; built by `assembleFourXvrExtension`
- Outputs: Steam Link `../builds/steamlink-patches/gradle/patches/generated/extension-resources/extensions/extension.mpe`; 4XVR `../builds/steamlink-patches/gradle/patches/generated/fourxvr-extension-resources/extensions/fourxvr-extension.mpe`
- **Smali assembler flag: `-a 33`** — NEVER `-a 35` (produces DEX 040/041 container format; dexlib2 crashes).
- Only brand-new helper classes are assembled; edits to an app's existing classes stay in build-aware Kotlin.

## Build

All generated outputs, temporary checkouts and project caches always go to
`../builds/steamlink-patches/` outside this repository. Use `gradle/root` and
`gradle/patches` for Gradle output, mixed `build/` for retained fixtures/tools/evidence,
and `extensions/<name>/build-*` for native CMake. Preserve mixed inputs during
`clean`; keep canonical source/resources and committed release metadata here.
See `AGENTS.md` and `diagnostics/build-layout/README.md`; pass this contract to
delegated agents and check release asset/attestation consumers when editing builds.
Attestation resolves the exact released bundle to a canonical absolute path;
`..` is rejected even inside absolute patterns. Keep release preparation's
Gradle-only clean so an older preflight bundle cannot become a release asset.
Local Packages access: `tools/Configure-GitHubPackages.ps1`, classic token with
`read:packages`, user Gradle properties outside source. Never put tokens in chat.

```powershell
.\gradlew.bat build                    # full build (every app)
.\gradlew.bat assembleExtension        # rebuild the Steam Link extension DEX only
.\gradlew.bat assembleFourXvrExtension # rebuild the 4XVR extension DEX only
```

---
name: morphe-patches
description: Author, edit, debug and validate Morphe patches for the apps in this repository (Steam Link VR, 4XVR), including exact-build guards and fresh-checkout test inputs.
---

# morphe-patches skill

Workspace scope: `steamlink-patches/` (XR Porting Hub). Prose paths below are
relative to that checkout unless they are explicit workspace links.

Use when authoring, editing, or debugging patches in this project.

## Project facts
- Library: morphe-patcher 1.13.0; Morphe Manager 1.30.0 or newer
- Target apps (this repository patches several): Steam Link VR `com.valvesoftware.steamlinkvr` and 4XVR `cn.vr4p.oculus4xvrplayerov`; exact `(versionName, versionCode)` pairs are defined per app in `shared/Constants.kt` (`COMPATIBILITIES_STEAM_LINK*`, `COMPATIBILITIES_FOUR_XVR`)
- Required compatibility rules: read the repository-root `AGENTS.md` before editing; its app registry maps each app to its package, exact bases, sources, resources and extension payload
- Kotlin source roots: `patches/src/main/kotlin/app/template/patches/steamlink/` and `.../fourxvr/`; shared code lives in `.../patches/shared/` and `patches/src/main/kotlin/util/`
- Resources roots: `patches/src/main/resources/steamlink/` and `patches/src/main/resources/fourxvr/`
- App isolation: a patch declares `compatibleWith` only its own app's `Compatibility`, and an app's extension payload never references another app's `.mpe`, helper classes or resource paths

## Patch type selection

| Need | Use |
|------|-----|
| Copy/replace raw APK file (lib, assets, .so) | `rawResourcePatch {}` |
| Edit AndroidManifest.xml or other XML | `resourcePatch {}` |
| Merge a DEX extension into the app | `bytecodePatch {}` + `extendWith(...)` |

## Patch structure template

```kotlin
@Suppress("unused")
val myPatch = bytecodePatch(          // or rawResourcePatch / resourcePatch
    name = "Human readable name",
    description = "What it does.",
    default = true,
) {
    compatibleWith(COMPATIBILITY_STEAM_LINK)   // always required
    dependsOn(someOtherPatch)                  // if ordering matters

    extendWith("extensions/extension.mpe")     // bytecodePatch only, when needed

    execute {
        // patch logic here
        // rawResourcePatch: get("lib/arm64-v8a/libfoo.so").writeBytes(...)
        // bytecodePatch:    (no fingerprint injection — see FORBIDDEN below)
    }

    finalize {
        // runs after APK rebuild; use for XML/manifest edits via document(...)
    }
}
```

## Adding an app

A new app is a new package, never a widening of an existing patch. Checklist:

1. `shared/Constants.kt`: a private `data class <App>Build(version, versionCode)`, its exact build list, an `is<App><Build>(version, versionCode)` helper that every patch body re-checks, a `Compatibility` built on the primary `AppTarget` constructor with a distinct `name`, and an exposed `COMPATIBILITIES_<APP>`. Never touch another app's lists.
2. Layout: patches `app/template/patches/<app>/`, payloads `resources/<app>/<group>/`, extension smali `resources/<app>/smali/<package path>/`, tests `test/.../<app>/`.
3. Categories: reuse `shared/PatchCategories.kt`; a new category needs the matching `PatchCategoriesTest` group map and expected counts in the same change. That test enumerates patches by hand (it does not discover new top-level patches), so a new app's patches stay outside its category and catalog assertions until they are listed there explicitly. A new category also needs an entry in `CATEGORY_ORDER` in `.github/scripts/generate_patches_readme.py`; unknown categories are appended alphabetically after the known ones. Adding OR moving categories also updates `PatchCategoriesTest`'s numeric asserts in the same change: the group-size `assertEquals(listOf(...))`, the stable/experimental entry totals, and any per-category count assert (e.g. the Controller count).
4. Extension payload: a JavaExec task mirroring `assembleExtension` (input that app's smali dir, args `a -a 33 -o <out>`) emitting `<app>-extension.mpe`, plus `resources.srcDir(...)` and `processResources`/`sourcesJar` dependsOn. Never merge two apps' classes into one `.mpe`.
5. Every patch declares `compatibleWith(*COMPATIBILITIES_<APP>.toTypedArray())` and re-checks the exact pair first: `if (!is<App><Build>(packageMetadata.versionName, packageMetadata.versionCode)) return@execute`.
6. Payloads load through one private loader per app (`object {}.javaClass.getResourceAsStream("/<app>/<path>")`) that errors with the missing path.
7. Native in-place edits: model each as `(offset, original, replacement)`, pin the library by exact size + stock SHA-256, accept original or replacement per site (idempotent), fail closed otherwise; keep an unpinned per-site applier so synthetic tests run without the decoded base.
8. Tests: tracked payload SHA-256 pins, synthetic apply / keep-patched / reject-foreign / wrong-size / wrong-hash cases, and the real-base audit behind `org.junit.Assume.assumeTrue("BLOCKED: ...")`. Report executed and skipped counts separately.
9. Worked example in repo: the 4XVR set (`fourxvr/`, `resources/fourxvr/`, `fourxvr-extension.mpe`). Cross-check every app-member reference in extension smali against the stock decode before assembling — the app's existing classes are patched in place, never replaced.

## Native OpenXR layer protocol pitfall (device-verified 2026-10-12)

An implicit API layer's `getInstanceProcAddr` is queried by the shipped loader with `XR_NULL_HANDLE` for `xrInitializeLoaderKHR` BEFORE any `createApiLayerInstance` chain exists (`ApiLayerInterface::LoadApiLayers` in the app's own `libopenxr_loader.so`). Forwarding that call through a not-yet-assigned next-chain pointer branches to null and SIGSEGVs the app at every start (pc=0, `xrCreateInstance` caller). Guard every layer's fallback: return `XR_ERROR_FUNCTION_UNSUPPORTED` when the next chain is unassigned. A failed call is the loader-tolerated "no forward" answer; `XR_SUCCESS` with a null output pointer makes the loader abort the layer. Repro/evidence: `diagnostics/steamlink-foveal-canvas/2026-10-12-loader-crash-rca-and-fix.md`.

## Extension DEX (smali)

- Sources: `patches/src/main/resources/steamlink/androidxr/smali/` (Steam Link) and `patches/src/main/resources/fourxvr/smali/` (4XVR)
- Built by `assembleExtension` (Steam Link, plus `assembleMinimalExtension` and `assembleBatteryExtension`) and `assembleFourXvrExtension` (4XVR). Each task emits its own payload: `extension.mpe`, `minimal-extension.mpe`, `battery-extension.mpe`, `fourxvr-extension.mpe`
- Only brand-new helper classes are assembled. Edits to an app's existing classes stay in build-aware Kotlin code (typed dexlib2 builders, no raw smali strings). A new app adds its own task and output directory; it does not rewire an existing app's task.
- Output: `../builds/steamlink-patches/gradle/patches/generated/extension-resources/extensions/extension.mpe`
- Delete cached `.mpe` before rebuilding: `Remove-Item ../builds/steamlink-patches/gradle/patches/generated/extension-resources/extensions/extension.mpe`
- **Smali API level: `-a 33`** — never use 35 or higher

### DEX format follows the pinned toolchain
The current smali `d856bad65f` emits standard DEX 040 with a `0x70` header at API 33.
Patcher 1.13.0 parses this format; keep the fresh-helper parsing checks in
`NativeXrTrackingConfigTest` mandatory. The earlier smali pin emitted DEX 039 at
API 33. Do not change DEX headers manually or raise the API level to fix a format mismatch.

Historical Morphe 1.7.0 builds could not parse container-format output from higher
API-level assembly and crashed:
```
Caused by: com.android.tools.smali.dexlib2.util.DexUtil$InvalidFile: Unexpected container offset in header
```
This historical failure does not make every DEX 040 file a multi-DEX container.

## Patch descriptions

Patch `description` text is user-facing (Morphe Manager list) and is copied verbatim into
the tracked `patches-list*.json` catalogs. Tests pin some wording:

- `AndroidSurfaceTriggerResourceTest` asserts `patchSource.contains("future RGB10_A2")`
  against `OptionalXrPatches.kt`.
- `ControllerHalPosePatchTest` / `ControllerVelocityFramePatchTest` assert `needs Shizuku`,
  `local to the grip pose`, `local to the streamed pose`, `in the base space`.
- `GxrSpacewarpLayerPatchTest` asserts `2.0.23/5002363`, `2.0.20/5001712` and `stand` in the description; `FourXvrBundleTest` asserts the premium patch `name` contains `experimental` (rename no patch whose name or text a test pins).
- Sweeps beat lists: grep `patches/src/test` for `.description`/`patchSource.contains`/name assertions before a text pass and treat every hit as locked verbatim; keep build-id pins (`2.0.23/5002363`) inside the rewritten text.
- The 4XVR tests cover the new app instead of wording: `FourXvrNativeGuardTest` pins
  `libvr4p-oculus.so` and `libovrplatformloader.so` size/SHA-256 plus the refresh-profile
  and stub edit bytes and fails closed on wrong-size, wrong-hash or unpinned layouts;
  `FourXvrPayloadResourceTest` pins the bundled 4XVR payload hashes and requires the
  shipped smali helper sources.

Rewriting a description therefore needs a matching test update (or keep the phrase), then
`:patches:generatePatchesList` to rewrite all 4 root catalogs, and `:patches:test`.
`:patches:generatePatchesList` rewrites all 4 root catalogs: it always writes
`patches-list-stable.json` (STABLE), `patches-list-experimental.json` (EXPERIMENTAL) and
`patches-list-all.json` (ALL), and stamps `patches-list.json` (mirror of `patches-list-stable.json`,
byte-identical) with the selected `-PreleaseChannel` (release prep uses `stable`). A bare run
without `-PreleaseChannel` stamps `patches-list.json` as channel `all` (49 patches) — wrong shape;
run `-PreleaseChannel=stable` locally.
`:patches:generatePatchesList` no longer depends on `test` (CI and the release build never
run tests), so the local `:patches:test` run at the end of the task is the only thing that
catches a broken assertion.
Option and AppTarget descriptions are not covered by these assertions.

House description style (user-approved): one short plain-words sentence plus
`Builds: <exact ids/pairs>. Needs: <requirement>.` Keep every build id/pair the old text
named; omit `Needs:` when only a normal install is required; keep credit appends intact
and excluded from the length budget; no jargon beyond app/tool names. Names keep
pinned suffixes like `(experimental)` unchanged.

The build line is generated with `buildLabel(compatibilityList)` from
`shared/PatchBuilds.kt` and the same list is passed to `compatibleWith`. Never hand-write a
build line that can drift from the declared compatibility. The `assertPatchDescriptionContract` test helper enforces the exact code mapping and a 150-character
one-sentence budget. One patch object per feature: add a per-build table row instead of a
second patch object. Duplicate objects only when the global `default` recommendation scope
genuinely differs, and say why in the source comment.

Option texts: `stringOption(...)`/`intSliderOption(...)` carry user-facing `title` and
`description`, plus a `values` map whose left keys are display labels and right keys the
stored settings — reword left keys only when clearly long, never the right-side values,
and keep the mapping 1:1.

For a repo-wide text pass (all descriptions plus option texts): build the inventory from
the root `patches` key of `patches-list-all.json` (a recursive walk over-picks
`compatiblePackages` rows because they carry `name` fields too), map patch names to source
files by grepping `name = "`, then hand one directory batch per parallel child a
scratch-file job (role contract + allowed file list + locked phrases + receipt shape),
run the reviewer over the diff, then regenerate: `:patches:generatePatchesList`, the
readme generator, `:patches:test`, and a remnant sweep expecting zero old-phrase matches
outside `CHANGELOG.md` and `diagnostics/` (history is never rewritten).

## Bytecode editing

- Prefer typed dexlib2 builders such as `BuilderInstruction21c` and the repository's
  `InstructionExtensions` helpers for exact bytecode edits.
- Do **not** call `addInstructions(index, "smali string")`. Historical Morphe Manager 1.7
  builds can crash in `InlineSmaliCompiler` with:
```
Caused by: java.util.NoSuchElementException: Collection is empty.
    at app.morphe.patcher.util.smali.InlineSmaliCompiler$Companion.compile
```
- Structural fingerprints or exact class/method lookups are allowed when validated against every
  declared base. Keep mutation preconditions build-aware and fail closed.
- Parameter register offsets must count register WORDS, not parameter types: `J` and `D` occupy two
  words, so in `RenderFrame(ZJJ)V` p1 sits 5 words below the register count, not 3. Counting types
  mis-anchors every wide-parameter method and silently matches nothing (regression: the 4XVR
  render-dispatch drain anchor; fixed by `parameterWordCount`, pinned by `FourXvrDexKitTest`).
- dexlib2 inserts before a branch TARGET are skipped at runtime: a branch holds the target
  instruction object, and inserting new instructions before that object leaves the jump landing on
  the original instruction, so inserted code never runs on the incoming-branch path. Insert after
  the label target or on a branch-free stretch, and confirm no label sits between the insert and
  the first read of what it defines.
- dexlib2 `Opcode.name` is a FIELD holding the lowercase mnemonic (`"if-eqz"`, `"goto"`), not the
  enum constant: matching `startsWith("IF_")`/`"GOTO"` never fires on a live dex. Match lowercase
  mnemonic prefixes or compare enum constants (`opcode == Opcode.IF_EQZ`). Caught only by a real
  apply-audit (fail-closed require at the `V4BuyVIPDlg.onCreate` join, 2026-10-12).
- Never park a hook result in a stock scratch register across a merge point. Use only registers the
  stock code provably rewrites before their next read on the same straight-line path, or bump the
  register count and use a fresh local exactly as the shipped reference build does. A skipped or
  merged insert turns the register into `Conflict` and ART rejects the whole class.
- Unit tests do not run ART verification: every new or changed bytecode edit needs a device run.
  A rejected class shows in logcat as `java.lang.VerifyError: Verifier rejected class ...
  (base.apk!classesN.dex)` with the register and expected type; treat any such line as a hard patch
  failure (frozen UI, dead playback pipeline), never as app flakiness.
- Byte-hashed text payloads need an explicit `.gitattributes` `text eol=lf` pin. Without it a Windows
  checkout (core.autocrlf) fails the sha256 payload tests while CI on Linux passes; add the pin and
  re-checkout the file instead of touching the expected hash.
- Index-0 insertion is safe only when the method's first instruction is not a branch or switch target
  and no try range starts there; both are cheap to read out of the decoded smali and belong in a
  decoded-tree audit test that skips without the tree (pattern: `FourXvrPopupRefreshAnchorTest` —
  unique selector, head not labelled, 0 `.catch` in the method, plus the helper's call surface:
  public `IsVisible()Z` / `SetHide()V`, the public getter, the reflectively read field name).
- Method-receiver extension helpers (idempotency guards, anchor predicates) need `MutableMethod` from
  `app.morphe.patcher.util.proxy.mutableTypes.MutableMethod` — the patcher fork's package, not
  `app.morphe.patcher.patch`. Guard each `addInstruction` with a check on `instructions.first()` so
  re-running `execute` cannot inject a second call.
- A new `diagnostics/<app>/` record directory is ignored by `diagnostics/*`: add the whitelist block
  (`!diagnostics/<dir>/`, `diagnostics/<dir>/*`, `!diagnostics/<dir>/*.md`) in the same change, or a
  plain `git add` silently drops the records the release docs and `AGENTS.md` cite.

## On-device APK sources (headset pulls)

The headset's Morphe Manager keeps each patched app's pristine original and the patched output under
`/data/data/app.morphe.manager/app_original-apks/<pkg>_<version>[_original].apk` and
`app_patched-apps/<pkg>_<version>.apk` — that is where a missing stock base (for example 4XVR
2.0.22/20022) can be recovered. `/sdcard/Download` copies are `media_rw`-owned and `adb pull` as
shell fails with Permission denied: `su -c 'cp <src> /data/local/tmp/<name>; chmod 644 <name>'`
first, then pull; delete the stage copy afterwards. Verify the pulled bytes with sha256 before use.

## Manager app tile icon

Morphe Manager renders a stored APK's app tile from its manifest icon
(`packageManager.getResourcesForApplication` + `ResourcesCompat.getDrawable(resources, appInfo.icon)`).
A custom icon name or an adaptive XML in `mipmap-anydpi` without `-v26` can resolve to nothing and
show a blank tile. Expose the artwork under the canonical `ic_launcher` / `ic_launcher_round` names
(adaptive XML in `res/mipmap-anydpi-v26/` plus density PNGs) and point the manifest
`android:icon`/`android:roundIcon` at them — see the 4XVR `Galaxy XR launcher icon` patch.

## Constants
Use the exact `(versionName, versionCode)` model and primary `AppTarget` constructor documented in
the repository-root `AGENTS.md`. Never replace the existing compatibility list when adding a base.

## Helpers available
- `loadResource(name: String): ByteArray` — Steam Link private loader for `/steamlink/androidxr/` (`AndroidXrCompatibilityPatch.kt`)
- `loadFourXvrResource(name: String): ByteArray` — 4XVR loader for `/fourxvr/` (`fourxvr/FourXvrDexKit.kt`). Each app keeps its own private loader; there is no cross-app loader.
- `BinaryPatchHelper.findUniqueAndReplace(bytes, search, replace)` — AArch64 .so patching
- `BinaryPatchHelper.vaddrToFileOffset(...)` — virtual address → file offset in ELF

## Build commands

### Required output location

Every local build, native compile, diagnostic runner, temporary checkout and
project cache must write under `../builds/steamlink-patches/`:
`D:/Angelo/Desktop/SteamLink-GalaxyXR-Windows-Toolkit-FULL/builds/steamlink-patches`
in this workspace. Gradle uses `gradle/root` and `gradle/patches`; fixtures/tools
and retained evidence use mixed `build/`; native CMake output uses
`extensions/<name>/build-*`. Keep release catalogs and canonical source/resources
in the checkout. Use the wrappers' configured paths; update release consumers
with output changes. Never create a local `build`, `patches/build`, `.gradle`,
`.kotlin` or Zig cache tree. A lowercase-drive argument (`d:/...`) reaching a native tool or Java
resolves as relative on this host and creates a literal `d/` tree inside the checkout that the
`.gradle` ignore rule hides from `git status`: check for it, delete it, and keep `D:/` spellings in
wrapper arguments. Read [the layout and validation](../../../diagnostics/build-layout/README.md)
when building or relocating artifacts. Saved classpaths/receipts must be resolved
through the known relocation map; regenerate moved CMake configure state.

### Dependency scopes and release preflight

Tests importing a production `compileOnly` library must declare their own
`testImplementation` dependency. Gson belongs in `compileOnly(libs.gson)`,
`testImplementation(libs.gson)` and the separate catalog generator configuration;
do not promote it to production `implementation` or `runtimeOnly` to fix tests.

### Tests and the release graph

CI runs no tests. `:patches:test` is a local end-of-task step: run it at the end of every
task that touches this repository and report the executed/skipped counts.

```powershell
.\gradlew.bat :patches:test                  # local gate, end of every task
.\gradlew.bat :patches:buildAndroid :patches:generatePatchesList -PreleaseChannel=experimental  # reproduce the CI/release graph
```

The release workflow builds the graph exactly once: `gradle-semantic-release-plugin` bumps
`gradle.properties`, `@semantic-release/exec` prepareCmd runs
`./gradlew clean generatePatchesList -PreleaseChannel=...` before any git tag exists, and a
push that publishes no release falls back to
`./gradlew :patches:buildAndroid :patches:generatePatchesList -PreleaseChannel=... clean`.
`:patches:buildAndroid` (jar + D8 into the `.mpp`) and `:patches:generatePatchesList` are the
end of that graph — keep it that way; do not re-add `dependsOn(build)` or any test task.
For dependency, plugin, generator or test changes, validate this same graph plus
`:patches:test` in an isolated checkout locally.
Attestation uses the exact release version through
`.github/scripts/resolve_release_artifact.cjs`, yielding a canonical absolute
path without `..`. Prefixing a relative pattern with the workspace is insufficient.
Keep release preparation's Gradle `clean` before rebuilding; this removes the
preflight version from upload candidates while preserving retained `build/` inputs.
Run `node .github/scripts/test_release_artifact_paths.cjs` after `npm ci` for its
actual `@actions/glob` regression checks. On Windows, configure GitHub Packages
with `tools/Configure-GitHubPackages.ps1` and a classic `read:packages` token in
user Gradle properties; tokens must stay outside source, chat and logs.
Use an isolated checkout for this clean command; retained inputs are now in
`../builds/steamlink-patches/build/`, separately from cleanable `gradle/` outputs.
Wrappers route project caches outside the checkout. Use `Verify-Build.ps1` for
scoped local test/Android checks rather than cleaning that mixed directory.

Manual compiler classpaths and desktop fat JARs can hide missing declarations and
use a different compiler. Never call their output a Gradle or CI pass. When local
plugin resolution blocks the gate, report that limitation and use the corrected
commit's GitHub run for authoritative build verification.

For workflow failures, identify the exact SHA/run/job and first failed task before
editing; verify that same corrected SHA afterward. Run `37129119119` failed in
`:patches:compileTestKotlin`: `PatchCategoriesTest` imported Gson without a test
dependency, while manual validation supplied `gson.jar`. Production compilation
and Android packaging passed, so changing the patcher/smali pins or skipping tests
would not fix that failure.

Before accepting new tests, check whether each file input is tracked (`git ls-files -- <path>`) or explicitly provisioned by `.github/workflows/release.yml`. CI never runs `:patches:test`, so it provisions no decoded APK and produces no test evidence; the local run is the only test proof. A local cached-compiler pass with ignored inputs present does not test a fresh checkout.

Keep tracked resource/hash checks, metadata tests and malformed-input rejection runnable without proprietary decoded APKs. For real-byte audits, use `org.junit.Assume.assumeTrue` with an explicit `BLOCKED` reason only when the exact decoded input is absent. Do not skip missing canonical payloads, invalid present inputs or assertion failures. Synthetic invalid bytes can prove rejection, never real-base compatibility.

Run fixture-dependent tests once with retained inputs and once from a fresh/isolated working directory where they are absent, using only tracked runtime resources and explicit classpaths. Check JUnit executed/skipped/failure counts. The 2026-10-01 failure in run `36907870366` was 5 `FovealCanvasPatchTest` failures caused by an ignored 5001812 scene; `buildAndroid` had already passed. Fix that input boundary rather than changing production guards or disabling the test task.

Report cached compilation, full Gradle/Android packaging, real APK audits and GitHub CI separately. Confirm the corrected commit's workflow result before claiming CI success.

Real apply-audits (`util.*MorpheAudit` runners — `FovealCanvasMorpheAudit`, and the 4XVR
`PremiumUnlockMorpheAudit`) execute the patcher end-to-end on a retained original APK and package an
unsigned output; they are the strongest pre-device evidence for a patch's edit surface. The runner's
classpath is a cached-compiler snapshot assembled by its `build-classpath.py`: after ANY patch-source
change re-run that script before the audit or the JVM loads stale classes (tell: a stack frame at
pre-edit line positions). Verify the output with `Tools/apk-tools/build-tools-35.0.1/android-15/dexdump.exe`;
dexdump renders references as `Lpkg/Class;.method:(args)ret` (dot separator), not smali's `->`. Under
STRIP_FAST packaging the modified classes move to a new dex (the output `classes.dex`), the originals
shift one slot, and the stripped original keeps its size with a different sha256 — check its class
list holds no leftover class_defs for the moved classes (no duplicate definitions).

Byte-pinned JSON/text resources need deterministic checkout bytes. Set an explicit `.gitattributes` rule (the foveal canvas manifest uses `text eol=lf`) and check `git ls-files --eol` plus the resource SHA-256 after checkout. Windows `core.autocrlf` can otherwise invalidate a correct Linux manifest pin. Preserve the canonical hash and installer guard; do not accept multiple line-ending hashes to hide the mismatch.

### Isolated `:patches:test` when the working tree is blocked

Another task's uncommitted refactor can break `:patches:test` compilation (for example tests still calling a removed patch object). Do not revert or stash their files; verify your change in a throwaway worktree instead:

1. `git worktree add --detach <EXTERNAL>/build/<name> HEAD` (`<EXTERNAL>` = `../builds/steamlink-patches`), then copy only your changed files in. Do not commit.
2. `settings.gradle.kts` resolves the external root relative to `settingsDir`, so a worktree nested deeper builds a second tree under `<worktree>/../builds/...`; run the build, then delete that derived tree and `git worktree remove --force`.
3. Run `JAVA_HOME=<jdk> ./gradlew.bat :patches:test --offline --console=plain`, then read executed/skipped/failure counts from the JUnit XML in that derived tree's `gradle/patches/test-results/test/*.xml` (`tests`/`skipped`/`failures`/`errors` attributes).
4. Report those counts separately from the in-place run. An in-place compile failure caused by foreign uncommitted edits is a blocker to state, never a result to hide.

### Attribution credit lines in patch descriptions

Credit text lives in one `internal const val <APP>_CREDIT` per app (4XVR: `FOUR_XVR_UPSTREAM_CREDIT` in `FourXvrDexKit.kt`; Steam Link: `STEAM_LINK_CONTROLLER_CREDIT` in `steamlink/androidxr/ControllerAngularVelocityFrame.kt`) and is appended as `"... " + CREDIT` to the description of every patch the credit covers; option descriptions never get it. Keep appended text free of phrases tests pin as sentinels (for example `not run on this base`), then run `:patches:generatePatchesList`, `python -B .github/scripts/generate_patches_readme.py <owner/repo> <branch> patches-list-all.json TECHNICAL_REFERENCE.md`, and `:patches:test`.

```powershell
.\gradlew.bat :patches:test            # local gate, run at the end of every task
.\gradlew.bat build                    # full build (every app)
.\gradlew.bat assembleExtension        # rebuild the Steam Link extension DEX only
.\gradlew.bat assembleFourXvrExtension # rebuild the 4XVR extension DEX only
.\gradlew.bat generatePatchesList      # regenerate the root patches-list.json catalogs
```

package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5002363
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5001712
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.shared.PatchCategories
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

// GXR spacewarp layer (09-LAYER-B1b): stages the bundled OpenXR implicit layer that
// synthesizes missed-tick projection frames. Provisional val name — the owner may
// rename at review; only the display name and description are user-visible.
internal const val GXR_SPACEWARP_LIBRARY = "libgxr_spacewarp.so"
internal const val GXR_SPACEWARP_MANIFEST = "XR_APILAYER_local_GalaxyXR_gxr_spacewarp.json"
internal const val GXR_SPACEWARP_GATE0 = "gxr_spacewarp_gate0_5002363.json"
internal const val GXR_SPACEWARP_GATE0_5001712 = "gxr_spacewarp_gate0_5001712.json"

// Builtin sha pins for the canonical staged payloads. The .so pin is filled by the
// owner build flow (Build-Native.ps1 output, staged, then pinned — extensions skill
// "Build output only"); the manifest + Gate-0 pins are committed with their files.
// The layer payload is base-agnostic (the .so's per-base profile table selects the
// scene-lib pins at runtime); only the two gate evidence JSONs are per base.
internal const val GXR_SPACEWARP_LIBRARY_SHA256 =
    "7fe4b20b6d9d6ce8c10fec40ceb93bd7b76d0acb84acfe21a8a6577b741536ab"
internal const val GXR_SPACEWARP_MANIFEST_SHA256 =
    "623f0f1b9c69d049cc9c3754d0481aa06b40529d322f3e233bafa12af840943b"
internal const val GXR_SPACEWARP_GATE0_SHA256 =
    "4acb20d818553cf6a625cdd983497fead739ffa0b21171fd7f63731a4384c3ca"
internal const val GXR_SPACEWARP_VTS_GATE = "gxr_spacewarp_vts_5002363.json"
internal const val GXR_SPACEWARP_VTS_GATE_SHA256 =
    "7d12e9485f29594e5ef299706d576e1e40ea15a326a45ce45f1210c78cc27e54"
internal const val GXR_SPACEWARP_GATE0_5001712_SHA256 =
    "2c7666bef9d6e970268ae31aec4fa932d7bc5811ac16625dbcdbd8b7685bc23c"
internal const val GXR_SPACEWARP_VTS_GATE_5001712 = "gxr_spacewarp_vts_5001712.json"
internal const val GXR_SPACEWARP_VTS_GATE_5001712_SHA256 =
    "183d352208a04e23ed636e065c6154b37d7d21c131b19e58b7f43cd621bd1aa9"

internal fun isGxrSpacewarpSteamLinkBuild(versionName: String, versionCode: String): Boolean =
    (versionName == "2.0.20" && versionCode == "5001712") ||
        (versionName == "2.0.23" && versionCode == "5002363")

private fun spacewarpSha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
    .digest(bytes).joinToString("") { "%02x".format(it) }

// Window-gated scene-library preconditions, PER BASE (RE-SHARED-IMPL.md "Known
// anchors" + brief 13, re-derived per base by
// extensions/gxr-spacewarp-layer/tests/derive_gate0.py --base <base> against
// android-steamlinkvr-release-base-<tree>; full-file sha stays a
// diagnostics-only value in the Gate-0 JSON, never a gate here). Each base's
// list is exactly its gate-0 JSON's anchor list in the same order: the runtime
// identity anchor (that base's codecs-support audit window) plus that base's
// brief-01 windows (AppTick/FlipFrame/Render). Anchors are pinned by the same
// values the layer itself re-checks at load time.
internal val gxrSpacewarpScenePreconditions: Map<String, List<Triple<Int, Int, String>>> = mapOf(
    "2.0.20/5001712" to listOf(
        Triple(0xfd304, 0x80, "6e5dcb4f5094763e948388ca35c6132af242159fc3c22e812f0fe2f4ab4f2649"),
        Triple(0x10ad18, 0x80, "82ee275b8e5d02083867d5e4a478d2bf80c6f516c7f186df05dbe4a7465100a9"),
        Triple(0x1190a4, 0x80, "4ebd60f23ef4b0f5957f749cb64d0ab68f4f70177a10ab437647767dd5924585"),
        Triple(0x13eb80, 0x80, "aac0333421512dfc5c28fc347d02013f60cedc1b23aabe5ee31f1b8d35ca12b3"),
    ),
    "2.0.23/5002363" to listOf(
        Triple(0xfd6fc, 0x80, "f5c43c6aacb69e61c3f929f361612e1b91bb509803d2c14824b9601b3dd8c795"),
        Triple(0x10cd00, 0x80, "21cfe100a3a057dc633ab0b4934d1aedaa2809278b320a66e93c79e494aff7ca"),
        Triple(0x11b708, 0x80, "daa07b02967fd5682a9add717ceb396a95549c7ffbb7cc38f5a4e3de43ac66aa"),
    ),
)

internal fun validateGxrSpacewarpScene(scene: ByteArray, versionName: String, versionCode: String) {
    val preconditions = gxrSpacewarpScenePreconditions["$versionName/$versionCode"]
    if (preconditions == null) {
        throw PatchException("GXR spacewarp has no scene contract for $versionName/$versionCode")
    }
    preconditions.forEach { (offset, size, expectedHash) ->
        if (offset + size > scene.size) {
            throw PatchException("GXR spacewarp found a truncated scene library at 0x${offset.toString(16)}")
        }
        if (spacewarpSha256(scene.copyOfRange(offset, offset + size)) != expectedHash) {
            throw PatchException("GXR spacewarp found an unknown scene layout at 0x${offset.toString(16)}")
        }
    }
}

// The per-base gate pair (brief 13's "SIMPLEST provable scheme", chose + documented):
// the exact versionCode selects the row; the row's sha pins validate the staged
// payloads, AND each gate JSON's own "versionCode" content marker must name that
// same base - a wrong pair cannot pass, because its content marker names a
// different versionCode than the selector row.
internal data class GxrSpacewarpGatePair(
    val gate0Name: String,
    val gate0Sha256: String,
    val vtsName: String,
    val vtsSha256: String,
)

internal val gxrSpacewarpGatePairs: Map<String, GxrSpacewarpGatePair> = mapOf(
    "5001712" to GxrSpacewarpGatePair(
        GXR_SPACEWARP_GATE0_5001712,
        GXR_SPACEWARP_GATE0_5001712_SHA256,
        GXR_SPACEWARP_VTS_GATE_5001712,
        GXR_SPACEWARP_VTS_GATE_5001712_SHA256,
    ),
    "5002363" to GxrSpacewarpGatePair(
        GXR_SPACEWARP_GATE0,
        GXR_SPACEWARP_GATE0_SHA256,
        GXR_SPACEWARP_VTS_GATE,
        GXR_SPACEWARP_VTS_GATE_SHA256,
    ),
)

internal fun validateGxrSpacewarpPayload(
    library: ByteArray,
    manifest: ByteArray,
    gate0: ByteArray,
    vts: ByteArray,
    versionName: String,
    versionCode: String,
) {
    if (library.size < 20 || !library.copyOfRange(0, 4)
            .contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46)) ||
        library[4] != 2.toByte() || library[5] != 1.toByte() ||
        library[18] != 0xb7.toByte() || library[19] != 0.toByte()
    ) {
        throw PatchException("Bundled GXR spacewarp library is not an ARM64 ELF shared object")
    }
    if (spacewarpSha256(library) != GXR_SPACEWARP_LIBRARY_SHA256) {
        throw PatchException("Bundled GXR spacewarp library failed its SHA-256 pin")
    }
    if (spacewarpSha256(manifest) != GXR_SPACEWARP_MANIFEST_SHA256) {
        throw PatchException("Bundled GXR spacewarp manifest failed its SHA-256 pin")
    }
    val pair = gxrSpacewarpGatePairs[versionCode]
    if (pair == null) {
        throw PatchException("GXR spacewarp has no gate evidence pair for $versionName/$versionCode")
    }
    if (spacewarpSha256(gate0) != pair.gate0Sha256) {
        throw PatchException("Bundled GXR spacewarp gate-0 evidence failed its SHA-256 pin")
    }
    if (spacewarpSha256(vts) != pair.vtsSha256) {
        throw PatchException("Bundled GXR spacewarp value-transport gate failed its SHA-256 pin")
    }
    // The content marker: each gate JSON names its own base versionCode. Both
    // files of the selected row must carry the selector's code.
    val marker = "\"versionCode\": ${versionCode.toInt()}"
    if (marker !in gate0.decodeToString()) {
        throw PatchException("Bundled GXR spacewarp gate-0 evidence names a different base than $versionName/$versionCode")
    }
    if (marker !in vts.decodeToString()) {
        throw PatchException("Bundled GXR spacewarp value-transport gate names a different base than $versionName/$versionCode")
    }
}

/** All layout, payload and existing-file preconditions run before any write. */
internal fun installGxrSpacewarpResources(
    apkRoot: File,
    versionName: String,
    versionCode: String,
    libraryBytes: ByteArray? = null,
    manifestBytes: ByteArray? = null,
    gate0Bytes: ByteArray? = null,
    vtsBytes: ByteArray? = null,
): Boolean {
    // Dependencies bypass Morphe compatibility. Excluded pairs must not even read files.
    if (!isGxrSpacewarpSteamLinkBuild(versionName, versionCode)) return false

    val sceneFile = File(apkRoot, "lib/arm64-v8a/libvrlink_scene.so")
    if (!sceneFile.isFile) throw PatchException("Missing exact GXR spacewarp scene library")
    validateGxrSpacewarpScene(sceneFile.readBytes(), versionName, versionCode)
    val pair = requireNotNull(gxrSpacewarpGatePairs[versionCode]) {
        "GXR spacewarp has no gate pair registered for $versionCode"
    }
    val library = libraryBytes ?: projectionModeResource(GXR_SPACEWARP_LIBRARY)
    val manifest = manifestBytes ?: projectionModeResource(GXR_SPACEWARP_MANIFEST)
    val gate0 = gate0Bytes ?: projectionModeResource(pair.gate0Name)
    val vts = vtsBytes ?: projectionModeResource(pair.vtsName)
    validateGxrSpacewarpPayload(library, manifest, gate0, vts, versionName, versionCode)

    // The .so mounts under lib/arm64-v8a beside the scene library; the manifest is
    // the loader's implicit.d layer entry; the gate-0 evidence is an inert asset
    // (NEVER under implicit.d — the loader parses every json there as a layer).
    val files = listOf(
        File(sceneFile.parentFile, GXR_SPACEWARP_LIBRARY) to library,
        File(apkRoot, "assets/openxr/1/api_layers/implicit.d/$GXR_SPACEWARP_MANIFEST") to manifest,
        File(apkRoot, "assets/gxr_spacewarp/${pair.gate0Name}") to gate0,
        File(apkRoot, "assets/gxr_spacewarp/${pair.vtsName}") to vts,
    )
    files.forEach { (target, expected) ->
        if (target.exists() && (!target.isFile || !target.readBytes().contentEquals(expected))) {
            throw PatchException("GXR spacewarp found a stale or modified resource: ${target.path}")
        }
    }
    val missing = files.filterNot { (target, _) -> target.exists() }
    if (missing.isEmpty()) return true

    val staged = mutableListOf<Pair<File, File>>()
    val created = mutableListOf<File>()
    try {
        missing.forEach { (target, bytes) ->
            if (!target.parentFile.isDirectory && !target.parentFile.mkdirs()) {
                throw PatchException("Could not create the GXR spacewarp resource directory")
            }
            val temporary = File.createTempFile("gxr-spacewarp-", ".tmp", target.parentFile)
            staged += temporary to target
            temporary.writeBytes(bytes)
        }
        staged.forEach { (temporary, target) ->
            // No replacement: an unexpected concurrent file must never be overwritten.
            Files.move(temporary.toPath(), target.toPath())
            created += target
        }
    } catch (failure: Exception) {
        created.asReversed().forEach { target ->
            try { Files.deleteIfExists(target.toPath()) } catch (rollback: Exception) {
                failure.addSuppressed(rollback)
            }
        }
        throw failure
    } finally {
        staged.forEach { (temporary, _) -> Files.deleteIfExists(temporary.toPath()) }
    }
    return true
}

// Default false: experimental, headset validation pending, and the recommended
// set must not gain a frame-synthesis layer automatically (RE-SHARED-IMPL.md:
// worst case = stock, and Manager selection stays manual for experiments).
// Second base (brief 13): the exact 2.0.20/5001712 pair joins the same
// experimental object (EXPERIMENTAL name for both, one global default); the
// guard/select logic stays the exact-pair check above.
@Suppress("unused")
val gxrSpacewarpLayerPatch = rawResourcePatch(
    name = "GXR spacewarp layer (experimental)",
    description = "Exact 2.0.20/5001712 and 2.0.23/5002363: synthesizes a projection " +
        "frame from the two most recent decoded frames when the video stream misses a " +
        "display tick, via a bundled OpenXR implicit layer gated on each base's pinned " +
        "scene-library layout (per-base pin profiles selected at runtime; the layer " +
        "itself re-verifies the base's identity anchor before any hook). The client's " +
        "composition layers stay untouched; any construction or layout failure stands " +
        "down to the stock renderer. Experimental: headset validation pending, so it is " +
        "not part of the recommended set.",
    default = false,
) {
    category(PatchCategories.STREAMING_AND_NETWORK)
    compatibleWith(
        *COMPATIBILITIES_STEAM_LINK_5002363.map { exact ->
            Compatibility(
                name = EXPERIMENTAL_COMPATIBILITY_NAME,
                packageName = requireNotNull(exact.packageName),
                targets = exact.targets,
            )
        }.toTypedArray(),
        *COMPATIBILITIES_STEAM_LINK_5001712.map { exact ->
            Compatibility(
                name = EXPERIMENTAL_COMPATIBILITY_NAME,
                packageName = requireNotNull(exact.packageName),
                targets = exact.targets,
            )
        }.toTypedArray(),
    )
    execute {
        // Morphe executes dependencies recursively without rechecking compatibility.
        // Keep this build guard inside the mutation itself so an excluded APK stays untouched.
        if (!isGxrSpacewarpSteamLinkBuild(packageMetadata.versionName, packageMetadata.versionCode)) {
            return@execute
        }
        val sceneFile = get("lib/arm64-v8a/libvrlink_scene.so")
        val apkRoot = sceneFile.parentFile!!.parentFile!!.parentFile!!
        installGxrSpacewarpResources(apkRoot, packageMetadata.versionName, packageMetadata.versionCode)
    }
}
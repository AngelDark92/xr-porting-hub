package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5001712
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.shared.PatchCategories
import app.template.patches.shared.buildLabel
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest

internal const val FOVEA_QUAD_MODE = "fovea_quad_layers_v1"
internal const val FOVEA_QUAD_LIBRARY = "libgxr_fovea_quad.so"
internal const val FOVEA_QUAD_MANIFEST =
    "XR_APILAYER_local_GalaxyXR_fovea_quad_v1.json"
internal const val FOVEA_QUAD_BUILD_ID = "gxr-fovea-quad-v1.0-20261025"
internal const val QUAD_ONLY_MODE = "quad_only_composition_v1"
internal const val QUAD_ONLY_LIBRARY = "libgxr_quad_only.so"
internal const val QUAD_ONLY_MANIFEST =
    "XR_APILAYER_local_GalaxyXR_quad_only_v1.json"
internal const val QUAD_ONLY_BUILD_ID = "gxr-quad-only-v1.0-20261025"

internal data class QuadLayerVariant(
    val mode: String,
    val library: String,
    val manifest: String,
    val buildId: String,
    val helperSha256: String,
    val manifestSha256: String,
    val patchName: String,
    val sentence: String,
)

internal val FOVEA_QUAD_VARIANT = QuadLayerVariant(
    mode = FOVEA_QUAD_MODE,
    library = FOVEA_QUAD_LIBRARY,
    manifest = FOVEA_QUAD_MANIFEST,
    buildId = FOVEA_QUAD_BUILD_ID,
    helperSha256 = "5004336d2dfd1efcbde0274bff32400b32e43954dac7c1a01785c6ebc4437253",
    manifestSha256 = "a38244e3e793e7856e022d838c0065f2681c743e9dc3eae263bb8869d66c904f",
    patchName = "Fovea quad layers (experimental)",
    sentence = "Replaces Steam Link's fovea projection with two per-eye quad layers while keeping the background projection.",
)

internal val QUAD_ONLY_VARIANT = QuadLayerVariant(
    mode = QUAD_ONLY_MODE,
    library = QUAD_ONLY_LIBRARY,
    manifest = QUAD_ONLY_MANIFEST,
    buildId = QUAD_ONLY_BUILD_ID,
    helperSha256 = "39078df8c18597e4188f2991d4a95dcede36ca53b6f135f0fab3d170709d15ea",
    manifestSha256 = "d0420eea19794b7179cef49934b152695ec8deb0ed72474f6e22544f86aefec2",
    patchName = "Quad-only composition (experimental)",
    sentence = "Submits Steam Link's background and fovea as four per-eye quad layers with no projection layers.",
)

internal val QUAD_LAYER_VARIANTS = listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT)

// Headset validation is pending, so both arms stay in the experimental catalog
// only. The exact target still comes from the shared 5001712 registry.
private val QUAD_LAYER_COMPATIBILITIES = COMPATIBILITIES_STEAM_LINK_5001712.map { exact ->
    Compatibility(
        name = EXPERIMENTAL_COMPATIBILITY_NAME,
        packageName = requireNotNull(exact.packageName),
        targets = exact.targets,
    )
}

internal fun isQuadLayerSteamLinkBuild(versionName: String, versionCode: String): Boolean =
    COMPATIBILITIES_STEAM_LINK_5001712.any { compatibility ->
        compatibility.targets.any { target ->
            target.version == versionName &&
                target.versionCodes.orEmpty().values.any { it.toString() == versionCode }
        }
    }

private fun quadLayerResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: throw PatchException("Missing bundled quad-layer resource: $name"))
        .use { it.readBytes() }

private fun sha256(bytes: ByteArray): String =
    MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { "%02x".format(it) }

internal fun validateQuadLayerPayload(
    variant: QuadLayerVariant,
    helper: ByteArray,
    manifest: ByteArray,
) {
    if (helper.size < 4 || !helper.copyOfRange(0, 4)
            .contentEquals(byteArrayOf(0x7f, 0x45, 0x4c, 0x46))) {
        throw PatchException("Bundled ${variant.patchName} helper is not an ELF library")
    }
    if (sha256(helper) != variant.helperSha256) {
        throw PatchException("Bundled ${variant.patchName} helper failed its SHA-256 precondition")
    }
    if (sha256(manifest) != variant.manifestSha256) {
        throw PatchException("Bundled ${variant.patchName} manifest failed its SHA-256 precondition")
    }
    val manifestText = manifest.toString(Charsets.UTF_8)
    if (!manifestText.contains("\"library_path\": \"${variant.library}\"")) {
        throw PatchException("Bundled ${variant.patchName} manifest names the wrong library")
    }
    val helperText = helper.toString(Charsets.ISO_8859_1)
    if (!helperText.contains(variant.mode) || !helperText.contains(variant.buildId)) {
        throw PatchException("Bundled ${variant.patchName} helper build identity is missing")
    }
}

internal fun installQuadLayerResources(
    apkRoot: File,
    versionName: String,
    versionCode: String,
    variant: QuadLayerVariant,
    helperBytes: ByteArray? = null,
    manifestBytes: ByteArray? = null,
): Boolean {
    // Dependencies bypass Morphe compatibility. Leave every other build untouched
    // before reading or writing target files.
    if (!isQuadLayerSteamLinkBuild(versionName, versionCode)) return false

    val sceneFile = File(apkRoot, "lib/arm64-v8a/libvrlink_scene.so")
    if (!sceneFile.isFile) {
        throw PatchException("Missing exact 2.0.20/5001712 scene library")
    }
    if (sceneFile.length() != 2_221_072L) {
        throw PatchException("${variant.patchName} requires the exact 2.0.20/5001712 scene size")
    }
    retiredNativeProjectionHook(sceneFile.readBytes())?.let { retired ->
        throw PatchException(
            "${variant.patchName} found retired renderer helper $retired; use a clean source APK",
        )
    }

    val libDir = sceneFile.parentFile!!
    val layerDir = File(apkRoot, "assets/openxr/1/api_layers/implicit.d")
    val helper = helperBytes ?: quadLayerResource(variant.library)
    val manifest = manifestBytes ?: quadLayerResource(variant.manifest)
    validateQuadLayerPayload(variant, helper, manifest)

    // The alpha-0 Android-Surface trigger and these quad experiments replace the
    // same submission topology; running both would stack two terminal strategies.
    if (File(libDir, ANDROID_SURFACE_TRIGGER_LIBRARY).exists() ||
        File(layerDir, ANDROID_SURFACE_TRIGGER_MANIFEST).exists()) {
        throw PatchException(
            "${variant.patchName} needs Alpha-0 high-resolution trigger deselected",
        )
    }
    QUAD_LAYER_VARIANTS.filterNot { it.mode == variant.mode }.forEach { other ->
        if (File(libDir, other.library).exists() || File(layerDir, other.manifest).exists()) {
            throw PatchException(
                "${variant.patchName} is mutually exclusive with ${other.patchName}",
            )
        }
    }

    val files = listOf(
        File(libDir, variant.library) to helper,
        File(layerDir, variant.manifest) to manifest,
    )
    files.forEach { (target, expected) ->
        if (target.exists() && (!target.isFile || !target.readBytes().contentEquals(expected))) {
            throw PatchException("${variant.patchName} found a stale or modified resource: ${target.path}")
        }
    }
    val missing = files.filterNot { (target, _) -> target.exists() }
    if (missing.isEmpty()) return true

    val staged = mutableListOf<Pair<File, File>>()
    val created = mutableListOf<File>()
    try {
        missing.forEach { (target, bytes) ->
            if (!target.parentFile.isDirectory && !target.parentFile.mkdirs()) {
                throw PatchException("Could not create ${variant.patchName} resource directory")
            }
            val temporary = File.createTempFile("gxr-quad-layer-", ".tmp", target.parentFile)
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

private fun quadLayerPatch(variant: QuadLayerVariant) = rawResourcePatch(
    name = variant.patchName,
    description = variant.sentence + " " + buildLabel(QUAD_LAYER_COMPATIBILITIES) +
        " Needs: deselect Alpha-0 high-resolution trigger.",
    default = false,
) {
    category(PatchCategories.IMAGE_QUALITY)
    compatibleWith(*QUAD_LAYER_COMPATIBILITIES.toTypedArray())

    execute {
        if (packageMetadata.packageName != "com.valvesoftware.steamlinkvr" ||
            !isQuadLayerSteamLinkBuild(packageMetadata.versionName, packageMetadata.versionCode)) {
            return@execute
        }
        val sceneFile = get("lib/arm64-v8a/libvrlink_scene.so")
        val apkRoot = sceneFile.parentFile!!.parentFile!!.parentFile!!
        installQuadLayerResources(
            apkRoot,
            packageMetadata.versionName,
            packageMetadata.versionCode,
            variant,
        )
    }
}

@Suppress("unused")
val foveaQuadLayersPatch = quadLayerPatch(FOVEA_QUAD_VARIANT)

@Suppress("unused")
val quadOnlyCompositionPatch = quadLayerPatch(QUAD_ONLY_VARIANT)

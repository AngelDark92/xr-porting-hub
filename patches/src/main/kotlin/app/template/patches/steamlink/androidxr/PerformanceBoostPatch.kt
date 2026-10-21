package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK
import app.template.patches.shared.PatchCategories

internal const val PERFORMANCE_BOOST_LIBRARY = "libgxr_performance_boost.so"
internal const val PERFORMANCE_BOOST_MANIFEST = "XR_APILAYER_local_GalaxyXR_performance_boost.json"
private val recommendedPerformanceBoostBuilds = setOf(
    "2.0.20" to 5001712, "2.0.23" to 5002363,
)
private val recommendedPerformanceBoostCompatibilities = COMPATIBILITIES_STEAM_LINK.filter { compatibility ->
    compatibility.targets.any { target ->
        target.versionCodes?.values?.any { (target.version to it) in recommendedPerformanceBoostBuilds } == true
    }
}

private fun matchesPerformanceBoostTarget(
    compatibilities: List<Compatibility>, packageName: String, version: String, code: String,
): Boolean = compatibilities.any { compatibility ->
    compatibility.packageName == packageName && compatibility.targets.any { target ->
        target.version == version && target.versionCodes?.values?.any { it.toString() == code } == true
    }
}
internal fun isPerformanceBoostTarget(packageName: String, version: String, code: String): Boolean =
    matchesPerformanceBoostTarget(COMPATIBILITIES_STEAM_LINK, packageName, version, code)
internal fun isRecommendedPerformanceBoostTarget(packageName: String, version: String, code: String): Boolean =
    matchesPerformanceBoostTarget(recommendedPerformanceBoostCompatibilities, packageName, version, code)
internal fun performanceBoostResource(name: String): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/$name")
        ?: throw PatchException("Missing performance boost resource: $name")).use { it.readBytes() }

private fun createPerformanceBoostPatch(
    name: String,
    default: Boolean,
    compatibilities: List<Compatibility>,
) = rawResourcePatch(
    name = name,
    description = "Asks OpenXR for the CPU and GPU BOOST hint at each session start; the headset still controls clocks " +
        "and thermals, and unsupported ones continue without BOOST. No root needed; performance gain not yet measured." +
        " Details: https://github.com/4uva4ek/steamlink-patches.",
    default = default,
) {
    category(PatchCategories.APP_AND_DEVICE_IDENTITY)
    compatibleWith(*compatibilities.toTypedArray())
    execute {
        if (!matchesPerformanceBoostTarget(compatibilities, packageMetadata.packageName,
                packageMetadata.versionName, packageMetadata.versionCode)) return@execute
        if (!get("lib/arm64-v8a/libopenxr_loader.so").isFile) {
            throw PatchException("OpenXR BOOST requires the ARM64 Steam Link OpenXR loader")
        }
        val payload = performanceBoostResource(PERFORMANCE_BOOST_LIBRARY)
        val manifest = performanceBoostResource(PERFORMANCE_BOOST_MANIFEST)
        val libraryFile = get("lib/arm64-v8a/$PERFORMANCE_BOOST_LIBRARY")
        val manifestFile = get("assets/openxr/1/api_layers/implicit.d/$PERFORMANCE_BOOST_MANIFEST")
        libraryFile.parentFile!!.mkdirs(); manifestFile.parentFile!!.mkdirs()
        libraryFile.writeBytes(payload); manifestFile.writeBytes(manifest)
    }
}

@Suppress("unused")
val performanceBoostPatch = createPerformanceBoostPatch(
    name = "OpenXR CPU and GPU BOOST",
    default = true,
    compatibilities = recommendedPerformanceBoostCompatibilities,
)

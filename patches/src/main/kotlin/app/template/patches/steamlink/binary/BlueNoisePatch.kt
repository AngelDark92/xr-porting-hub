package app.template.patches.steamlink.binary

import app.template.patches.shared.PatchCategories
import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.booleanOption
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK

/** One selectable patch owning both blue-noise layers. The two former per-layer patches and
 * their declared-depth option merged: 8-bit and 10-bit input share the same final 8-bit sRGB
 * quantizer, so no runtime or declared depth exists. Recommended and default-enabled on every
 * exact Steam Link base: the foveal layer is on by default, the background layer is opt-in,
 * and the helper rebuilds owned configurations from the final source, order-independently. */
@Suppress("unused")
val blueNoisePatch = rawResourcePatch(
    name = "Blue-noise dithering",
    description = "Adds static blue noise after video colour processing and fade, independently " +
        "selectable on the foveal and the background/base layer; 8-bit or 10-bit input, always " +
        "8-bit sRGB output. Recommended on all exact bases with the foveal layer on by default. " +
        "Turn OLED's VD-like fovea toggle off when foveal blue noise is used. Unknown host " +
        "shaders pass through unchanged.",
    default = true,
) {
    category(PatchCategories.IMAGE_QUALITY)
    compatibleWith(*COMPATIBILITIES_STEAM_LINK.toTypedArray())

    val fovea by booleanOption(
        key = "fovea",
        default = true,
        title = "Foveal layer",
        description = "Static blue noise on the foveal (masked) layer only. The foveal draw caller " +
            "and the 8-bit sRGB framebuffer state are verified per draw; requires the exact stock " +
            "shader or an OLED calibration with its VD-like fovea toggle off.",
        required = true,
    )

    val background by booleanOption(
        key = "background",
        default = false,
        title = "Background layer",
        description = "Static blue noise on the complete base projection, including the part " +
            "underneath the foveal overlay; the foveal program is never dithered by this selection.",
        required = true,
    )

    // Morphe runs every execute before any finalize. Hash the final shader bytes after an
    // optional OLED calibration execute, without forcing calibration as a dependency.
    finalize {
        if (BLUE_NOISE_LAYOUTS.none { it.version == packageMetadata.versionName && it.code == packageMetadata.versionCode }) return@finalize
        if (fovea != true && background != true) return@finalize
        val file = get("lib/arm64-v8a/libvrlink_scene.so")
        val formatted = setProjectionSwapchainFormat(
            file.readBytes(), VideoOutputPrecision.SRGB8_HIGHP,
            packageMetadata.versionName, packageMetadata.versionCode,
        )
        val destination = get("lib/arm64-v8a/libgxd.so")
        val existing = if (destination.isFile) destination.readBytes() else null
        var scene = formatted
        var helper: ByteArray? = existing
        // Fixed fovea-then-background order; each enabled layer rebuilds the owned configuration
        // from the final source so selection order never changes bytes.
        for (layer in BlueNoiseLayer.entries) {
            val mode = when {
                layer == BlueNoiseLayer.FOVEA && fovea == true -> FoveaMode.INPUT_10BIT
                layer == BlueNoiseLayer.BACKGROUND && background == true -> FoveaMode.INPUT_10BIT
                else -> FoveaMode.OFF
            }
            val result = applyBlueNoiseLayer(scene, packageMetadata.versionName,
                packageMetadata.versionCode, mode, layer, existingHelper = helper)
            scene = result.scene
            helper = result.helper
        }
        val prepared = helper ?: throw PatchException("Blue-noise payload was not prepared")
        destination.parentFile.mkdirs()
        destination.writeBytes(prepared)
        file.writeBytes(scene)
    }
}

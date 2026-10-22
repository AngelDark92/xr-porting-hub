package app.template.patches.steamlink.identity

import app.morphe.patcher.patch.rawResourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_LEGACY
import app.template.patches.shared.Constants.isLegacyXrFoundationSteamLinkBuild
import app.template.patches.shared.PatchCategories
import app.template.patches.shared.buildLabel
import app.template.patches.steamlink.androidxr.adaptLegacyHmdConfigForBuild
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch

private fun galaxyIdentityResource(): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/hmd_config.json")
        ?: error("Missing bundled resource: steamlink/androidxr/hmd_config.json"))
        .use { it.readBytes() }

/**
 * Installs the Galaxy XR transport identity on the legacy Steam Link bases.
 *
 * Valve's native 2.0.23 build already reports its own Android XR identity, so this patch is
 * deliberately legacy-only. Its body keeps an exact-pair guard because Galaxy XR SteamVR driver
 * force-runs its dependencies without a compatibility re-check.
 */
@Suppress("unused")
val deviceIdentityPatch = rawResourcePatch(
    name = "Device identity",
    description = "Reports the Galaxy XR headset identity to SteamVR (model xrvst2ue). " +
        buildLabel(COMPATIBILITIES_STEAM_LINK_LEGACY),
    default = true,
) {
    category(PatchCategories.APP_AND_DEVICE_IDENTITY)
    compatibleWith(*COMPATIBILITIES_STEAM_LINK_LEGACY.toTypedArray())
    dependsOn(xrDeviceConfigBaselinePatch)

    execute {
        if (!isLegacyXrFoundationSteamLinkBuild(packageMetadata.versionName, packageMetadata.versionCode)) {
            return@execute
        }
        val file = get("assets/config/hmd_config.json")
        // 2.0.20/5001712 uses a headset-keyed requestedExtensions object; the other legacy bases
        // keep the array shape already present in the bundled payload.
        file.writeBytes(
            adaptLegacyHmdConfigForBuild(
                galaxyIdentityResource(),
                packageMetadata.versionName,
                packageMetadata.versionCode,
            ),
        )
    }
}

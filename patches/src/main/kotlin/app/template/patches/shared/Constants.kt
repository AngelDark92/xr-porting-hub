package app.template.patches.shared

import app.morphe.patcher.patch.ApkFileType
import app.morphe.patcher.patch.AppTarget
import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.SupportedAbi

object Constants {
    const val EXPERIMENTAL_COMPATIBILITY_NAME = "Steam Link Experimental"

    private const val STEAM_LINK_PACKAGE = "com.valvesoftware.steamlinkvr"
    private data class SteamLinkBuild(val version: String, val versionCode: Int)

    private val LEGACY_STEAM_LINK_BUILDS = listOf(
        SteamLinkBuild("2.0.20", 5001712),
        SteamLinkBuild("2.0.20", 5001812),
        SteamLinkBuild("2.0.21", 5001968),
        SteamLinkBuild("2.0.22", 5002244),
    )
    private val NATIVE_XR_STEAM_LINK_BUILDS = listOf(
        SteamLinkBuild("2.0.23", 5002363),
    )
    private val FULL_FACEBRIDGE_STEAM_LINK_BUILDS = LEGACY_STEAM_LINK_BUILDS
    private val MODERN_TONGUE_BRIDGE_STEAM_LINK_BUILDS = listOf(
        SteamLinkBuild("2.0.23", 5002363),
    )
    private val HIGH_RESOLUTION_STEAM_LINK_BUILDS = listOf(
        SteamLinkBuild("2.0.20", 5001712),
        SteamLinkBuild("2.0.20", 5001812),
        SteamLinkBuild("2.0.21", 5001968),
        SteamLinkBuild("2.0.22", 5002244),
        SteamLinkBuild("2.0.23", 5002363),
    )
    private val TWO_PROJECTION_STEAM_LINK_BUILDS = listOf(
        SteamLinkBuild("2.0.20", 5001712),
        SteamLinkBuild("2.0.20", 5001812),
        SteamLinkBuild("2.0.21", 5001968),
    )
    private val LEGACY_RECOMMENDED_STEAM_LINK_BUILDS = listOf(
        SteamLinkBuild("2.0.22", 5002244),
    )
    private val LEGACY_XR_FOUNDATION_STEAM_LINK_BUILDS = LEGACY_STEAM_LINK_BUILDS

    // The exact early-build bundles are separate from the shared legacy bundle, but all use the
    // legacy recommendation defaults. Match exact pairs, not a numeric build cutoff.
    fun isLegacyRecommendedSteamLinkBuild(version: String, versionCode: String): Boolean =
        isTwoProjectionSteamLinkBuild(version, versionCode) ||
            LEGACY_RECOMMENDED_STEAM_LINK_BUILDS.any {
                it.version == version && it.versionCode.toString() == versionCode
            }

    fun isTwoProjectionSteamLinkBuild(version: String, versionCode: String): Boolean =
        TWO_PROJECTION_STEAM_LINK_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    fun isMouseOnlySteamLinkBuild(version: String, versionCode: String): Boolean =
        isTwoProjectionSteamLinkBuild(version, versionCode)

    // Exact known startup adaptations for the legacy bases.
    fun isEarlierStartupSteamLinkBuild(version: String, versionCode: String): Boolean =
        LEGACY_XR_FOUNDATION_STEAM_LINK_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    val COMPATIBILITIES_STEAM_LINK_EARLIER_STARTUP =
        LEGACY_XR_FOUNDATION_STEAM_LINK_BUILDS.map(::steamLinkBuildCompatibility)

    fun isNativeXrSteamLinkBuild(version: String, versionCode: String): Boolean =
        NATIVE_XR_STEAM_LINK_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    fun isFullFacebridgeSteamLinkBuild(version: String, versionCode: String): Boolean =
        FULL_FACEBRIDGE_STEAM_LINK_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    fun isModernTongueBridgeSteamLinkBuild(version: String, versionCode: String): Boolean =
        MODERN_TONGUE_BRIDGE_STEAM_LINK_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    fun isHighResolutionSteamLinkBuild(version: String, versionCode: String): Boolean =
        HIGH_RESOLUTION_STEAM_LINK_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    fun isLegacyXrFoundationSteamLinkBuild(version: String, versionCode: String): Boolean =
        LEGACY_XR_FOUNDATION_STEAM_LINK_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    private fun steamLinkBuildCompatibility(
        build: SteamLinkBuild,
        name: String = "Steam Link",
        description: String = if (build in listOf(
                SteamLinkBuild("2.0.20", 5001812),
                SteamLinkBuild("2.0.21", 5001968),
                SteamLinkBuild("2.0.23", 5002363),
            )) {
            "Native and APK adaptation for exact Steam Link ${build.version} build ${build.versionCode}; headset validation pending."
        } else {
            "Verified Steam Link ${build.version} build ${build.versionCode}."
        },
    ) = Compatibility(
        name = name,
        packageName = STEAM_LINK_PACKAGE,
        apkFileType = ApkFileType.APK,
        appIconColor = 0x1B2838,
        targets = listOf(
            AppTarget(
                version = build.version,
                versionCodes = SupportedAbi.entries.associateWith { build.versionCode },
                description = description,
            ),
        ),
    )

    val COMPATIBILITIES_STEAM_LINK_LEGACY =
        LEGACY_STEAM_LINK_BUILDS.map(::steamLinkBuildCompatibility)

    val COMPATIBILITIES_STEAM_LINK_NATIVE_XR =
        NATIVE_XR_STEAM_LINK_BUILDS.map { build ->
            steamLinkBuildCompatibility(
                build = build,
                description = if (build in MODERN_TONGUE_BRIDGE_STEAM_LINK_BUILDS) {
                    "Build ${build.versionCode} recommends Galaxy XR high-resolution 3-projection fix, GXR face and tongue bridge, " +
                        "Microphone input preset (Voice Recognition), Unrestricted battery usage, " +
                        "Visual Delay Fix (60 ms), OLED color calibration with the Final balanced tested profile, " +
                        "Blue-noise dithering with the default foveal layer, and Network Optimizations. " +
                        "The retired projection experiments are excluded. " +
                        if (build.versionCode == 5002363) "Decoded-base adaptation; headset validation pending." else ""
                } else {
                    "Build ${build.versionCode} recommends its native-Android-XR-safe set: Galaxy XR " +
                        "high-resolution 3-projection fix, Device identity, Microphone input preset, OLED color " +
                        "calibration, GXR face and tongue bridge, Visual Delay Fix, and Unrestricted battery usage. " +
                        "Appear on top remains an optional legacy fallback."
                },
            )
        }

    val COMPATIBILITIES_STEAM_LINK =
        COMPATIBILITIES_STEAM_LINK_LEGACY + COMPATIBILITIES_STEAM_LINK_NATIVE_XR

    // Historical legacy-only scope: adding a newer native base must not re-enable these patches.
    val COMPATIBILITIES_STEAM_LINK_BEFORE_LATEST =
        COMPATIBILITIES_STEAM_LINK.filterNot { compatibility ->
            compatibility.targets.any { target ->
                MODERN_TONGUE_BRIDGE_STEAM_LINK_BUILDS.any { build ->
                    target.version == build.version && target.versionCodes?.values?.contains(build.versionCode) == true
                }
            }
        }

    // Derived from the same build list the guards use (isFullFacebridgeSteamLinkBuild), so
    // compatibility and mutation guards cannot diverge when bases are added.
    val COMPATIBILITIES_STEAM_LINK_FULL_FACEBRIDGE =
        FULL_FACEBRIDGE_STEAM_LINK_BUILDS.map(::steamLinkBuildCompatibility)

    val COMPATIBILITIES_STEAM_LINK_MODERN_TONGUE_BRIDGE =
        MODERN_TONGUE_BRIDGE_STEAM_LINK_BUILDS.map(::steamLinkBuildCompatibility)

    val COMPATIBILITIES_STEAM_LINK_5002363 =
        NATIVE_XR_STEAM_LINK_BUILDS.filter { it == SteamLinkBuild("2.0.23", 5002363) }
            .map(::steamLinkBuildCompatibility)

    val COMPATIBILITIES_STEAM_LINK_5001712 =
        COMPATIBILITIES_STEAM_LINK_LEGACY.filter { compatibility ->
            compatibility.targets.any { target ->
                target.version == "2.0.20" && target.versionCodes?.values?.contains(5001712) == true
            }
        }

    val COMPATIBILITIES_STEAM_LINK_5001812 =
        COMPATIBILITIES_STEAM_LINK_LEGACY.filter { compatibility ->
            compatibility.targets.any { target ->
                target.version == "2.0.20" && target.versionCodes?.values?.contains(5001812) == true
            }
        }

    val COMPATIBILITIES_STEAM_LINK_5001968 =
        COMPATIBILITIES_STEAM_LINK_LEGACY.filter { compatibility ->
            compatibility.targets.any { target ->
                target.version == "2.0.21" && target.versionCodes?.values?.contains(5001968) == true
            }
        }

    val COMPATIBILITIES_STEAM_LINK_LEGACY_RECOMMENDED =
        LEGACY_RECOMMENDED_STEAM_LINK_BUILDS.map(::steamLinkBuildCompatibility)

    val COMPATIBILITIES_STEAM_LINK_HIGH_RESOLUTION =
        HIGH_RESOLUTION_STEAM_LINK_BUILDS.map { build ->
            steamLinkBuildCompatibility(
                build = build,
                description = when (build.versionCode) {
                    5001712 -> "Exact Steam Link 2.0.20/5001712 high-resolution target with its isolated " +
                        "2-projection to 3-layer payload. The topology correction has prior user-reported " +
                        "startup and delayed-frame runtime evidence; this rebuilt binary remains uninstalled."
                    else -> "Static decoded-base adaptation of the Galaxy XR high-resolution patch for exact " +
                        "Steam Link ${build.version} build ${build.versionCode}; headset validation pending."
                },
            )
        }

    private fun experimentalCompatibilities(builds: List<SteamLinkBuild>) =
        builds.map { build ->
            steamLinkBuildCompatibility(
                build = build,
                name = EXPERIMENTAL_COMPATIBILITY_NAME,
                description = "Experimental patches for Steam Link ${build.version} build ${build.versionCode}.",
            )
        }

    val COMPATIBILITIES_STEAM_LINK_EXPERIMENTAL =
        experimentalCompatibilities(LEGACY_STEAM_LINK_BUILDS + NATIVE_XR_STEAM_LINK_BUILDS)

    val COMPATIBILITIES_STEAM_LINK_LEGACY_EXPERIMENTAL = experimentalCompatibilities(LEGACY_STEAM_LINK_BUILDS)

    val COMPATIBILITIES_STEAM_LINK_NATIVE_XR_EXPERIMENTAL = experimentalCompatibilities(NATIVE_XR_STEAM_LINK_BUILDS)

    // ---------------------------------------------------------------------
    // 4XVR (cn.vr4p.oculus4xvrplayerov) — exact single-build Galaxy XR adaptation.
    // ---------------------------------------------------------------------

    private const val FOUR_XVR_PACKAGE = "cn.vr4p.oculus4xvrplayerov"
    private data class FourXvrBuild(val version: String, val versionCode: Int)

    private val FOUR_XVR_BUILDS = listOf(
        FourXvrBuild("2.0.22", 20022),
    )

    fun isFourXvr20022Build(version: String, versionCode: String): Boolean =
        FOUR_XVR_BUILDS.any {
            it.version == version && it.versionCode.toString() == versionCode
        }

    private fun fourXvrBuildCompatibility(build: FourXvrBuild) = Compatibility(
        name = "4XVR",
        packageName = FOUR_XVR_PACKAGE,
        apkFileType = ApkFileType.APK,
        appIconColor = 0x6C4BB6,
        targets = listOf(
            AppTarget(
                version = build.version,
                versionCodes = SupportedAbi.entries.associateWith { build.versionCode },
                description = "4XVR Galaxy XR adaptation for exact build ${build.versionCode}; " +
                    "decoded-base design, headset validation pending.",
            ),
        ),
    )

    val COMPATIBILITIES_FOUR_XVR = FOUR_XVR_BUILDS.map(::fourXvrBuildCompatibility)
}

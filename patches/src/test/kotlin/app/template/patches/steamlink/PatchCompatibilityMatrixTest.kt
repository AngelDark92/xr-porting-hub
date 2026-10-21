package app.template.patches.steamlink

import app.morphe.patcher.patch.Patch
import app.template.patches.steamlink.androidxr.appearOnTopPatch
import app.template.patches.steamlink.androidxr.androidXrUiExtensionPatch
import app.template.patches.steamlink.androidxr.gxrFacebridgePatch
import app.template.patches.steamlink.androidxr.unrestrictedBatteryUsagePatch
import app.template.patches.steamlink.androidxr.performanceBoostPatch
import app.template.patches.steamlink.androidxr.xrCodecsSupportPatch
import app.template.patches.steamlink.androidxr.xrCoreRuntimePatch
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch
import app.template.patches.steamlink.androidxr.xrDirectInputFixPatch
import app.template.patches.steamlink.androidxr.xrGalaxyXrHighResolutionPatch
import app.template.patches.steamlink.androidxr.xrInputRoutingConfigPatch
import app.template.patches.steamlink.androidxr.xrLauncherBootstrapPatch
import app.template.patches.steamlink.androidxr.xrManifestCapabilityPackPatch
import app.template.patches.steamlink.androidxr.xrPermissionSettingsBootstrapPatch
import app.template.patches.steamlink.androidxr.xrStartupPermissionsPatch
import app.template.patches.steamlink.binary.androidXrNativePermissionNamesPatch
import app.template.patches.steamlink.binary.blueNoisePatch
import app.template.patches.steamlink.binary.forceHmdInitializationGatesPatch
import app.template.patches.steamlink.binary.forceLobbyPermissionStateGatePatch
import app.template.patches.steamlink.binary.forceStreamXrGatesPatch
import app.template.patches.steamlink.binary.hmdOnlyPatch
import app.template.patches.steamlink.binary.microphoneInputPresetPatch
import app.template.patches.steamlink.binary.networkOptimizationsPatch
import app.template.patches.steamlink.binary.oledCalibrationPatch
import app.template.patches.steamlink.identity.changePackageNamePatch
import app.template.patches.steamlink.identity.deviceIdentityPatch
import app.template.patches.steamlink.identity.deviceIdentityNativeXrPatch
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class PatchCompatibilityMatrixTest {
    
    @Test
    fun individual_patches_are_recommended_on_supported_builds() {
        assertEquals(false, oledCalibrationPatch.options["foveaVdLike"].default)
        assertEquals(
            legacyRecommendedPatches5001712,
            recommendedFor("2.0.20", 5001712).toSet(),
            "2.0.20/5001712",
        )
        assertEquals(
            nativeRecommendedPatches,
            recommendedFor("2.0.23", 5002363).toSet(),
            "2.0.23/5002363",
        )
        assertEquals("Appear on top (legacy)", appearOnTopPatch.name)
        assertEquals("GXR face and tongue bridge", gxrFacebridgePatch.name)
    }

    @Test
    fun exact_builds_select_the_recommended_patch_set() {
        assertEquals(
            legacyRecommendedPatches5001712,
            recommendedFor("2.0.20", 5001712).toSet(),
        )
        assertEquals(
            nativeRecommendedPatches,
            recommendedFor("2.0.23", 5002363).toSet(),
        )
        listOf(
            "2.0.20" to 5001740,
            "2.0.20" to 5001813,
            "2.0.20" to 5002363,
            "2.0.23" to 5001712,
            "2.0.21" to 5001969,
            "2.0.20" to 5002322,
            "2.0.22" to 5002243,
            "2.0.22" to 5002296,
            "2.0.22" to 5002313,
            "2.0.22" to 5002318,
            "2.0.22" to 5002322,
            "2.0.22" to 5002363,
            "2.0.23" to 5002322,
            "2.0.23" to 5002364,
        ).forEach { (version, versionCode) ->
            assertTrue(recommendedFor(version, versionCode).isEmpty(), "$version/$versionCode")
        }
    }

    @Test
    fun modern_recommended_set_contains_the_requested_ten_patches() {
        assertEquals(
            setOf(
                xrGalaxyXrHighResolutionPatch,
                gxrFacebridgePatch,
                microphoneInputPresetPatch,
                networkOptimizationsPatch,
                performanceBoostPatch,
                xrCodecsSupportPatch,
                blueNoisePatch,
                unrestrictedBatteryUsagePatch,
                hmdOnlyPatch,
                oledCalibrationPatch,
            ),
            nativeRecommendedPatches,
        )
        assertTrue(nativeRecommendedPatches.all { it.default })
        val modernClosure = nativeRecommendedPatches.flatMap { it.dependencyClosure() }.toSet()
        listOf(
            forceHmdInitializationGatesPatch,
            forceLobbyPermissionStateGatePatch,
            forceStreamXrGatesPatch,
            xrLauncherBootstrapPatch,
            xrStartupPermissionsPatch,
            deviceIdentityPatch,
        ).forEach { patch ->
            assertFalse(patch in modernClosure, patch.name)
        }
        assertTrue(oledCalibrationPatch.supports("2.0.23", 5002363))
        assertEquals("voice-recognition", microphoneInputPresetPatch.options["preset"].default)
        assertEquals(60, hmdOnlyPatch.options["offsetMs"].default)
        assertEquals("final-balanced", oledCalibrationPatch.options["profile"].default)
        assertEquals(false, oledCalibrationPatch.options["foveaVdLike"].default)
        assertEquals(1.00f, oledCalibrationPatch.options["fovealGamma"].default)
        assertTrue(networkOptimizationsPatch.default)
        assertEquals("Network Optimizations", networkOptimizationsPatch.name)
        assertFalse(deviceIdentityPatch.supports("2.0.23", 5002363))
        assertTrue(deviceIdentityNativeXrPatch.supports("2.0.23", 5002363))
        assertFalse(deviceIdentityPatch.supports("2.0.22", 5002322))
        assertTrue(deviceIdentityPatch.default)
        assertFalse(deviceIdentityNativeXrPatch.default)
        assertTrue(gxrFacebridgePatch.supports("2.0.23", 5002363))
        assertFalse(gxrFacebridgePatch.supports("2.0.22", 5002322))
    }

    @Test
    fun new_native_base_exposes_eleven_individual_patches_and_no_legacy_mutations() {
        val expected = setOf(
            xrGalaxyXrHighResolutionPatch,
            gxrFacebridgePatch,
            microphoneInputPresetPatch,
            networkOptimizationsPatch,
            blueNoisePatch,
            xrCodecsSupportPatch,
            performanceBoostPatch,
            unrestrictedBatteryUsagePatch,
            hmdOnlyPatch,
            oledCalibrationPatch,
            deviceIdentityNativeXrPatch,
        )
        assertEquals(expected, allIndividualPatches.filter { it.supports("2.0.23", 5002363) }.toSet())
        listOf("2.0.22" to 5002322, "2.0.22" to 5002363, "2.0.23" to 5002322, "2.0.23" to 5002364)
            .forEach { (version, versionCode) ->
                allIndividualPatches.forEach { patch ->
                    assertFalse(patch.supports(version, versionCode), "${patch.name}: $version/$versionCode")
                }
            }
        val excludedPublicPatches = allIndividualPatches.toSet() - expected
        assertEquals(13, excludedPublicPatches.size)
        val modernClosure = nativeRecommendedPatches.flatMap { it.dependencyClosure() }.toSet()
        // Identity is optional on the native build (not in the recommended set), so the recommended
        // closure does not reach the excluded legacy XR baseline chain.
        assertTrue(modernClosure.intersect(excludedPublicPatches).isEmpty())
    }

    @Test
    fun all_legacy_recommended_patches_preserve_startup_behavior_through_explicit_patches() {
        val expected = setOf(
            androidXrNativePermissionNamesPatch,
            forceHmdInitializationGatesPatch,
            forceLobbyPermissionStateGatePatch,
            forceStreamXrGatesPatch,
            gxrFacebridgePatch,
            xrGalaxyXrHighResolutionPatch,
            microphoneInputPresetPatch,
            oledCalibrationPatch,
            unrestrictedBatteryUsagePatch,
            hmdOnlyPatch,
            xrCoreRuntimePatch,
            xrDeviceConfigBaselinePatch,
            xrInputRoutingConfigPatch,
            xrLauncherBootstrapPatch,
            xrStartupPermissionsPatch,
            xrManifestCapabilityPackPatch,
            deviceIdentityPatch, blueNoisePatch,
        )
        assertEquals(legacyRecommendedPatches, expected)
        val closure = legacyRecommendedPatches.flatMap { it.dependencyClosure() }.toSet()
        listOf(
            appearOnTopPatch, changePackageNamePatch, blueNoisePatch,
        )
            .forEach { assertFalse(it in closure, "recommended selection unexpectedly includes ${it.name}") }
        assertTrue(forceHmdInitializationGatesPatch.default)
        assertTrue(forceLobbyPermissionStateGatePatch.default)
        assertTrue(forceStreamXrGatesPatch.default)
        assertFalse(changePackageNamePatch.default)
        assertFalse(appearOnTopPatch.default)
    }

    @Test
    fun `legacy base preserves standalone patches and optional defaults`() {
        val completeCatalog = allIndividualPatches
        val reference = completeCatalog.filter { it.supports("2.0.20", 5001712) }.toSet()
        assertEquals(22, reference.size)
        assertEquals(
            legacyRecommendedPatches5001712,
            recommendedFor("2.0.20", 5001712).toSet(),
        )
        recommendedFor("2.0.20", 5001712).forEach { assertTrue(it.default, it.name) }
        (reference - legacyRecommendedPatches5001712).forEach { patch ->
            assertFalse(patch.default, patch.name)
        }
        listOf(
            "2.0.20" to 5002363,
            "2.0.23" to 5001712,
            "2.0.20" to 5001740,
            "2.0.23" to 5002364,
        ).forEach { (version, code) ->
            completeCatalog.forEach { patch ->
                assertFalse(patch.supports(version, code), "${patch.name}: $version/$code")
            }
        }
    }

    @Test
    fun previous_verified_compatibility_is_preserved() {
        assertTrue(xrGalaxyXrHighResolutionPatch.supports("2.0.20", 5001712))
        assertTrue(xrGalaxyXrHighResolutionPatch.supports("2.0.23", 5002363))
        assertFalse(xrGalaxyXrHighResolutionPatch.supports("2.0.20", 5001740))
        listOf(5002296, 5002313, 5002318, 5002322).forEach { versionCode ->
            assertFalse(xrGalaxyXrHighResolutionPatch.supports("2.0.22", versionCode), "$versionCode")
        }
        assertFalse(xrGalaxyXrHighResolutionPatch.supports("2.0.22", 5001712))
        assertFalse(xrGalaxyXrHighResolutionPatch.supports("2.0.22", 5002243))

        val legacyCommon = listOf(
            deviceIdentityPatch,
            microphoneInputPresetPatch,
            oledCalibrationPatch,
            gxrFacebridgePatch,
            unrestrictedBatteryUsagePatch,
        )
        legacyCommon.forEach { patch ->
            assertTrue(patch.supports("2.0.20", 5001712), patch.name)
        }
        legacyCommon.forEach { patch ->
            assertFalse(patch.supports("2.0.20", 5001740), patch.name)
            assertFalse(patch.supports("2.0.22", 5002313), patch.name)
            assertFalse(patch.supports("2.0.22", 5002318), patch.name)
        }
        assertTrue(hmdOnlyPatch.supports("2.0.20", 5001712))
        assertTrue(hmdOnlyPatch.supports("2.0.23", 5002363))
        assertFalse(hmdOnlyPatch.supports("2.0.20", 5001740))
        listOf(5002313, 5002318, 5002322).forEach { versionCode ->
            assertFalse(hmdOnlyPatch.supports("2.0.22", versionCode), "$versionCode")
        }
    }

    @Test
    fun recursive_foundation_dependencies_remain_wired_and_guarded() {
        val identityClosure = deviceIdentityPatch.dependencyClosure()
        assertTrue(xrDeviceConfigBaselinePatch in identityClosure)
        assertTrue(xrCoreRuntimePatch in identityClosure)
        assertTrue(androidXrUiExtensionPatch in identityClosure)
        assertTrue(xrDirectInputFixPatch in identityClosure)

        val routingClosure = xrInputRoutingConfigPatch.dependencyClosure()
        assertFalse(xrLauncherBootstrapPatch in routingClosure)
        assertTrue(xrManifestCapabilityPackPatch in routingClosure)
        assertTrue(xrCoreRuntimePatch in routingClosure)

        listOf(xrGalaxyXrHighResolutionPatch, gxrFacebridgePatch).forEach { patch ->
            val closure = patch.dependencyClosure()
            assertFalse(xrLauncherBootstrapPatch in closure, patch.name)
            assertFalse(xrPermissionSettingsBootstrapPatch in closure, patch.name)
            assertFalse(xrStartupPermissionsPatch in closure, patch.name)
        }
    }

    private fun recommendedFor(version: String, versionCode: Int): List<Patch<*>> =
        allTopLevelPatches.filter { patch ->
            patch.default && patch.supports(version, versionCode)
        }

    private fun Patch<*>.supports(version: String, versionCode: Int): Boolean =
        compatibility.orEmpty().any { compatibility ->
            compatibility.targets.any { target ->
                target.version == version && target.versionCodes?.values?.contains(versionCode) == true
            }
        }

    private fun Patch<*>.dependencyClosure(visited: MutableSet<Patch<*>> = mutableSetOf()): Set<Patch<*>> {
        dependencies.forEach { dependency ->
            if (visited.add(dependency)) dependency.dependencyClosure(visited)
        }
        return visited
    }

    private companion object {
        val allIndividualPatches = listOf(
            xrStartupPermissionsPatch,
            androidXrNativePermissionNamesPatch,
            appearOnTopPatch,
            changePackageNamePatch,
            deviceIdentityPatch,
            deviceIdentityNativeXrPatch,
            forceHmdInitializationGatesPatch,
            forceLobbyPermissionStateGatePatch,
            forceStreamXrGatesPatch,
            gxrFacebridgePatch,
            xrGalaxyXrHighResolutionPatch,
            microphoneInputPresetPatch,
            networkOptimizationsPatch, blueNoisePatch,
            xrCodecsSupportPatch,
            performanceBoostPatch,
            oledCalibrationPatch,
            unrestrictedBatteryUsagePatch,
            hmdOnlyPatch,
            xrCoreRuntimePatch,
            xrDeviceConfigBaselinePatch,
            xrManifestCapabilityPackPatch,
            xrLauncherBootstrapPatch,
            xrInputRoutingConfigPatch,
        )

        val allTopLevelPatches = allIndividualPatches

        val legacyRecommendedPatches = setOf(
            androidXrNativePermissionNamesPatch,
            forceHmdInitializationGatesPatch,
            forceLobbyPermissionStateGatePatch,
            forceStreamXrGatesPatch,
            gxrFacebridgePatch,
            xrGalaxyXrHighResolutionPatch,
            microphoneInputPresetPatch,
            oledCalibrationPatch,
            unrestrictedBatteryUsagePatch,
            hmdOnlyPatch,
            xrCoreRuntimePatch,
            xrDeviceConfigBaselinePatch,
            xrInputRoutingConfigPatch,
            xrLauncherBootstrapPatch,
            xrStartupPermissionsPatch,
            xrManifestCapabilityPackPatch,
            deviceIdentityPatch, blueNoisePatch,
        )

        // Codecs Support retains its exact 5001712 legacy base.
        val legacyRecommendedPatches5001712 = legacyRecommendedPatches + xrCodecsSupportPatch + performanceBoostPatch

        val nativeRecommendedPatches = setOf(
            xrGalaxyXrHighResolutionPatch,
            gxrFacebridgePatch,
            microphoneInputPresetPatch,
            networkOptimizationsPatch,
            performanceBoostPatch,
            xrCodecsSupportPatch,
            blueNoisePatch,
            unrestrictedBatteryUsagePatch,
            hmdOnlyPatch,
            oledCalibrationPatch,
        )
    }
}

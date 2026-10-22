package app.template.patches.fourxvr

import app.template.patches.shared.assertPatchDescriptionContract
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * Mandatory portable checks: the 4XVR Galaxy XR patches are recommended by default (owner decision
 * 2026-10-11, individual defaults since the set wrapper was removed) and every member patch plus
 * payload is default-on, including the launch-critical Meta platform decoupling (on-device A/B,
 * 2026-10-12: without its stubs the app hangs before its first frame). The premium unlock stays
 * default-off and experimental until its headset validation is recorded (owner decision
 * 2026-10-12).
 */
class FourXvrBundleTest {

    @Test
    fun `4XVR patches are recommended by default including the platform decoupling`() {
        val members = listOf(
            fourXvrLaunchCompatibilityPatch,
            fourXvrLauncherIconPatch,
            fourXvrControllerModelsPatch,
            fourXvrRefreshProfilePatch,
            fourXvrHdrPipelinePatch,
            fourXvrAudioBridgePatch,
            fourXvrRenderDispatchPatch,
            fourXvrSubtitleSuitePatch,
            fourXvrToastContinuityPatch,
            fourXvrPlatformDecouplingPatch,
            fourXvrPopupRefreshPatch,
        )
        assertEquals(11, members.size)
        members.forEach { member -> assertTrue(member.default, member.name) }
        listOf(
            fourXvrHdrPayloadPatch,
            fourXvrAudioBridgePayloadPatch,
            fourXvrRenderDispatchPayloadPatch,
            fourXvrSubtitleSuitePayloadPatch,
        ).forEach { payload -> assertTrue(payload.default, payload.name) }
    }

    @Test
    fun `4XVR descriptions name exactly their compatible build`() {
        listOf(
            fourXvrLaunchCompatibilityPatch,
            fourXvrLauncherIconPatch,
            fourXvrControllerModelsPatch,
            fourXvrRefreshProfilePatch,
            fourXvrHdrPipelinePatch,
            fourXvrAudioBridgePatch,
            fourXvrRenderDispatchPatch,
            fourXvrSubtitleSuitePatch,
            fourXvrToastContinuityPatch,
            fourXvrPlatformDecouplingPatch,
            fourXvrPopupRefreshPatch,
            fourXvrHdrPayloadPatch,
            fourXvrAudioBridgePayloadPatch,
            fourXvrRenderDispatchPayloadPatch,
            fourXvrSubtitleSuitePayloadPatch,
            fourXvrPremiumUnlockPatch,
        ).forEach(::assertPatchDescriptionContract)
    }

    @Test
    fun `the premium unlock ships experimental and off until headset-validated`() {
        // Owner decision 2026-10-12: remove the premium requirement on Galaxy XR, where the store
        // cannot be reached to verify; default off until the on-device validation is recorded.
        assertFalse(fourXvrPremiumUnlockPatch.default, fourXvrPremiumUnlockPatch.name)
        assertTrue(fourXvrPremiumUnlockPatch.name!!.contains("experimental"), fourXvrPremiumUnlockPatch.name)
    }
}

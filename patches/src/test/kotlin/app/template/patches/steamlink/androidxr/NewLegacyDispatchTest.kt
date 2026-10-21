package app.template.patches.steamlink.androidxr

import app.template.patches.shared.Constants.isMouseOnlySteamLinkBuild
import app.template.patches.shared.Constants.isTwoProjectionSteamLinkBuild
import app.template.patches.steamlink.identity.deviceIdentityNativeXrPatch
import app.template.patches.steamlink.identity.deviceIdentityPatch
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class NewLegacyDispatchTest {
    private val legacyBuilds = listOf("2.0.20" to "5001712")

    @Test
    fun `legacy base selects the verified 2 projection payload and mouse only JNI wrappers`() {
        legacyBuilds.forEach { (version, code) ->
            assertTrue(isTwoProjectionSteamLinkBuild(version, code), "$version/$code")
            assertTrue(isMouseOnlySteamLinkBuild(version, code), "$version/$code")
            assertEquals(
                ANDROID_SURFACE_TRIGGER_5001712_RESOURCE_LIBRARY,
                androidSurfaceTriggerResourceLibraryForBuild(version, code),
                "$version/$code",
            )
            assertEquals(
                XrPointerRouteMethods("routeXrPointerAsMouse5001712", "routeXrPointerAsMouseGeneric5001712"),
                xrPointerRouteMethodsFor(version, code),
                "$version/$code",
            )
        }
    }

    @Test
    fun `later bases and mismatched pairs retain standard projection and input dispatch`() {
        listOf(
            "2.0.23" to "5002363",
            "2.0.20" to "5002363",
            "2.0.23" to "5001712",
            "2.0.20" to "5001813",
            "2.0.21" to "5001969",
        ).forEach { (version, code) ->
            assertFalse(isTwoProjectionSteamLinkBuild(version, code), "$version/$code")
            assertFalse(isMouseOnlySteamLinkBuild(version, code), "$version/$code")
            assertEquals(
                ANDROID_SURFACE_TRIGGER_LIBRARY,
                androidSurfaceTriggerResourceLibraryForBuild(version, code),
                "$version/$code",
            )
            assertEquals(
                XrPointerRouteMethods("routeXrPointerAsMouse", "routeXrPointerAsMouseGeneric"),
                xrPointerRouteMethodsFor(version, code),
                "$version/$code",
            )
        }
    }

    @Test
    fun `identity patch exposes no headset selector on any base`() {
        assertTrue(deviceIdentityPatch.options.isEmpty(), "legacy identity has no options")
        assertTrue(deviceIdentityNativeXrPatch.options.isEmpty(), "native identity has no options")
        assertTrue(deviceIdentityPatch.default, "legacy identity stays recommended")
        assertFalse(deviceIdentityNativeXrPatch.default, "native identity stays optional")
    }
}

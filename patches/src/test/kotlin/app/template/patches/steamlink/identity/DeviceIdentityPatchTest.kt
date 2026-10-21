package app.template.patches.steamlink.identity

import app.morphe.patcher.patch.PatchException
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class DeviceIdentityPatchTest {
    @Test
    fun `identity patches expose no headset selector`() {
        // One identity only: the Steam Link client reports xrvst2ue, so there is nothing to select.
        assertTrue(deviceIdentityPatch.options.isEmpty(), "legacy identity must have no options")
        assertTrue(deviceIdentityNativeXrPatch.options.isEmpty(), "native identity must have no options")
        assertTrue(deviceIdentityPatch.default, "legacy identity stays recommended")
        assertFalse(deviceIdentityNativeXrPatch.default, "native identity stays optional")
        assertEquals("xrvst2ue", GALAXY_XR_MODEL_NUMBER)
    }

    private val stockNativeXr =
        """
        {
          "requestedExtensions": ["XR_EXT_eye_gaze_interaction", "XR_PICO_eye_tracking"],
          "staticProps": {
            "hollywood": {
              "sTrackingSystem": "oculus",
              "sSerialNumber": "VRLINKHMDQUEST2",
              "sManufacturerName": "Oculus",
              "sModelNumber": "Oculus Quest2",
              "sRenderModelName": "generic_hmd",
              "sResourceRoot": "vrlink",
              "sInputProfilePath": "{oculus}/input/rift_profile.json",
              "flLaserMouseHmdAngleDegrees": 0,
              "sControllerType": "rift",
              "sDeviceType": "oculus/VRLINKHMDQUEST2"
            },
            "Project Swan": {
              "sTrackingSystem": "pico",
              "sSerialNumber": "VRLINKHMDPICO4ULTRA",
              "sManufacturerName": "Pico",
              "sModelNumber": "PICO 4 Ultra",
              "sRenderModelName": "generic_hmd",
              "sResourceRoot": "vrlink",
              "sInputProfilePath": "{oculus}/input/rift_profile.json",
              "flLaserMouseHmdAngleDegrees": 0,
              "sControllerType": "pico",
              "sDeviceType": "pico/VRLINKHMDPICO4ULTRA"
            },
            "unknown": {
              "sTrackingSystem": "oculus",
              "sSerialNumber": "VRLINKHMDUNKNOWN",
              "sManufacturerName": "Oculus",
              "sModelNumber": "oculus_quest_hmd",
              "sRenderModelName": "generic_hmd",
              "sResourceRoot": "vrlink",
              "sInputProfilePath": "{oculus}/input/rift_profile.json",
              "flLaserMouseHmdAngleDegrees": 0,
              "sControllerType": "rift",
              "sDeviceType": "oculus/VRLINKHMDUNKNOWN"
            }
          },
          "headOffsets": {
            "PICO 4 Ultra": [-0.050711045, -0.018201836, 0.051669556]
          },
          "input": {
            "eyeTracking": {
              "bindings": {
                "/interaction_profiles/ext/eye_gaze_interaction": [
                  "/user/eyes_ext/input/gaze_ext/pose"
                ]
              }
            }
          }
        }
        """.trimIndent()

    private val galaxyFields = listOf(
        "\"sTrackingSystem\": \"SamsungVST\"",
        "\"sSerialNumber\": \"VRLINKHMDGALAXYXR\"",
        "\"sManufacturerName\": \"Samsung\"",
        "\"sModelNumber\": \"xrvst2ue\"",
        "\"sRenderModelName\": \"generic_hmd\"",
        "\"sResourceRoot\": \"vrlink\"",
        "\"sInputProfilePath\": \"{vrlink}/input/galaxy_xr_hmd_profile.json\"",
        "\"flLaserMouseHmdAngleDegrees\": 0",
        "\"sControllerType\": \"galaxy_xr_hmd\"",
        "\"sDeviceType\": \"SamsungVST/VRLINKHMDGALAXYXR\"",
    )

    @Test
    fun `Galaxy identity upserts all transport entries and preserves unrelated config`() {
        val patched = patchGalaxyIdentity(stockNativeXr)

        listOf("xrvst2", "xrvst2ue", "unknown").forEach { key ->
            val entry = identityEntry(patched, key)
            galaxyFields.forEach { field -> assertTrue(entry.contains(field), "$key missing $field") }
        }
        assertEquals(3, Regex("\"sSerialNumber\": \"VRLINKHMDGALAXYXR\"").findAll(patched).count())
        // Everything the headset also needs stays byte-identical: stock products, extensions,
        // head offsets and eye-tracking bindings.
        assertTrue(patched.contains("\"XR_PICO_eye_tracking\""))
        assertTrue(patched.contains("\"Project Swan\""))
        assertTrue(patched.contains("-0.050711045"))
        assertTrue(patched.contains("/user/eyes_ext/input/gaze_ext/pose"))
        assertEquals(patched, patchGalaxyIdentity(patched), "reapplication must be idempotent")
    }

    @Test
    fun `existing product entry is replaced wholesale and unrelated bytes are preserved`() {
        val existing = "\"xrvst2ue\": {\"sModelNumber\": \"Galaxy XR\", \"keep\": 42},"
        val input = stockNativeXr.replace("\"staticProps\": {", "\"staticProps\": {\n    $existing")
        val patched = patchGalaxyIdentity(input)

        galaxyFields.forEach { field -> assertTrue(identityEntry(patched, "xrvst2ue").contains(field), field) }
        // The whole runtime-selected entry is replaced: a stale model string must not survive.
        assertFalse(identityEntry(patched, "xrvst2ue").contains("\"keep\""))
        assertEquals(identityEntry(patched, "unknown"), identityEntry(patched, "xrvst2"))
        // Only the three identity entries change; the neighbouring stock product keeps its bytes.
        assertTrue(
            identityEntry(patched, "hollywood").contains("\"sModelNumber\": \"Oculus Quest2\""),
            "neighbouring stock entry must keep its bytes",
        )
        assertEquals(patched, patchGalaxyIdentity(patched))
    }

    @Test
    fun `duplicate and malformed targets fail before any output is returned`() {
        listOf(
            "\"xrvst2ue\": {}, \"xrvst2ue\": {}",
            "\"xrvst2ue\": 42",
            "\"\\u0078rvst2ue\": {\"sModelNumber\": \"first\"}",
            "\"xrvst2ue\": {\"sModelNumber\": \"first\"}, \"\\u0078rvst2ue\": {}",
        ).forEach { broken ->
            val input = stockNativeXr.replace("\"staticProps\": {", "\"staticProps\": {$broken,")
            assertFailsWith<PatchException>(broken) { patchGalaxyIdentity(input) }
        }
        assertFailsWith<PatchException> { patchGalaxyIdentity("{\"requestedExtensions\": []}") }
    }

    @Test
    fun `CRLF layouts keep their line endings`() {
        val input = stockNativeXr.replace("\n", "\r\n")
        val patched = patchGalaxyIdentity(input)

        assertFalse(Regex("(?<!\r)\n").containsMatchIn(patched), "no bare LF may be introduced")
        galaxyFields.forEach { field -> assertTrue(identityEntry(patched, "unknown").contains(field), field) }
        assertEquals(patched, patchGalaxyIdentity(patched))
    }

    @Test
    fun `installed Galaxy payload carries the single xrvst2ue model`() {
        val payload = requireNotNull(
            javaClass.getResource("/steamlink/androidxr/hmd_config.json"),
        ).readText()

        assertEquals(3, Regex("\"sModelNumber\": \"xrvst2ue\"").findAll(payload).count())
        assertEquals(3, Regex("\"sTrackingSystem\": \"SamsungVST\"").findAll(payload).count())
        assertEquals(3, Regex("\"sControllerType\": \"galaxy_xr_hmd\"").findAll(payload).count())
    }

    private fun identityEntry(json: String, key: String): String {
        val match = Regex("\"$key\"\\s*:\\s*\\{").find(json) ?: error("missing $key")
        val start = match.range.last
        var depth = 0
        var quoted = false
        var escaped = false
        for (index in start until json.length) {
            val character = json[index]
            if (quoted) {
                when {
                    escaped -> escaped = false
                    character == '\\' -> escaped = true
                    character == '"' -> quoted = false
                }
            } else {
                when (character) {
                    '"' -> quoted = true
                    '{' -> depth++
                    '}' -> if (--depth == 0) return json.substring(start, index + 1)
                }
            }
        }
        error("unterminated $key")
    }
}

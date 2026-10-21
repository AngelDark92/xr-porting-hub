package app.template.patches.steamlink.androidxr

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LegacyHmdConfigTest {
    private val galaxyPayload by lazy {
        requireNotNull(javaClass.getResource("/steamlink/androidxr/hmd_config.json")).readBytes()
    }

    @Test
    fun `5001712 uses headset keyed requested extensions`() {
        val patched = adaptLegacyHmdConfigForBuild(galaxyPayload, "2.0.20", "5001712").decodeToString()

        assertTrue(Regex("\"requestedExtensions\"\\s*:\\s*\\{").containsMatchIn(patched))
        assertFalse(Regex("\"requestedExtensions\"\\s*:\\s*\\[").containsMatchIn(patched))
        listOf("xrvst2", "xrvst2ue", "unknown").forEach { key ->
            assertTrue(
                Regex("\"$key\"\\s*:\\s*\\[\\s*\"XR_EXT_eye_gaze_interaction\"").containsMatchIn(patched),
                key,
            )
        }
        assertEquals(1, Regex("\"requestedExtensions\"").findAll(patched).count())
    }
}

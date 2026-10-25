package app.template.patches.steamlink.androidxr

import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.Constants.EXPERIMENTAL_COMPATIBILITY_NAME
import app.template.patches.shared.PatchCategories
import java.io.File
import java.nio.file.Files
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.test.assertFailsWith

class QuadLayerPatchTest {
    @Test
    fun `both arms are exact 5001712 experimental patches`() {
        listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT).forEach { variant ->
            val patch = patchFor(variant)
            assertEquals(variant.patchName, patch.name)
            assertFalse(patch.default, variant.patchName)
            assertEquals(PatchCategories.IMAGE_QUALITY, patch.category, variant.patchName)
            assertTrue(patch.dependencies.isEmpty(), variant.patchName)
            assertTrue(patch.options.isEmpty(), variant.patchName)
            val targets = patch.compatibility.orEmpty().flatMap { compatibility ->
                assertEquals("com.valvesoftware.steamlinkvr", compatibility.packageName)
                assertEquals(
                    EXPERIMENTAL_COMPATIBILITY_NAME,
                    compatibility.name,
                    variant.patchName,
                )
                compatibility.targets.flatMap { target ->
                    target.versionCodes.orEmpty().values.map { target.version to it }
                }
            }.toSet()
            assertEquals(setOf("2.0.20" to 5001712), targets, variant.patchName)
            assertTrue(patch.description.orEmpty().contains(
                "Needs: deselect Alpha-0 high-resolution trigger.",
            ), variant.patchName)
        }
        assertTrue(isQuadLayerSteamLinkBuild("2.0.20", "5001712"))
        listOf(
            "2.0.20" to "5001812",
            "2.0.21" to "5001968",
            "2.0.22" to "5002244",
            "2.0.23" to "5002363",
            "2.0.24" to "5002395",
            "2.0.20" to "5001713",
        ).forEach { (version, code) ->
            assertFalse(isQuadLayerSteamLinkBuild(version, code), "$version/$code")
        }
    }

    @Test
    fun `bundled payloads validate and reject tampering without decoded inputs`() {
        listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT).forEach { variant ->
            val helper = projectionModeResource(variant.library)
            val manifest = projectionModeResource(variant.manifest)
            validateQuadLayerPayload(variant, helper, manifest)
            assertContentEquals(
                byteArrayOf(0x7f, 0x45, 0x4c, 0x46),
                helper.copyOfRange(0, 4),
                variant.library,
            )
            val helperText = helper.toString(Charsets.ISO_8859_1)
            assertTrue(helperText.contains(variant.mode), variant.library)
            assertTrue(helperText.contains(variant.buildId), variant.library)
            assertTrue(manifest.toString(Charsets.UTF_8).contains(
                "\"library_path\": \"${variant.library}\"",
            ), variant.manifest)

            val alteredHelper = helper.copyOf().apply {
                this[lastIndex] = (last().toInt() xor 1).toByte()
            }
            assertFailsWith<PatchException>(variant.patchName) {
                validateQuadLayerPayload(variant, alteredHelper, manifest)
            }
            val alteredManifest = manifest.copyOf().apply {
                this[0] = (this[0].toInt() xor 1).toByte()
            }
            assertFailsWith<PatchException>(variant.patchName) {
                validateQuadLayerPayload(variant, helper, alteredManifest)
            }
        }
        val foveaHelper = projectionModeResource(FOVEA_QUAD_LIBRARY)
        val quadOnlyHelper = projectionModeResource(QUAD_ONLY_LIBRARY)
        assertFalse(foveaHelper.toString(Charsets.ISO_8859_1).contains(QUAD_ONLY_MODE))
        assertFalse(quadOnlyHelper.toString(Charsets.ISO_8859_1).contains(FOVEA_QUAD_MODE))
    }

    @Test
    fun `excluded exact pairs return unchanged before file or payload access`() = withTemporaryRoot { root ->
        val missing = File(root, "must-stay-absent")
        listOf("2.0.20" to "5001812", "2.0.20" to "5001713", "2.0.24" to "5002395")
            .forEach { (version, code) ->
                listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT).forEach { variant ->
                    assertFalse(installQuadLayerResources(
                        missing, version, code, variant, byteArrayOf(1), byteArrayOf(2),
                    ), "${variant.patchName}: $version/$code")
                }
                assertFalse(missing.exists())
            }
    }

    @Test
    fun `production install and reapplication preserve neighbours and timestamps`() =
        withSyntheticBase { root ->
            listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT).forEach { variant ->
                val helper = projectionModeResource(variant.library)
                val manifest = projectionModeResource(variant.manifest)
                val neighbour = File(layerDirectory(root), NEIGHBOUR_LAYER_MANIFEST)
                    .apply { parentFile.mkdirs(); writeText("existing neighbour layer manifest") }
                val neighbourBytes = neighbour.readBytes()
                val sceneBytes = sceneFile(root).readBytes()

                assertTrue(installQuadLayerResources(
                    root, "2.0.20", "5001712", variant, helper, manifest,
                ), variant.patchName)
                val installedHelper = File(libDirectory(root), variant.library)
                val installedManifest = File(layerDirectory(root), variant.manifest)
                assertContentEquals(helper, installedHelper.readBytes())
                assertContentEquals(manifest, installedManifest.readBytes())
                val helperTime = installedHelper.lastModified()
                val manifestTime = installedManifest.lastModified()
                assertTrue(installQuadLayerResources(
                    root, "2.0.20", "5001712", variant, helper, manifest,
                ), variant.patchName)
                assertEquals(helperTime, installedHelper.lastModified())
                assertEquals(manifestTime, installedManifest.lastModified())
                assertContentEquals(sceneBytes, sceneFile(root).readBytes())
                assertContentEquals(neighbourBytes, neighbour.readBytes())
                assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
                assertFalse(root.walkTopDown().any { it.name == ANDROID_SURFACE_TRIGGER_LIBRARY })

                installedHelper.delete()
                installedManifest.delete()
            }
    }

    @Test
    fun `wrong scene size and missing scene fail before creating resources`() = withTemporaryRoot { root ->
        val scene = sceneFile(root)
        scene.parentFile.mkdirs()
        scene.writeBytes(ByteArray(EXACT_SCENE_SIZE - 1))
        listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT).forEach { variant ->
            assertFailsWith<PatchException>(variant.patchName) {
                installQuadLayerResources(root, "2.0.20", "5001712", variant)
            }
            assertFalse(File(libDirectory(root), variant.library).exists())
        }
        scene.delete()
        assertFailsWith<PatchException> {
            installQuadLayerResources(root, "2.0.20", "5001712", FOVEA_QUAD_VARIANT)
        }
        assertFalse(File(root, "assets/openxr").exists())
    }

    @Test
    fun `retired renderer hook fails closed before installing`() = withSyntheticBase { root ->
        val scene = sceneFile(root)
        val bytes = scene.readBytes()
        val retired = RETIRED_LIBRARY.toByteArray(Charsets.US_ASCII)
        retired.copyInto(bytes, 0x1000)
        scene.writeBytes(bytes)
        listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT).forEach { variant ->
            assertFailsWith<PatchException>(variant.patchName) {
                installQuadLayerResources(root, "2.0.20", "5001712", variant)
            }
            assertFalse(File(libDirectory(root), variant.library).exists())
            assertFalse(File(layerDirectory(root), variant.manifest).exists())
        }
    }

    @Test
    fun `alpha-0 trigger and both quad arms reject each other in either order`() =
        withSyntheticBase { root ->
            val alpha0Library = File(libDirectory(root), ANDROID_SURFACE_TRIGGER_LIBRARY)
            alpha0Library.writeBytes(byteArrayOf(1, 2, 3))
            listOf(FOVEA_QUAD_VARIANT, QUAD_ONLY_VARIANT).forEach { variant ->
                assertFailsWith<PatchException>(variant.patchName) {
                    installQuadLayerResources(root, "2.0.20", "5001712", variant)
                }
                assertContentEquals(byteArrayOf(1, 2, 3), alpha0Library.readBytes())
                assertFalse(File(libDirectory(root), variant.library).exists())
            }
            alpha0Library.delete()

            assertTrue(installQuadLayerResources(
                root, "2.0.20", "5001712", FOVEA_QUAD_VARIANT,
            ))
            val requested = ProjectionModeResources(
                ANDROID_SURFACE_TRIGGER_MODE,
                ANDROID_SURFACE_TRIGGER_LIBRARY,
                ANDROID_SURFACE_TRIGGER_MANIFEST,
            )
            assertFailsWith<PatchException> {
                installProjectionModeResources(
                    libDirectory(root), layerDirectory(root), requested, byteArrayOf(1, 2, 3),
                )
            }
            assertTrue(File(libDirectory(root), FOVEA_QUAD_LIBRARY).isFile)
            assertTrue(File(layerDirectory(root), FOVEA_QUAD_MANIFEST).isFile)
            assertFalse(alpha0Library.exists())
            assertFalse(File(layerDirectory(root), ANDROID_SURFACE_TRIGGER_MANIFEST).exists())
        }

    @Test
    fun `arms are mutually exclusive and preserve the installed arm`() = withSyntheticBase { root ->
        assertTrue(installQuadLayerResources(root, "2.0.20", "5001712", FOVEA_QUAD_VARIANT))
        val foveaHelper = File(libDirectory(root), FOVEA_QUAD_LIBRARY).readBytes()
        val foveaManifest = File(layerDirectory(root), FOVEA_QUAD_MANIFEST).readBytes()
        assertFailsWith<PatchException>(QUAD_ONLY_VARIANT.patchName) {
            installQuadLayerResources(root, "2.0.20", "5001712", QUAD_ONLY_VARIANT)
        }
        assertContentEquals(foveaHelper, File(libDirectory(root), FOVEA_QUAD_LIBRARY).readBytes())
        assertContentEquals(
            foveaManifest,
            File(layerDirectory(root), FOVEA_QUAD_MANIFEST).readBytes(),
        )
        assertFalse(File(libDirectory(root), QUAD_ONLY_LIBRARY).exists())
        assertFalse(File(layerDirectory(root), QUAD_ONLY_MANIFEST).exists())
    }

    @Test
    fun `stale installed resource and tampered payload fail before partial writes`() =
        withSyntheticBase { root ->
            val helperFile = File(libDirectory(root), FOVEA_QUAD_LIBRARY)
            val manifestFile = File(layerDirectory(root), FOVEA_QUAD_MANIFEST)
            manifestFile.parentFile.mkdirs()
            manifestFile.writeText("stale manifest")
            assertFailsWith<PatchException> {
                installQuadLayerResources(root, "2.0.20", "5001712", FOVEA_QUAD_VARIANT)
            }
            assertFalse(helperFile.exists())
            assertEquals("stale manifest", manifestFile.readText())
            manifestFile.delete()

            val helper = projectionModeResource(FOVEA_QUAD_LIBRARY)
            val altered = helper.copyOf().apply {
                this[lastIndex] = (last().toInt() xor 1).toByte()
            }
            assertFailsWith<PatchException> {
                installQuadLayerResources(
                    root, "2.0.20", "5001712", FOVEA_QUAD_VARIANT,
                    altered, projectionModeResource(FOVEA_QUAD_MANIFEST),
                )
            }
            assertFalse(helperFile.exists())
            assertFalse(manifestFile.exists())
            assertFalse(root.walkTopDown().any { it.name.endsWith(".tmp") })
        }

    private fun patchFor(variant: QuadLayerVariant) = when (variant) {
        FOVEA_QUAD_VARIANT -> foveaQuadLayersPatch
        QUAD_ONLY_VARIANT -> quadOnlyCompositionPatch
        else -> error("Unknown quad-layer variant: ${variant.mode}")
    }

    private fun libDirectory(root: File) = File(root, "lib/arm64-v8a")
    private fun sceneFile(root: File) = File(libDirectory(root), "libvrlink_scene.so")
    private fun layerDirectory(root: File) = File(root, "assets/openxr/1/api_layers/implicit.d")

    private fun withSyntheticBase(block: (File) -> Unit) = withTemporaryRoot { root ->
        // Installer-only fixture. Exact size exercises the build pin; it is not
        // evidence that a rendered base matches Valve's decoded layout.
        sceneFile(root).apply { parentFile.mkdirs(); writeBytes(ByteArray(EXACT_SCENE_SIZE)) }
        block(root)
    }

    private fun withTemporaryRoot(block: (File) -> Unit) {
        val root = Files.createTempDirectory("quad-layer-patch-test-").toFile()
        try { block(root) } finally { root.deleteRecursively() }
    }

    private companion object {
        const val EXACT_SCENE_SIZE = 2_221_072
        const val RETIRED_LIBRARY = "libgxr_nsp.so"
        const val NEIGHBOUR_LAYER_MANIFEST =
            "XR_APILAYER_local_GalaxyXR_controller_extrapolation.json"
    }
}

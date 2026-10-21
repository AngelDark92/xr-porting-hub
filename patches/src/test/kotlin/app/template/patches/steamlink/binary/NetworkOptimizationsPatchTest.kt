package app.template.patches.steamlink.binary

import app.morphe.patcher.patch.Patch
import app.morphe.patcher.patch.PatchException
import app.template.patches.shared.PatchCategories
import org.junit.Assume.assumeTrue
import java.io.File
import java.security.MessageDigest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * Byte-level audit of the Network Optimizations patch against the frozen
 * 2.0.23/5002363 transport gate:
 * builds/steamlink-patches/build/transport-optimisation-20261011/gate0-network-optimizations.json.
 *
 * Every decoded-input check is driven by the real ignored release tree and skips
 * cleanly on a fresh checkout; no synthetic replacement library is used anywhere.
 */
class NetworkOptimizationsPatchTest {
    @Test
    fun `metadata declares the streaming category and only the exact 2_0_23 target`() {
        assertEquals("Network Optimizations", networkOptimizationsPatch.name)
        assertEquals(true, networkOptimizationsPatch.default)
        assertEquals(PatchCategories.STREAMING_AND_NETWORK, networkOptimizationsPatch.category)
        assertTrue(networkOptimizationsPatch.supports("2.0.23", 5002363))
        assertFalse(networkOptimizationsPatch.supports("2.0.22", 5002363))
        assertFalse(networkOptimizationsPatch.supports("2.0.20", 5001712))
    }

    @Test
    fun `scene transform reproduces the frozen hashes edits and idempotence`() =
        withDecoded(SCENE_RELATIVE_PATH) { stock ->
            val snapshot = stock.copyOf()
            assertEquals(SCENE_SIZE, stock.size, "Frozen stock size")
            assertEquals(SCENE_STOCK_SHA256, stock.sha256(), "Frozen stock hash")

            val patched = applyNetworkOptimizationsScene(stock)

            assertContentEquals(snapshot, stock, "Caller input mutated")
            assertEquals(SCENE_SIZE, patched.size, "Patch must not resize the library")
            assertEquals(SCENE_PATCHED_SHA256, patched.sha256(), "Frozen patched hash")

            val changed = stock.indices.filter { stock[it] != patched[it] }
            assertEquals(20, changed.size, "Exactly 20 scene bytes may change")
            assertEquals(SCENE_CHANGED_OFFSETS, changed, "Changed-byte set drifted from the frozen transport edit")

            // E1 owns the frozen 0x166b60..0x166b8b window; E2/E3 keep their 16-byte programs.
            assertEquals(0x166b60, E1.offset)
            assertEquals(0x166b8b, E1.endExclusive - 1)
            assertEquals(0x167904, E2.offset)
            assertEquals(0x17575c, E3.offset)
            assertContentEquals(E1.replacementBytes(), patched.copyOfRange(0x166b60, 0x166b8c),
                "E1 region 0x166b60..0x166b8b")
            for (edit in SCENE_EDITS) {
                assertContentEquals(edit.stockBytes(), stock.copyOfRange(edit.offset, edit.endExclusive),
                    "${edit.id} stock window 0x${edit.offset.toString(16)}")
                assertContentEquals(edit.replacementBytes(), patched.copyOfRange(edit.offset, edit.endExclusive),
                    "${edit.id} patched window 0x${edit.offset.toString(16)}")
            }

            assertContentEquals(patched, applyNetworkOptimizationsScene(patched), "Reapply is not idempotent")
            assertContentEquals(patched, applyNetworkOptimizationsScene(patched.copyOf()), "Reapply is not idempotent")
        }

    @Test
    fun `scene transform fails closed on a corrupted or truncated library`() =
        withDecoded(SCENE_RELATIVE_PATH) { stock ->
            val flipped = stock.copyOf().also { it[0x166b60] = (it[0x166b60].toInt() xor 0x01).toByte() }
            val flippedSnapshot = flipped.copyOf()
            assertFailsWith<PatchException>("Flipped 0x166b60 anchor must be rejected") {
                applyNetworkOptimizationsScene(flipped)
            }
            assertContentEquals(flippedSnapshot, flipped, "Failed patch mutated its input")

            val truncated = stock.copyOf(stock.size - 1)
            val truncatedSnapshot = truncated.copyOf()
            assertFailsWith<PatchException>("Truncated library must be rejected") {
                applyNetworkOptimizationsScene(truncated)
            }
            assertContentEquals(truncatedSnapshot, truncated, "Failed patch mutated its input")
        }

    @Test
    fun `scene transform tolerates unrelated prior patch edits and preserves them`() =
        withDecoded(SCENE_RELATIVE_PATH) { stock ->
            // Simulates the recommended-set order: Microphone input preset (0x0F44C0), GXR tongue
            // block (0x141C6C) and Visual Delay hook (0x101F1C) have already edited the scene
            // library before this patch runs. Only the windows this patch owns may be checked.
            val prior = stock.copyOf().also {
                // Microphone preset's real write is 0xE1 -> 0xC1 (Voice Recognition) at 0x0F44C0.
                it[0x0F44C0] = 0xC1.toByte()
                it[0x141C6C] = (it[0x141C6C].toInt() xor 0x01).toByte()
                it[0x101F1C] = 0x11.toByte()
            }
            val patched = applyNetworkOptimizationsScene(prior)

            for (edit in SCENE_EDITS) {
                assertContentEquals(edit.replacementBytes(), patched.copyOfRange(edit.offset, edit.endExclusive),
                    "${edit.id} patched window over a multi-patch input")
            }
            assertEquals(0xC1, patched[0x0F44C0].toInt() and 0xFF,
                "Prior microphone edit not preserved")
            assertEquals((stock[0x141C6C].toInt() xor 0x01) and 0xFF, patched[0x141C6C].toInt() and 0xFF,
                "Prior tongue edit not preserved")
            assertEquals(0x11, patched[0x101F1C].toInt() and 0xFF, "Prior delay edit not preserved")
            assertContentEquals(patched, applyNetworkOptimizationsScene(patched), "Reapply is not idempotent")
        }

    @Test
    fun `main transform reproduces the frozen hashes edits and idempotence`() =
        withDecoded(MAIN_RELATIVE_PATH) { stock ->
            val snapshot = stock.copyOf()
            assertEquals(MAIN_SIZE, stock.size, "Frozen stock size")
            assertEquals(MAIN_STOCK_SHA256, stock.sha256(), "Frozen stock hash")

            val patched = applyNetworkOptimizationsMain(stock)

            assertContentEquals(snapshot, stock, "Caller input mutated")
            assertEquals(MAIN_SIZE, patched.size, "Patch must not resize the library")
            assertEquals(MAIN_PATCHED_SHA256, patched.sha256(), "Frozen patched hash")

            val changed = stock.indices.filter { stock[it] != patched[it] }
            assertEquals(4, changed.size, "Exactly 4 main bytes may change")
            assertEquals(MAIN_CHANGED_OFFSETS, changed, "Changed-byte set drifted from the frozen E4 edit")

            assertEquals(0x8ec5c0, E4.offset)
            assertContentEquals(E4.replacementBytes(), patched.copyOfRange(E4.offset, E4.endExclusive),
                "E4 region 0x8ec5c0..0x8ec5cf")
            assertContentEquals(E4.stockBytes(), stock.copyOfRange(E4.offset, E4.endExclusive), "E4 stock window")

            assertContentEquals(patched, applyNetworkOptimizationsMain(patched), "Reapply is not idempotent")
            assertContentEquals(patched, applyNetworkOptimizationsMain(patched.copyOf()), "Reapply is not idempotent")
        }

    @Test
    fun `main transform fails closed on a corrupted library`() =
        withDecoded(MAIN_RELATIVE_PATH) { stock ->
            val flipped = stock.copyOf().also { it[0x8ec5c0] = (it[0x8ec5c0].toInt() xor 0x01).toByte() }
            val flippedSnapshot = flipped.copyOf()
            assertFailsWith<PatchException>("Flipped 0x8ec5c0 anchor must be rejected") {
                applyNetworkOptimizationsMain(flipped)
            }
            assertContentEquals(flippedSnapshot, flipped, "Failed patch mutated its input")
        }

    @Test
    fun `main transform tolerates unrelated prior edits`() =
        withDecoded(MAIN_RELATIVE_PATH) { stock ->
            val prior = stock.copyOf().also { it[0x1000] = (it[0x1000].toInt() xor 0x01).toByte() }
            val patched = applyNetworkOptimizationsMain(prior)
            assertContentEquals(E4.replacementBytes(), patched.copyOfRange(E4.offset, E4.endExclusive),
                "E4 patched window over a prior-edit input")
            assertEquals((stock[0x1000].toInt() xor 0x01) and 0xFF, patched[0x1000].toInt() and 0xFF,
                "Prior edit not preserved")
        }

    @Test
    fun `both transforms reproduce the frozen patched hashes and stock windows`() {
        for (library in LIBRARIES) {
            withDecoded(library.relativePath) { stock ->
                assertEquals(library.size, stock.size, library.fileName)
                assertEquals(library.stockSha256, stock.sha256(), "${library.fileName} stock hash")
                for (edit in library.edits) {
                    assertContentEquals(edit.stockBytes(), stock.copyOfRange(edit.offset, edit.endExclusive),
                        "${library.fileName} ${edit.id} stock window")
                }

                val patched = library.transform(stock)

                assertEquals(library.patchedSha256, patched.sha256(), "${library.fileName} patched hash")
                for (edit in library.edits) {
                    assertContentEquals(edit.replacementBytes(), patched.copyOfRange(edit.offset, edit.endExclusive),
                        "${library.fileName} ${edit.id} patched window")
                }
                assertEquals(library.changedOffsets, stock.indices.filter { stock[it] != patched[it] },
                    "${library.fileName} changed-byte set")
            }
        }
    }

    /** Repo-root walk-up and skip mechanism mirror VdSdrFoveaPatchTest. */
    private fun withDecoded(relativePath: String, action: (ByteArray) -> Unit) {
        val working = File(System.getProperty("user.dir")).canonicalFile
        val root = generateSequence(working) { it.parentFile }
            .firstOrNull { File(it, "patches/src/main/kotlin").isDirectory }
        if (root == null) {
            assumeTrue("Real decoded-input audit requires the repository workspace", false)
            return
        }
        // The decoded trees were renamed; accept both layouts so the audit works today
        // and on a fresh checkout of either name.
        val tree = DECODED_TREE_NAMES.map { File(root, it) }.firstOrNull { it.isDirectory }
        if (tree == null) {
            assumeTrue("Real decoded-input audit BLOCKED: no decoded 2.0.23/5002363 tree", false)
            return
        }
        val file = File(tree, relativePath)
        if (!file.isFile) {
            assumeTrue("Real decoded-input audit BLOCKED: ${file.path}; no synthetic replacement", false)
            return
        }
        action(file.readBytes())
    }

    private data class FrozenEdit(
        val id: String,
        val offset: Int,
        val stock: String,
        val replacement: String,
    ) {
        val length: Int get() = stock.length / 2
        val endExclusive: Int get() = offset + length
        fun stockBytes(): ByteArray = stock.hexBytes()
        fun replacementBytes(): ByteArray = replacement.hexBytes()
    }

    private class FrozenLibrary(
        val fileName: String,
        val relativePath: String,
        val size: Int,
        val stockSha256: String,
        val patchedSha256: String,
        val transform: (ByteArray) -> ByteArray,
        val edits: List<FrozenEdit>,
        val changedOffsets: List<Int>,
    )

    private fun Patch<*>.supports(version: String, versionCode: Int): Boolean =
        compatibility.orEmpty().any { compatibility ->
            compatibility.targets.any { target ->
                target.version == version && target.versionCodes?.values?.contains(versionCode) == true
            }
        }

    private companion object {
        const val SCENE_RELATIVE_PATH = "lib/arm64-v8a/libvrlink_scene.so"
        const val MAIN_RELATIVE_PATH = "lib/arm64-v8a/libmain.so"

        // Both decoded-tree spellings are git-ignored; the renamed one is preferred.
        val DECODED_TREE_NAMES = listOf(
            "android-steamlinkvr-release-base-2.0.23-5002363",
            "decoded-apk-android-steamlinkvr-release-base-2.0.23-5002363",
        )

        // Frozen 2.0.23/5002363 gate values (gate0-network-optimizations.json).
        const val SCENE_SIZE = 2_292_008
        const val SCENE_STOCK_SHA256 = "628821feab199d7712be8a51273eb9a21ec440a7c91aa6a768cc7307a4fe22f0"
        const val SCENE_PATCHED_SHA256 = "4d5ff6937d07747495b82a412592ce017d6bdadd54c8264e1e49ebb24b5aa72c"
        const val MAIN_SIZE = 15_826_584
        const val MAIN_STOCK_SHA256 = "1bc350c5d0d2cb6501680f98d55512c516802df7d78fb48f8a142baf0d2f5acb"
        const val MAIN_PATCHED_SHA256 = "17c1a4d91351190fcf2a996c1cb7673faac3bdde8aee70716cb07e295b950ade"

        val E1 = FrozenEdit("E1", 0x166b60,
            "00fafff0610a40b900541891d9ca0294682261f929008052f4031f2a0941313986000014e9ff9f5234008052",
            "00fafff0610a40b900541891d9ca02941f2003d51f2003d51f2003d51f2003d554000014e9ff9f5234008052")
        val E2 = FrozenEdit("E2", 0x167904,
            "f8038b1aff0200f91f1b0071562107b9",
            "f8038b1aff0200f91f330071562107b9")
        val E3 = FrozenEdit("E3", 0x17575c,
            "4800a052809a40b9e80b00b9e3230091",
            "0802a052809a40b9e80b00b9e3230091")
        val E4 = FrozenEdit("E4", 0x8ec5c0,
            "003040f9ff360c942000805297ffff17",
            "003040f91f2003d52000805297ffff17")

        val SCENE_EDITS = listOf(E1, E2, E3)
        val MAIN_EDITS = listOf(E4)

        val SCENE_CHANGED_OFFSETS: List<Int> =
            (0x166b70..0x166b7f).toList() + listOf(0x166b80, 0x16790d, 0x17575c, 0x17575d)
        val MAIN_CHANGED_OFFSETS: List<Int> = (0x8ec5c4..0x8ec5c7).toList()

        val LIBRARIES = listOf(
            FrozenLibrary("libvrlink_scene.so", SCENE_RELATIVE_PATH, SCENE_SIZE, SCENE_STOCK_SHA256,
                SCENE_PATCHED_SHA256, { bytes -> applyNetworkOptimizationsScene(bytes) },
                SCENE_EDITS, SCENE_CHANGED_OFFSETS),
            FrozenLibrary("libmain.so", MAIN_RELATIVE_PATH, MAIN_SIZE, MAIN_STOCK_SHA256,
                MAIN_PATCHED_SHA256, { bytes -> applyNetworkOptimizationsMain(bytes) },
                MAIN_EDITS, MAIN_CHANGED_OFFSETS),
        )
    }
}

// File-private so the nested FrozenEdit/FrozenLibrary helpers can decode and hash the
// frozen hex values without an outer-class receiver.
private fun String.hexBytes(): ByteArray {
    require(length % 2 == 0) { "Odd-length frozen hex literal: $this" }
    return ByteArray(length / 2) {
        ((this[it * 2].digitToInt(16) shl 4) or this[it * 2 + 1].digitToInt(16)).toByte()
    }
}

private fun ByteArray.sha256(): String = MessageDigest.getInstance("SHA-256").digest(this)
    .joinToString("") { "%02x".format(it) }

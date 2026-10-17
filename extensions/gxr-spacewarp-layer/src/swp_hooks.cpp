// gxr-spacewarp: PLT GOT patch module for libvrlink_scene.so (09-LAYER-B1b comp 3).
//
// The layer must intercept a call the scene library makes internally without
// exporting anything to the loader: it patches the target's PLT GOT slot for
// eglCreateImageKHR (the decoded-frame arrival-learning site, shared brief
// candidate 1) after the library loads. Pattern mirrors codecs-support-layer:
//
//   1. dl_iterate_phdr base scan for libvrlink_scene.so (retried from XR calls;
//      implicit layers mount before the scene lib, so a constructor-only scan
//      would miss it).
//   2. Verify the library identity against ONE pinned code anchor per base — a
//      0x80-byte region whose SHA-256 was frozen in the Gate-0 JSON by
//      tests/derive_gate0.py. Fail closed on any mismatch: no interception.
//   3. mprotect the GOT page RW (these clients are BIND_NOW + full RELRO —
//      verified: DT_FLAGS=0x8 DF_BIND_NOW + DT_FLAGS_1 DF_1_NOW on 5002363),
//      write the wrapper pointer, restore PROT_READ.
//   4. Fail closed on unknown layout at every step; exactly one log line.
//
// Pins re-derived from the CURRENT decoded trees, per base [verified 2026-10-17]:
//   - anchor @ offset 0xfd6fc: the code segment (r-x) IS PT_LOAD-identity for
//     this lib (p_offset == p_vaddr), so file offset == runtime offset there.
//   - eglCreateImageKHR GOT slot @ runtime vaddr 0x22fcc8 (single JUMP_SLOT
//     relocation, exactly one .dynsym import, occurrence count 1). NOTE: this lib
//     is NOT PT_LOAD-identity in its writable segments (writable PT_LOAD delta is
//     one 16 KiB page), so this pin is a runtime vaddr, added to dlpi_addr only,
//     never a file offset.
// Gate-0 JSONs (frozen evidence, consumed by the Kotlin patch + mirrors test):
//   patches/src/main/resources/steamlink/androidxr/gxr_spacewarp_gate0_5002363.json
//   patches/src/main/resources/steamlink/androidxr/gxr_spacewarp_gate0_5001712.json
//   (regenerate with python -B tests/derive_gate0.py --base <5002363|5001712>;
//   never full-file-sha gating; a base's layout is never transplanted).
//   - 2.0.20/5001712 (brief 13, fresh per-base derivation): identity anchor
//     0xfd304 (this base's codecs-support audit window; the 5002363 window
//     0xfd6fc/f5c43c6a... hashes to 692664de... on this lib, so the bases do
//     NOT share anchor bytes), slot 0x21bb88 (single JUMP_SLOT, exactly one
//     .dynsym import, occurrence count 1; same non-identity writables).

#include <android/log.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../../codecs-support-layer/src/sha256.h"  // repo-bundled header (allowed reuse)

// SwpWrapEglCreateImageKHR: the eglCreateImageKHR wrapper installed into the slot
// (defined in swp_egl.cpp); SwpEglSetOriginal: original-pointer handover.
extern "C" void* SwpWrapEglCreateImageKHR(void* dpy, void* ctx, unsigned target, void* buffer,
                                          const int* attrs);
extern "C" void SwpEglSetOriginal(void* originalCreateImageKHR);

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "GXRSwarp", __VA_ARGS__)

namespace {

constexpr const char* kSceneSoname = "libvrlink_scene.so";
constexpr size_t kAnchorSize = 0x80;

struct GOTProfile {
    const char* build;              // exact (versionName, versionCode) the table covers
    uintptr_t anchorOffset;         // runtime offset (code segment is PT_LOAD-identity)
    const char* anchorSha256;       // SHA-256 of bytes[anchorOffset..+kAnchorSize)
    uintptr_t slotEglCreateImageKHR;  // RUNTIME vaddr of the eglCreateImageKHR GOT slot
};

// 2.0.23/5002363 first (09-LAYER-B1b). Values re-derived by tests/derive_gate0.py
// [verified 2026-10-17 against android-steamlinkvr-release-base-2.0.23-5002363; the
// anchor sha is also codecs-support-layer's pinned anchor for the same base].
const GOTProfile kProfile5002363 = {
    "2.0.23/5002363",
    0xfd6fc,
    "f5c43c6aacb69e61c3f929f361612e1b91bb509803d2c14824b9601b3dd8c795",
    0x22fcc8,
};

// 2.0.20/5001712 second (brief 13-PORT-5001712-C1): fresh per-base derivation by
// tests/derive_gate0.py --base 5001712 [verified 2026-10-17 against
// android-steamlinkvr-release-base-2.0.20-5001712] - the identity anchor is THIS
// base's codecs-support audit window (0xfd304; independently cross-checked
// against codecs-support-layer's frozen anchor for the same base), and the
// slot is this base's own JUMP_SLOT.
const GOTProfile kProfile5001712 = {
    "2.0.20/5001712",
    0xfd304,
    "6e5dcb4f5094763e948388ca35c6132af242159fc3c22e812f0fe2f4ab4f2649",
    0x21bb88,
};

// Runtime base selection (the codecs-support-layer anchor-identity pattern):
// the loop below hashes each profile's anchor window at the dl_iterate_phdr
// base in this order; the first matching profile wins; no match = the same
// fail-closed stand-down as before (unknown layouts never gain interception).
const GOTProfile* const kProfiles[] = {&kProfile5002363, &kProfile5001712};
constexpr size_t kProfileCount = 2;

std::atomic<int> g_state{0};  // 0 = pending, 1 = armed, 2 = failed closed / disabled
std::atomic<bool> g_busy{false};
uintptr_t g_sceneBase = 0;

int PhdrCallback(struct dl_phdr_info* info, size_t, void*) {
    if (info->dlpi_name && std::strstr(info->dlpi_name, kSceneSoname)) {
        g_sceneBase = static_cast<uintptr_t>(info->dlpi_addr);
        return 1;
    }
    return 0;
}

#if defined(SWP_HOST_TESTS)
// Host-test hooks: drive the mechanism with synthetic bytes/layout instead of the
// (undistributable) scene binary. Never compiled into the shipped layer.
extern "C" {
GOTProfile g_testProfile{};
void SwpHooksTestSetProfile(const char* build, uintptr_t anchorOffset, const char* anchorSha256,
                            uintptr_t slotOffset) {
    g_testProfile = GOTProfile{build, anchorOffset, anchorSha256, slotOffset};
}
void SwpHooksTestResetState() { g_state.store(0); }
void SwpHooksTestSetBase(uintptr_t base) { g_sceneBase = base; }
}  // extern "C"
#endif

}  // namespace

extern "C" bool SwpHooksArmed() { return g_state.load() == 1; }

extern "C" bool SwpHooksArm() {
    int current = g_state.load();
    if (current != 0) return current == 1;
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return false;

#if !defined(SWP_HOST_TESTS)
    g_sceneBase = 0;
    dl_iterate_phdr(PhdrCallback, nullptr);
    if (!g_sceneBase) {
        g_busy.store(false);  // scene lib not loaded yet: retried from later XR calls
        return false;
    }
#endif

    // Anchor verification: fail closed when no pinned base layout matches.
    const GOTProfile* chosen = nullptr;
    uint8_t digest[32];
    char hex[65] = {0};
#if defined(SWP_HOST_TESTS)
    // Host tests drive the mechanism with synthetic bytes/layout (profile + base
    // preset above via the test hooks); the shipped loop below never compiles in.
    gxr::Sha256::Digest(reinterpret_cast<const void*>(g_sceneBase + g_testProfile.anchorOffset),
                        kAnchorSize, digest);
    gxr::Sha256::Hex(digest, hex);
    if (std::strcmp(hex, g_testProfile.anchorSha256) == 0) chosen = &g_testProfile;
#else
    for (size_t i = 0; i < kProfileCount; i++) {
        const GOTProfile* profile = kProfiles[i];
        gxr::Sha256::Digest(reinterpret_cast<const void*>(g_sceneBase + profile->anchorOffset),
                            kAnchorSize, digest);
        gxr::Sha256::Hex(digest, hex);
        if (std::strcmp(hex, profile->anchorSha256) == 0) {
            chosen = profile;
            break;
        }
    }
#endif
    if (!chosen) {
        LOG("libvrlink_scene.so matches no pinned base layout; no interception (fail closed)");
        g_state.store(2);
        g_busy.store(false);
        return false;
    }

    // The eglCreateImageKHR slot must have been resolved by the dynamic linker by
    // now (BIND_NOW + full RELRO): a null means unknown GOT state -> fail closed.
    void* original = *reinterpret_cast<void**>(g_sceneBase + chosen->slotEglCreateImageKHR);
    if (!original) {
        LOG("libvrlink_scene.so GOT slot for eglCreateImageKHR unresolved; "
            "no interception (fail closed)");
        g_state.store(2);
        g_busy.store(false);
        return false;
    }
    SwpEglSetOriginal(original);

    const long pageSize = sysconf(_SC_PAGESIZE);
    const uintptr_t slotAddr = g_sceneBase + chosen->slotEglCreateImageKHR;
    const uintptr_t mapStart = slotAddr & ~static_cast<uintptr_t>(pageSize - 1);
    const uintptr_t mapEnd =
        (slotAddr + sizeof(void*) + static_cast<uintptr_t>(pageSize) - 1) &
        ~static_cast<uintptr_t>(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(mapStart), mapEnd - mapStart,
                 PROT_READ | PROT_WRITE) != 0) {
        LOG("mprotect(RW) on the scene GOT failed; no interception (fail closed)");
        g_state.store(2);
        g_busy.store(false);
        return false;
    }
    *reinterpret_cast<void**>(slotAddr) =
        reinterpret_cast<void*>(&SwpWrapEglCreateImageKHR);
    mprotect(reinterpret_cast<void*>(mapStart), mapEnd - mapStart, PROT_READ);

    g_state.store(1);
    g_busy.store(false);
    LOG("interception installed (%s): eglCreateImageKHR GOT slot 0x%zx -> arrival learning",
        chosen->build, chosen->slotEglCreateImageKHR);
    return true;
}
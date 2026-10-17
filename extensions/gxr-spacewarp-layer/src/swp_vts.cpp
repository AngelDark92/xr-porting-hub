// gxr-spacewarp: VTS bridge consumer (brief 10-W2A-LAYER-WIRING).
//
// One boolean flows host -> client over the vrlink typed ValueTransportSystem
// channel (receipt 06-BRIDGE: the wire type 7 "Value Transport", decoded record
// codec, receiver auto-instantiates unknown value ids 1..0x73). The owner chose
// the carrier id 0x48 (72); the host's in-process VrlinkTap publishes a 4-byte
// int on it; THIS module consumes it on the client with NO stock APK edit:
//
//   * SVLRoot* capture: the layer GOT-patches two PLT-routed SVLRoot methods
//     of libvrlink_scene.so (SVLRoot::ParsePeerValueSettings — receipt 06's
//     named recipe — and SVLRoot::RXUnreliableStream, which fires per unreliable
//     datagram during any active session). Their wrappers capture x0 (the
//     SVLRoot*, exactly the `this` the client itself passes) and forward
//     verbatim to the original. One-shot per session; the pointer is
//     invalidated at xrDestroySession (never read a destroyed root).
//   * The enable read calls the base's pinned ValueTransportSystem read API
//     DIRECTLY - 5002363: ValueTransportSystem::BGetRemoteValue (runtime vaddr
//     0x179ee0); 5001712: ValueTransportSystem::GetRemoteValue (runtime vaddr
//     0x16a2c4) - with the VTS object embedded at SVLRoot + 0xa08 (derived from
//     each base's real call-site add+bl pairs; identical 0xa08 on both audited
//     bases). Signatures and semantics are re-derived from the receipt
//     disassembly + the frozen 0x80-byte anchors (Gate-0 appendices
//     gxr_spacewarp_vts_5002363.json / gxr_spacewarp_vts_5001712.json, emitted
//     by tests/derive_gate0_vts.py --base <base>):
//         shared register shape: x0 = the VTS (SVLRoot+0xa08), w1 = id, x2 =
//         out, w3 = size.
//       5002363 (bool present): bool in w0 - true ONLY when the id's holder
//       exists AND the FULL requested size was copied (a size/type mismatch
//       reads as "not present"); takes/releases the VTS mutex (this+0x1a78).
//       5001712 (copied byte count): u32 in w0 - the copied byte count, with
//       present == copied == requested (0xffffffff = the holder is absent; a
//       partial copy also reads as "not present"); locks/unlocks the VTS
//       mutex at this+0xd8 via the 0x208a40/0x208a50 PLT stubs.
//       Both mutex shapes make the call safe from the render thread at
//       ~1.5 Hz. The profile's ReadReturn field carries the policy.
//   * Cadence: one carrier read every 60 endframes (cheap). Semantics: 4-byte
//     int, nonzero = enabled. Read failure / never published (unknown) =>
//     treated as disabled; the layer NEVER auto-enables on stock sessions
//     ("carrier value not present" is logged once per session).
//   * Dispatch gate (swp_dispatch.cpp): ALL synthesis (MV generation +
//     extrapolation) runs only while this module reads the carrier enabled;
//     everything else stays the stock passthrough path (the layer's observers
//     keep learning). Stand-down semantics unchanged.
//
// Fail-safe model (same class as the codecs-support layer / swp_hooks): any
// anchor mismatch, unresolved slot, mprotect failure or missing capture =>
// no/last interception, cached disabled, one log line per session. The GOT
// writes here are ADDITIVE to swp_hooks' eglCreateImageKHR install and are
// independently gated: a bridge-arm failure never disturbs arrival learning
// (and vice versa); the worst case equals stock passthrough.
//
// The audited scene libs carry NO enforced BTI/PA marker [derived 2026-10-17:
// 5002363 has no PT_GNU_PROPERTY segment at all; 5001712's PT_GNU_PROPERTY
// region is not a valid GNU properties note (no GNU name record, no
// FEATURE_1_AND descriptor) and none of the pinned targets start with a bti
// pad], so an indirect call to a non-landing-pad address is safe, and the
// layer itself builds without branch protection (no marking in
// CMakeLists.txt/Build-Native.ps1). If a future scene lib becomes BTI-marked
// the anchors/slots re-derive first (the derive script fails closed on a valid
// BTI feature record) and the direct call must be re-evaluated BEFORE shipping
// that base.
//
// Threading: SwpVtsNoteEndFrame runs only inside the dispatch's xrEndFrame
// critical section (its recursive_mutex serializes the cadence counter and the
// poll); the capture wrappers run on the client's RX thread and touch only the
// atomic svlRoot pointer; arm() latches atomically.

#include <android/log.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../../codecs-support-layer/src/sha256.h"  // repo-bundled header (allowed reuse)

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "GXRSwarp", __VA_ARGS__)

extern "C" void SwpWrapVtsParsePeerValueSettings(void* self, const void* entries, int count);
extern "C" void SwpWrapVtsRxUnreliableStream(void* self, void* dataset, void* link,
                                             unsigned long size);

namespace {

constexpr const char* kSceneSoname = "libvrlink_scene.so";
constexpr size_t kAnchorSize = 0x80;

// ---------------------------------------------------------------------------
// Pins — RE-DERIVABLE, per base, frozen in gxr_spacewarp_vts_5002363.json /
// gxr_spacewarp_vts_5001712.json by tests/derive_gate0_vts.py (regenerate with
// `python -B tests/derive_gate0_vts.py --base <5002363|5001712>`; `... --check`
// cross-verifies these constants against the derivation). Values re-derived
// from the decoded trees [verified 2026-10-17]. The bases differ in MORE than
// addresses: 2.0.20/5001712 has NO BGetRemoteValue - its read API is
// ValueTransportSystem::GetRemoteValue, which RETURNS THE COPIED BYTE COUNT
// (0xffffffff = id absent; present == copied == requested) instead of
// 5002363's full-copy bool; the profile therefore carries the returned-value
// policy. The runtime selection is the codecs-support pattern: hash each
// profile's base-anchor window at the dl_iterate_phdr base (order: 5002363
// first, 5001712 second), first match wins, no match = latched fail closed.
// ---------------------------------------------------------------------------

// The carrier (owner decision per brief 10; unused-id + guard checks derive
// PER BASE): the carrier id travels the same wire on both bases.
constexpr uint32_t kCarrierId = 0x48;
constexpr uint32_t kCarrierPayloadBytes = 4;
constexpr uint32_t kReadCadenceEndframes = 60;

// The per-base read-API returned-value policy (derived per base):
enum class ReadReturn : uint8_t {
    kBoolPresentFullCopy,  // 5002363 BGetRemoteValue: bool, true = present && full copy
    kCopiedByteCount,      // 5001712 GetRemoteValue: u32 copied bytes; 0xffffffff = absent
};

// The per-base profile record (codecs-support anchor-identity pattern). All
// runtime vaddrs: anchor window offsets are runtime offsets in the code
// segment (PT_LOAD-identity on both bases); capture/write slots are GOT
// runtime vaddrs added to dlpi_addr.
struct VtsProfile {
    const char* build;             // exact (versionName, versionCode) the profile covers
    uintptr_t baseAnchorOffset;    // the scene-lib identity anchor swp_hooks pins too
    const char* baseAnchorSha256;
    const char* readApiName;       // the per-base read API (for logs only)
    uintptr_t readVa;              // the read API's runtime vaddr (code segment)
    const char* readAnchorSha256;
    ReadReturn readReturn;         // the returned-value policy (ABI derived per base)
    // The one-shot capture methods (each: PLT-routed intra-lib call, x0 = SVLRoot*):
    uintptr_t captureParsePeerVa;
    const char* captureParsePeerSha256;
    uintptr_t captureParsePeerSlot;  // RUNTIME vaddr (GOT)
    uintptr_t captureRxUnreliableVa;
    const char* captureRxUnreliableSha256;
    uintptr_t captureRxUnreliableSlot;  // RUNTIME vaddr (GOT)
    // The embedded VTS object offset inside SVLRoot, proven from the lib's real
    // call-site add+bl pairs (both bases derive 0xa08):
    uintptr_t vtsEmbedOffset;
};

const VtsProfile kVtsProfile5002363 = {
    "2.0.23/5002363",
    0xfd6fc,
    "f5c43c6aacb69e61c3f929f361612e1b91bb509803d2c14824b9601b3dd8c795",
    "BGetRemoteValue",
    0x179ee0,
    "0cd07a66d3af4994fad023fe60fa0b58df47f7c5bbb56fe4b77fbe6b409f9536",
    ReadReturn::kBoolPresentFullCopy,
    0x16c60c,
    "64e1e4568700c13a8b5c25a5582342146cc220340387434ece7bc80a726c7c03",
    0x22f378,  // RUNTIME vaddr (GOT)
    0x16c4ac,
    "f3e3ff1f08170dd8bfd01ac7e93dddf7758ffc9036a9a9e9da0f26ca6334175f",
    0x22f2e0,  // RUNTIME vaddr (GOT)
    0xa08,     // SVLRoot + 0xa08 (proven at 0xfc0c8/0xfc32c)
};

// 2.0.20/5001712 (brief 13-PORT-5001712-C1): fresh per-base derivation
// [verified 2026-10-17 against android-steamlinkvr-release-base-2.0.20-5001712]:
// identity anchor 0xfd304 (this base's codecs-support audit window; the bytes
// at 5002363's 0xfd6fc anchor offset hash differently here, so the anchor pair
// disambiguates the bases); read API GetRemoteValue @ 0x16a2c4 (136 bytes, bool
// -> copied-byte-count policy); same named capture methods at their own VAs
// (note: this base's RXUnreliableStream signature is (dataset*, SVLDataLink*) -
// no trailing unsigned m - the 4-arg wrapper forwards the unused 4th register
// arg, which the callee never reads); the same 0xa08 embed (proven at 22 real
// call sites; canonical pairs frozen in the appendix JSON).
const VtsProfile kVtsProfile5001712 = {
    "2.0.20/5001712",
    0xfd304,
    "6e5dcb4f5094763e948388ca35c6132af242159fc3c22e812f0fe2f4ab4f2649",
    "GetRemoteValue",
    0x16a2c4,
    "b5b407c66d1cbe593f161a816ecc7074375627f8ce0b533f83c9a90d54bffbf8",
    ReadReturn::kCopiedByteCount,
    0x164b8c,
    "cb57aa43cab2f8047490fe0d0ae37fad53273c40e54e0a0a3aed0f8940355150",
    0x21b298,  // RUNTIME vaddr (GOT)
    0x164a44,
    "d16e10075b262462aaa29dfcd40a81bcb14070863efd794d5253b7f2c1364f9a",
    0x21b1e0,  // RUNTIME vaddr (GOT)
    0xa08,     // SVLRoot + 0xa08 (census: 22 real call sites, one immediate)
};

// Order of tries (brief 13): 5002363 first (the existing base), 5001712 second.
const VtsProfile* const kVtsProfiles[] = {&kVtsProfile5002363, &kVtsProfile5001712};
constexpr size_t kVtsProfileCount = 2;

// bool (*)(VTS* obj, unsigned id, void* out, unsigned size) — the shared
// AArch64 argument shape of the re-derived read APIs (this, id, out, size).
// 5002363's BGetRemoteValue returns bool in w0 (present && full copy);
// 5001712's GetRemoteValue returns the copied byte count in w0 (0xffffffff =
// absent) - the policy distinction is the profile's ReadReturn field.
using BGetRemoteValueFn = bool (*)(void*, uint32_t, void*, uint32_t);
using CaptureParseFn = void (*)(void*, const void*, int);                    // (this, const TransportValue_t*, int)
using CaptureRxUnreliableFn = void (*)(void*, void*, void*, unsigned long);  // (this, dataset*, SVLDataLink*, m)
// NOTE per base: 5002363's RXUnreliableStream really takes a 4th `unsigned long`;
// 5001712's takes only 3 args (no trailing m). The two-arg-tail shape is the
// same registers; a 4-arg call into the 3-arg function passes x3 which the
// callee never reads (AArch64 register ABI) - safe, and documented in both
// appendix JSONs.

constexpr int kVtsStatePending = 0;
constexpr int kVtsStateArmed = 1;
constexpr int kVtsStateFailed = 2;  // latched: never retried (fail-closed base)

constexpr int kCarrierUnknown = 0;
constexpr int kCarrierOff = 1;         // published 0 (the host actively disables)
constexpr int kCarrierOn = 2;          // published nonzero
constexpr int kCarrierUnpublished = 3; // BGetRemoteValue returned "not present"

std::atomic<int> g_state{kVtsStatePending};
std::atomic<bool> g_busy{false};
std::atomic<uintptr_t> g_svlRoot{0};
std::atomic<int> g_carrier{kCarrierUnknown};

// Per-session one-line log flags (reset by SwpVtsResetSession).
std::atomic<bool> g_notPresentLogged{false};
std::atomic<bool> g_notCapturedLogged{false};
std::atomic<bool> g_notArmedLogged{false};

uint64_t g_endframeCounter = 0;  // poll cadence (serialized by the dispatch's EndFrame)
BGetRemoteValueFn g_bGetRemoteValue = nullptr;
CaptureParseFn g_originalParsePeer = nullptr;
CaptureRxUnreliableFn g_originalRxUnreliable = nullptr;
uintptr_t g_sceneBase = 0;
// Profile-derived state, latched at arm time. The defaults are the 5002363
// profile's values (the original single-base pins), which keeps the SWP_HOST_TESTS
// flow tests (which arm without a real scene lib) on the exact behavior the
// tests pinned (root + 0xa08); the shipped arm() always overwrites them.
uintptr_t g_vtsEmbedOffset = 0xa08;
ReadReturn g_readReturn = ReadReturn::kBoolPresentFullCopy;

int PhdrCallback(struct dl_phdr_info* info, size_t, void*) {
    if (info->dlpi_name && std::strstr(info->dlpi_name, kSceneSoname)) {
        g_sceneBase = static_cast<uintptr_t>(info->dlpi_addr);
        return 1;
    }
    return 0;
}

// QUIET anchor hash (bytes at g_sceneBase + offset vs the pinned sha); the
// base-anchor selection uses this so an unknown layout logs exactly ONE line
// (the same "matches no pinned base layout" shape).
bool AnchorShaMatches(uintptr_t offset, const char* expectedSha) {
    uint8_t digest[32];
    char hex[65] = {0};
    gxr::Sha256::Digest(reinterpret_cast<const void*>(g_sceneBase + offset), kAnchorSize,
                        digest);
    gxr::Sha256::Hex(digest, hex);
    return std::strcmp(hex, expectedSha) == 0;
}

// Runtime base selection (the codecs-support-layer anchor-identity pattern):
// hash each profile's base-anchor window at the dl_iterate_phdr base in order
// of tries; the first matching profile wins; none returns null (caller logs
// once + latches fail closed).
const VtsProfile* SelectVtsProfile() {
    for (size_t i = 0; i < kVtsProfileCount; i++) {
        const VtsProfile* profile = kVtsProfiles[i];
        if (AnchorShaMatches(profile->baseAnchorOffset, profile->baseAnchorSha256)) {
            return profile;
        }
    }
    return nullptr;
}

bool AnchorMatches(uintptr_t offset, const char* expectedSha, const char* label) {
    uint8_t digest[32];
    char hex[65] = {0};
    gxr::Sha256::Digest(reinterpret_cast<const void*>(g_sceneBase + offset), kAnchorSize,
                        digest);
    gxr::Sha256::Hex(digest, hex);
    if (std::strcmp(hex, expectedSha) == 0) return true;
    LOG("layout anchor mismatch for %s (re-derived %s); no capture (fail closed)", label,
        hex);
    return false;
}

// One GOT slot: read the resolved original (BIND_NOW + full RELRO), hand it over
// FIRST (the wrapper must never observe a null original — the mprotect syscalls
// order that store before the GOT write, exactly like SwpEglSetOriginal), then
// mprotect the page RW, write the wrapper, restore PROT_READ. Returns false on
// any failure (fail closed; the slot stays untouched when the original is
// unresolved).
bool PatchCaptureSlot(uintptr_t slotRuntimeVaddr, void* wrapper, const char* label,
                      void** outOriginal) {
    void* original = *reinterpret_cast<void**>(g_sceneBase + slotRuntimeVaddr);
    if (!original) {
        LOG("GOT slot for %s unresolved; capture not installed (fail closed)", label);
        return false;
    }
    *outOriginal = original;
    const long pageSize = sysconf(_SC_PAGESIZE);
    const uintptr_t slotAddr = g_sceneBase + slotRuntimeVaddr;
    const uintptr_t mapStart = slotAddr & ~static_cast<uintptr_t>(pageSize - 1);
    const uintptr_t mapEnd = (slotAddr + sizeof(void*) + static_cast<uintptr_t>(pageSize) - 1) &
                             ~static_cast<uintptr_t>(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(mapStart), mapEnd - mapStart,
                 PROT_READ | PROT_WRITE) != 0) {
        LOG("mprotect(RW) failed for %s; capture not installed (fail closed)", label);
        return false;
    }
    *reinterpret_cast<void**>(slotAddr) = wrapper;
    mprotect(reinterpret_cast<void*>(mapStart), mapEnd - mapStart, PROT_READ);
    return true;
}

// The carrier read (per-cadence). Tri-state caching per the brief: unknown
// (never published) => disabled; read failure/garbage type => disabled + one
// log line per session.
void PollCarrierLogged() {
    const int state = g_state.load();
    if (state != kVtsStateArmed) {
        if (!g_notArmedLogged.exchange(true)) {
            LOG("spacewarp bridge not armed (%s); carrier stays unknown => disabled",
                state == kVtsStateFailed ? "base layout unpinned" : "scene lib not loaded"
                                                                         " yet");
        }
        return;
    }
    const uintptr_t root = g_svlRoot.load();
    if (!root || !g_bGetRemoteValue) {
        if (!g_notCapturedLogged.exchange(true)) {
            LOG("SVLRoot not captured yet (no live vrlink session); carrier stays"
                " unknown => disabled");
        }
        g_carrier.store(kCarrierUnknown);
        return;
    }
    // The carrier read (per-base returned-value policy, both shapes share the
    // register ABI: (VTS*, id, out, size)):
    //   - 5002363 BGetRemoteValue: bool in w0 - true only when the id holder
    //     exists AND the FULL requested size was copied;
    //   - 5001712 GetRemoteValue: the copied byte count in w0 (0xffffffff =
    //     the id's holder is absent; a partial copy < requested reads as "not
    //     present"). present means copied == the 4-byte request.
    uint32_t value = 0;
    const uint32_t got = g_bGetRemoteValue(
        reinterpret_cast<void*>(root + g_vtsEmbedOffset), kCarrierId, &value,
        kCarrierPayloadBytes);
    const bool present =
        g_readReturn == ReadReturn::kBoolPresentFullCopy
            ? got != 0
            : got == kCarrierPayloadBytes;
    if (!present) {
        g_carrier.store(kCarrierUnpublished);
        if (!g_notPresentLogged.exchange(true)) {
            LOG("carrier value not present (id 0x%x never published this session);"
                " spacewarp stays disabled", kCarrierId);
        }
        return;
    }
    const int next = value != 0 ? kCarrierOn : kCarrierOff;
    const int previous = g_carrier.exchange(next);
    if (previous != next) {
        LOG("bridge carrier 0x%x = %u (spacewarp %s)", kCarrierId, value,
            next == kCarrierOn ? "enabled" : "disabled");
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Internal ABI (this .so only; the dispatch and the capture wrappers):
// ---------------------------------------------------------------------------
extern "C" {

// One-shot SVLRoot* capture (the GOT wrappers call this; host tests drive it
// directly). Storing the latest capture rides reconnects: a re-created root
// replaces the stale pointer.
void SwpVtsRootObserved(void* self) {
    if (self) g_svlRoot.store(reinterpret_cast<uintptr_t>(self));
}

// Session lifecycle (dispatch-owned):
void SwpVtsResetSession() {
    g_notPresentLogged.store(false);
    g_notCapturedLogged.store(false);
    g_notArmedLogged.store(false);
    g_carrier.store(kCarrierUnknown);
    g_endframeCounter = 0;
}

void SwpVtsSessionEnded() {
    // The XR session is gone: never read a root that may be destroyed with it.
    g_svlRoot.store(0);
    g_carrier.store(kCarrierUnknown);
    g_endframeCounter = 0;
}

// Endframe cadence + the one cheap gate read (called inside the dispatch's
// xrEndFrame critical section; the counter is serialized there).
void SwpVtsNoteEndFrame() {
    if (++g_endframeCounter < kReadCadenceEndframes) return;
    g_endframeCounter = 0;
    PollCarrierLogged();
}

// The synthesis gate (cheap, lock-free): true only while the carrier reads ON.
bool SwpVtsSynthesisAllowed() { return g_carrier.load() == kCarrierOn; }

// Arm once the scene lib is loaded (retried from XR calls like the egl hook).
// Independent of SwpHooksArm: a capture failure never disturbs arrival learning
// (a separate GOT install with its own fail-closed gate; both install paths
// verify the SAME base anchor first).
bool SwpVtsArm() {
    int current = g_state.load();
    if (current != kVtsStatePending) return current == kVtsStateArmed;
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return false;
    // Log context (host tests arm without a real scene lib: report the original
    // 5002363 profile there; the shipped path re-selects below).
    const VtsProfile* profile = &kVtsProfile5002363;

#if !defined(SWP_HOST_TESTS)
    g_sceneBase = 0;
    dl_iterate_phdr(PhdrCallback, nullptr);
    if (!g_sceneBase) {
        g_busy.store(false);  // scene lib not loaded yet: retried from later XR calls
        return false;
    }
    // Base identity first (codecs-support pattern: hash each profile's base
    // anchor at the scanned base; 5002363 tried first, then 5001712), then the
    // chosen profile's bridge-specific anchors: fail at any step => latched
    // failure, no writes. An unknown layout fails closed exactly as before.
    profile = SelectVtsProfile();
    if (!profile) {
        LOG("libvrlink_scene.so matches no pinned base layout; no VTS capture"
            " (fail closed)");
        g_state.store(kVtsStateFailed);
        g_busy.store(false);
        return false;
    }
    if (!AnchorMatches(profile->baseAnchorOffset, profile->baseAnchorSha256,
                       "scene-lib base anchor") ||
        !AnchorMatches(profile->readVa, profile->readAnchorSha256,
                       profile->readApiName) ||
        !AnchorMatches(profile->captureParsePeerVa, profile->captureParsePeerSha256,
                       "SVLRoot::ParsePeerValueSettings") ||
        !AnchorMatches(profile->captureRxUnreliableVa, profile->captureRxUnreliableSha256,
                       "SVLRoot::RXUnreliableStream")) {
        g_state.store(kVtsStateFailed);
        g_busy.store(false);
        return false;
    }
    // Latch the profile-derived constants BEFORE any GOT write (the poll can
    // only run once the state flips to armed; the order keeps a partially
    // armed state unusable either way).
    g_vtsEmbedOffset = profile->vtsEmbedOffset;
    g_readReturn = profile->readReturn;
    if (!PatchCaptureSlot(profile->captureParsePeerSlot,
                          reinterpret_cast<void*>(&SwpWrapVtsParsePeerValueSettings),
                          "ParsePeerValueSettings",
                          reinterpret_cast<void**>(&g_originalParsePeer))) {
        g_state.store(kVtsStateFailed);
        g_busy.store(false);
        return false;
    }
    if (!PatchCaptureSlot(profile->captureRxUnreliableSlot,
                          reinterpret_cast<void*>(&SwpWrapVtsRxUnreliableStream),
                          "RXUnreliableStream",
                          reinterpret_cast<void**>(&g_originalRxUnreliable))) {
        g_state.store(kVtsStateFailed);
        g_busy.store(false);
        return false;
    }
    g_bGetRemoteValue = reinterpret_cast<BGetRemoteValueFn>(g_sceneBase + profile->readVa);
#endif

    g_state.store(kVtsStateArmed);
    g_busy.store(false);
    LOG("bridge armed on %s: carrier 0x%x direct-read via %s (runtime vaddr"
        " 0x%zx); synthesis stays disabled until the host publishes the carrier",
        profile->build, kCarrierId, profile->readApiName,
        static_cast<size_t>(profile->readVa));
    return true;
}

// The GOT capture wrappers (installed into the scene lib only after BOTH
// originals were handed over, so a concurrent call can never see a null
// original). Forward verbatim, then capture x0.
void SwpWrapVtsParsePeerValueSettings(void* self, const void* entries, int count) {
    SwpVtsRootObserved(self);
    if (!g_originalParsePeer) return;  // unreachable in the shipped flow (fail safe)
    g_originalParsePeer(self, entries, count);
}

void SwpWrapVtsRxUnreliableStream(void* self, void* dataset, void* link, unsigned long size) {
    SwpVtsRootObserved(self);
    if (!g_originalRxUnreliable) return;  // unreachable in the shipped flow (fail safe)
    g_originalRxUnreliable(self, dataset, link, size);
}

#ifdef SWP_HOST_TESTS
// Host-test hooks: the (undistributable) scene binary is replaced by test-
// driven state + a mock BGetRemoteValue ("mocked table") + wrapper drives.
void SwpVtsTestSetTable(BGetRemoteValueFn fn) { g_bGetRemoteValue = fn; }
void SwpVtsTestSetOriginals(CaptureParseFn parsePeer, CaptureRxUnreliableFn rxUnreliable) {
    g_originalParsePeer = parsePeer;
    g_originalRxUnreliable = rxUnreliable;
}
void SwpVtsTestSetArmed(bool armed) {
    g_state.store(armed ? kVtsStateArmed : kVtsStateFailed);
}
void SwpVtsTestSetRoot(void* root) { SwpVtsRootObserved(root); }
// Preset the carrier cache as if a poll had already read it (the flow tests
// model "the host published the carrier before the session ticks").
void SwpVtsTestPublish(uint32_t value) {
    g_carrier.store(value != 0 ? kCarrierOn : kCarrierOff);
}
void SwpVtsTestResetAll() {
    g_state.store(kVtsStatePending);
    g_svlRoot.store(0);
    g_carrier.store(kCarrierUnknown);
    g_notPresentLogged.store(false);
    g_notCapturedLogged.store(false);
    g_notArmedLogged.store(false);
    g_bGetRemoteValue = nullptr;
    g_originalParsePeer = nullptr;
    g_originalRxUnreliable = nullptr;
    g_endframeCounter = 0;
}
#endif  // SWP_HOST_TESTS

}  // extern "C"
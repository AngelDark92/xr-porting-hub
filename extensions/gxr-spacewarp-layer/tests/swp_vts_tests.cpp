// Host tests for the gxr-spacewarp VTS bridge consumer (brief 10-W2A half).
//
// Compiles the PRODUCTION swp_vts.cpp (SWP_HOST_TESTS) against:
//   - a MOCKED BGetRemoteValue table (the value-transport read the real layer
//     makes as a pinned direct call into the undistributable scene lib),
//   - fake capture originals + direct drives of the two GOT capture wrappers,
//   - the stubbed Android log / link / mmap headers of tests/stubs/.
// Proven here (brief 10 + receipt 06 semantics):
//   - the layer NEVER auto-enables on stock sessions: unknown (no capture, no
//     carrier) and never-published (BGetRemoteValue false) both read disabled,
//     each with ONE log line per session;
//   - published 0 = disabled, published nonzero = enabled, live flips take
//     effect on the next cadence poll;
//   - the "garbage type" family (BGetRemoteValue returning false for a size
//     mismatch) reads as not-present = disabled;
//   - the carrier read happens every 60 endframes (cadence, not per tick);
//   - the read carries (root + 0xa08, 0x48, out, 4) — the VTS embed offset and
//     carrier pins;
//   - the capture wrappers forward AND capture the latest SVLRoot*;
//   - SwpVtsSessionEnded invalidates the root (no reads on a dead root) and
//     SwpVtsResetSession restarts the per-session one-line logs.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/swp_vts.cpp"

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond, label)                                                     \
    do {                                                                       \
        g_checks++;                                                            \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            std::printf("FAIL: %s (line %d)\n", label, __LINE__);              \
        }                                                                      \
    } while (0)

// --- Android log stub body (records lines) ---
std::vector<std::string> g_logLines;
extern "C" int __android_log_print(int, const char*, const char* format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_logLines.push_back(line);
    return 1;
}

// --- mprotect/sysconf stubs (the real ones never run under host tests) ---
extern "C" int mprotect(void*, size_t, int) { return -1; }
extern "C" long sysconf(int) { return 4096; }

// ---------------------------------------------------------------------------
// Mocked value-transport table + capture fakes.
// ---------------------------------------------------------------------------
static int g_mockReads = 0;
static bool g_mockPresent = false;
static bool g_mockTypeMismatch = false;  // "garbage type": holder size != 4 => false
static uint32_t g_mockValue = 0;
static void* g_mockLastVts = nullptr;
static uint32_t g_mockLastId = 0;
static uint32_t g_mockLastSize = 0;

extern "C" bool MockBGetRemoteValue(void* vts, uint32_t id, void* out, uint32_t size) {
    g_mockReads++;
    g_mockLastVts = vts;
    g_mockLastId = id;
    g_mockLastSize = size;
    if (!g_mockPresent) return false;       // never published this session
    if (g_mockTypeMismatch) return false;   // the holder's size/type garbage family
    if (out) *static_cast<uint32_t*>(out) = g_mockValue;
    return true;
}

static int g_forwardParse = 0;
static int g_lastParseCount = -1;
static void* g_captureFromParse = nullptr;
extern "C" void FakeParseOriginal(void*, const void*, int count) {
    g_forwardParse++;
    g_lastParseCount = count;
}

static int g_forwardRx = 0;
static unsigned long g_lastRxSize = 0;
static void* g_captureFromRx = nullptr;
extern "C" void FakeRxOriginal(void*, void*, void*, unsigned long size) {
    g_forwardRx++;
    g_lastRxSize = size;
}

static void ResetAll() {
    g_checks = 0;
    g_failures = 0;
    g_logLines.clear();
    g_mockReads = 0;
    g_mockPresent = false;
    g_mockTypeMismatch = false;
    g_mockValue = 0;
    g_mockLastVts = nullptr;
    g_mockLastId = 0;
    g_mockLastSize = 0;
    g_forwardParse = 0;
    g_lastParseCount = -1;
    g_captureFromParse = nullptr;
    g_forwardRx = 0;
    g_lastRxSize = 0;
    g_captureFromRx = nullptr;
    SwpVtsTestResetAll();
    SwpVtsTestSetTable(&MockBGetRemoteValue);
}

static bool LogHas(const char* needle) {
    for (const std::string& line : g_logLines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

static const void* kFakeRoot = reinterpret_cast<const void*>(0x5A1E);

// Session-scoped drive of one cadence poll (60 endframes; the 60th ticks it).
static void PollOnce() {
    for (int i = 0; i < 60; i++) SwpVtsNoteEndFrame();
}

int main() {
    // 1. Unarmed (nothing captured): the gate reads disabled, one log line.
    ResetAll();
    CHECK(!SwpVtsSynthesisAllowed(), "stock process: the gate never reads enabled");
    PollOnce();
    CHECK(!SwpVtsSynthesisAllowed(), "unarmed bridge keeps synthesis disabled");
    CHECK(LogHas("bridge not armed"), "unarmed logs once per session");
    g_logLines.clear();

    // 2. Armed + root captured + carrier never published: disabled + ONE line.
    ResetAll();
    CHECK(SwpVtsArm(), "host-test arm latches armed");
    SwpVtsTestSetRoot(const_cast<void*>(kFakeRoot));
    PollOnce();
    CHECK(!SwpVtsSynthesisAllowed(), "never-published carrier stays disabled");
    CHECK(g_mockReads == 1, "the poll read exactly once per 60 endframes");
    CHECK(LogHas("carrier value not present"), "not-present logged once");
    g_logLines.clear();
    PollOnce();  // another full cadence with the same state: no new line
    CHECK(!SwpVtsSynthesisAllowed(), "still disabled after repeat polls");
    CHECK(g_logLines.empty(), "the not-present line is once per session, not per poll");

    // 3. Carrier read semantics: (VTS*, 0x48, out, 4) pin shape + published-on.
    ResetAll();
    CHECK(SwpVtsArm(), "arm for the pin-shape suite");
    SwpVtsTestSetRoot(const_cast<void*>(kFakeRoot));
    g_mockPresent = true;
    g_mockValue = 1;
    PollOnce();
    CHECK(SwpVtsSynthesisAllowed(), "published nonzero enables synthesis");
    CHECK(g_mockReads == 1, "published carrier read once");
    CHECK(g_mockLastId == 0x48, "the carrier id pin (0x48) rides the read");
    CHECK(g_mockLastSize == 4, "the 4-byte payload pin rides the read");
    CHECK(g_mockLastVts == reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(kFakeRoot) +
                                                  0xa08),
          "the read targets the VTS object (SVLRoot + 0xa08)");
    CHECK(LogHas("bridge carrier 0x48 = 1"), "the first published value logs");

    // 4. Published 0: actively disabled; live flip back to nonzero re-enables.
    ResetAll();
    SwpVtsTestSetArmed(true);
    SwpVtsTestSetRoot(const_cast<void*>(kFakeRoot));
    g_mockPresent = true;
    PollOnce();
    CHECK(!SwpVtsSynthesisAllowed(), "published 0 disables synthesis");
    g_mockValue = 7;  // host reconfigure: nonzero again (live within the cadence)
    PollOnce();
    CHECK(SwpVtsSynthesisAllowed(), "a live flip to nonzero re-enables");
    g_mockValue = 0;
    PollOnce();
    CHECK(!SwpVtsSynthesisAllowed(), "a live flip to zero disables");

    // 5. Garbage type / size mismatch family: BGetRemoteValue false => disabled
    //    (the same not-present cache + one line per session).
    ResetAll();
    SwpVtsTestSetArmed(true);
    SwpVtsTestSetRoot(const_cast<void*>(kFakeRoot));
    g_mockPresent = true;
    g_mockValue = 1;
    g_mockTypeMismatch = true;  // the holder's size does not match the 4-byte read
    PollOnce();
    CHECK(!SwpVtsSynthesisAllowed(), "a garbage-type read is treated as not present");
    CHECK(LogHas("carrier value not present"), "garbage-type reads as not present");
    CHECK(g_mockLastSize == 4, "the layer keeps requesting the pinned 4-byte payload");

    // 6. Capture wrappers forward verbatim AND capture the latest root.
    ResetAll();
    SwpVtsTestSetArmed(true);
    SwpVtsTestSetOriginals(&FakeParseOriginal, &FakeRxOriginal);
    const void* otherRoot = reinterpret_cast<const void*>(0xD00D);
    SwpVtsTestSetRoot(const_cast<void*>(kFakeRoot));
    g_mockPresent = true;
    g_mockValue = 1;
    SwpVtsTestSetRoot(const_cast<void*>(otherRoot));  // the latest capture wins
    SwpWrapVtsParsePeerValueSettings(const_cast<void*>(kFakeRoot), nullptr, 9);
    SwpWrapVtsRxUnreliableStream(const_cast<void*>(otherRoot), nullptr, nullptr, 777);
    CHECK(g_forwardParse == 1 && g_lastParseCount == 9,
          "ParsePeerValueSettings wrapper forwards verbatim");
    CHECK(g_forwardRx == 1 && g_lastRxSize == 777,
          "RXUnreliableStream wrapper forwards verbatim");
    PollOnce();
    CHECK(SwpVtsSynthesisAllowed(), "captured root + published carrier enables");
    CHECK(g_mockLastVts == reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(otherRoot) +
                                                  0xa08),
          "the poll reads the LATEST captured root");

    // 7. Session ended: the root is invalidated (no reads on a dead object) and
    //    synthesis is disabled even though the mock would say published.
    SwpVtsSessionEnded();
    CHECK(!SwpVtsSynthesisAllowed(), "session end disables synthesis immediately");
    g_mockReads = 0;
    g_logLines.clear();
    PollOnce();
    CHECK(g_mockReads == 0 && !SwpVtsSynthesisAllowed(),
          "after session end the poll never touches the (dead) root again");
    CHECK(LogHas("SVLRoot not captured yet"), "the invalidated root logs once");

    // 8. ResetSession restarts the per-session one-line logs (a NEW session with
    //    the carrier published 0: disabled + the carrier line re-logged).
    g_logLines.clear();
    SwpVtsResetSession();
    SwpVtsTestSetRoot(const_cast<void*>(kFakeRoot));
    g_mockPresent = true;
    g_mockValue = 0;
    g_mockTypeMismatch = false;
    g_mockReads = 0;
    PollOnce();
    CHECK(!SwpVtsSynthesisAllowed(), "new session with published 0 stays disabled");
    CHECK(LogHas("bridge carrier 0x48 = 0"), "the new session re-logs the carrier");

    std::printf("gxr-spacewarp vts host tests: %d checks, %d failures\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
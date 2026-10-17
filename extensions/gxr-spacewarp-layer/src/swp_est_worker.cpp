// swp_est_worker.cpp — GL plumbing for the spacewarp estimator core.
//
// Program/shader compile cache (built once per init), one scratch FBO,
// per-pass uniform upload, dispatch, GL-error guard per pass (any error =>
// the call fails and the caller arms sticky stand-down; the caller's frame
// bindings are saved and restored around every public call), a single
// glFlush at the end of each public call, and no readback anywhere on the
// hot path (the GME/L1/median/H1 chain feeds all inter-stage data through
// textures, never glReadPixels).
//
// State discipline (RE-SHARED-IMPL.md + codecs-support-layer fail-safe shape):
//   - every pass re-attaches its target and re-checks FBO completeness;
//   - caller texture-sampler parameters are never touched (the extrapolate
//     pass samples caller textures with their own filter/wrap state);
//   - teardown deletes only what this engine created.

#include <cstdlib>
#include <cstring>

#include "spacewarp_est.h"
#include "swp_shaders.h"

namespace gxr {
namespace swp_internal {

#ifndef SWP_EST_LOG
#define SWP_EST_LOG(...) ((void)0)
#endif

namespace {

// ---------------------------------------------------------------- engine ---

struct Engine {
    const SwpGlTable* gl = nullptr;
    bool setupOk = false;
    bool haveQcom = false;

    // Two pipelines: MV-gen (gme -> sad -> median -> h1) and extrapolate.
    GLuint progGme = 0;
    GLuint progSad = 0;
    GLuint progMed = 0;
    GLuint progH1 = 0;
    GLuint progExtrap = 0;

    struct GmeLocs {
        GLint prev, curr, inputSize, block, gmeStep, originStride, globalMax, sadScale;
    } gme{};
    struct SadLocs {
        GLint prev, curr, gmeTex, inputSize, mvSize, blockGrid, block, searchSpan,
            globalMax, sadScale, noMotion;
    } sad{};
    struct MedLocs {
        GLint l1Raw, mvSize, stride;
    } med{};
    struct H1Locs {
        GLint prev, curr, medTex, inputSize, mvSize, blockGrid, block, globalMax,
            sadScale, noMotionUv;
    } h1{};
    struct ExtrapLocs {
        GLint src, mv, scaleFactor, fovScale, clampMag, srRadius, srMode;
    } extrap{};

    GLuint texGme = 0;  // 1x1 global-motion seed
    GLuint texL1 = 0;   // raw block offsets (input texels) + quality, per eye
    GLuint texMed = 0;  // median-filtered offsets, per eye
    GLuint fbo = 0;

    bool budgetHit = false;
};

Engine g;

// ------------------------------------------------------------- GL helpers ---

#define SWP_EST_MAX_LOG 384

static void DrainErrors() {
    if (!g.gl || !g.gl->glGetError) return;
    for (int i = 0; i < 64; ++i) {
        if (g.gl->glGetError() == GL_NO_ERROR) return;
    }
}

struct SaveState {
    GLint fbo = 0;
    GLint prog = 0;
    GLint active = GL_TEXTURE0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint texBinding[4] = {0, 0, 0, 0};  // GL_TEXTURE0..GL_TEXTURE0+3
    bool ok = false;
};

static bool SaveCallerState(SaveState* s) {
    if (!g.gl || !g.gl->glGetIntegerv || !g.gl->glActiveTexture) return false;
    g.gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s->fbo);
    g.gl->glGetIntegerv(GL_CURRENT_PROGRAM, &s->prog);
    g.gl->glGetIntegerv(GL_VIEWPORT, s->viewport);
    g.gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &s->active);
    const GLint base = s->active;
    for (int i = 0; i < 4; ++i) {
        g.gl->glActiveTexture(GL_TEXTURE0 + i);
        g.gl->glGetIntegerv(GL_TEXTURE_BINDING_2D, &s->texBinding[i]);
    }
    g.gl->glActiveTexture(base);
    s->active = base;
    s->ok = true;
    return true;
}

static void RestoreCallerState(const SaveState& s) {
    if (!g.gl || !s.ok) return;
    g.gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(s.fbo));
    g.gl->glUseProgram(static_cast<GLuint>(s.prog));  // may be 0
    for (int i = 0; i < 4; ++i) {
        g.gl->glActiveTexture(GL_TEXTURE0 + i);
        g.gl->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.texBinding[i]));
    }
    g.gl->glActiveTexture(s.active);
    g.gl->glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
}

// ------------------------------------------------------------ compilation ---

struct ULocDef {
    const char* name;
    GLint* dst;
};

static bool ResolveUniforms(GLuint prog, const ULocDef* defs, int n) {
    if (!g.gl || !g.gl->glGetUniformLocation) return false;
    for (int i = 0; i < n; ++i) {
        *defs[i].dst = g.gl->glGetUniformLocation(prog, defs[i].name);
        if (*defs[i].dst < 0) {
            // Every uniform we declare is used by its pass; a miss means the
            // embedded sources and this file drifted apart — fail safe.
            SWP_EST_LOG("GXRSwarp: setup: uniform %s unresolved (prog %u)",
                        defs[i].name, (unsigned)prog);
            return false;
        }
    }
    return true;
}

// One log line per failed compile/link, bounded log excerpt.
static bool CompileShader(GLuint type, const char* const* parts, int nParts,
                          const char* tag, GLuint* out) {
    (void)tag;  // consumed by the layer's SWP_EST_LOG override; silent here
    if (!g.gl->glCreateShader || !g.gl->glShaderSource || !g.gl->glCompileShader ||
        !g.gl->glGetShaderiv || !g.gl->glDeleteShader) {
        return false;
    }
    const GLuint sh = g.gl->glCreateShader(type);
    if (!sh) return false;
    g.gl->glShaderSource(sh, nParts, parts, nullptr);
    g.gl->glCompileShader(sh);
    GLint status = 0;
    g.gl->glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        char log[SWP_EST_MAX_LOG];
        log[0] = 0;
        if (g.gl->glGetShaderInfoLog) {
            GLsizei len = 0;
            g.gl->glGetShaderInfoLog(sh, SWP_EST_MAX_LOG - 1, &len, log);
            if (len < 0 || static_cast<int>(len) >= SWP_EST_MAX_LOG) {
                log[SWP_EST_MAX_LOG - 1] = 0;
            }
        }
        SWP_EST_LOG("GXRSwarp: setup: %s compile failed: %s", tag, log[0] ? log : "no log");
        g.gl->glDeleteShader(sh);
        return false;
    }
    *out = sh;
    return true;
}

static bool LinkProgram(GLuint vsh, GLuint fsh, const char* tag, GLuint* out) {
    (void)tag;  // consumed by the layer's SWP_EST_LOG override; silent here
    if (!g.gl->glCreateProgram || !g.gl->glAttachShader || !g.gl->glLinkProgram ||
        !g.gl->glGetProgramiv || !g.gl->glDeleteProgram) {
        return false;
    }
    const GLuint prog = g.gl->glCreateProgram();
    if (!prog) return false;
    g.gl->glAttachShader(prog, vsh);
    g.gl->glAttachShader(prog, fsh);
    g.gl->glLinkProgram(prog);
    GLint status = 0;
    g.gl->glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        char log[SWP_EST_MAX_LOG];
        log[0] = 0;
        if (g.gl->glGetProgramInfoLog) {
            GLsizei len = 0;
            g.gl->glGetProgramInfoLog(prog, SWP_EST_MAX_LOG - 1, &len, log);
            if (len < 0 || static_cast<int>(len) >= SWP_EST_MAX_LOG) {
                log[SWP_EST_MAX_LOG - 1] = 0;
            }
        }
        SWP_EST_LOG("GXRSwarp: setup: %s link failed: %s", tag, log[0] ? log : "no log");
        g.gl->glDeleteProgram(prog);
        return false;
    }
    *out = prog;
    return true;
}

static bool BuildPassProgram(const char* tag, const char* const* fragParts, int nParts,
                             GLuint vsh, GLuint* out) {
    GLuint fsh = 0;
    if (!CompileShader(GL_FRAGMENT_SHADER, fragParts, nParts, tag, &fsh)) return false;
    const bool ok = LinkProgram(vsh, fsh, tag, out);
    g.gl->glDeleteShader(fsh);  // flagged for deletion; linked programs retain it
    return ok;
}

// --------------------------------------------------------------- textures ---

static bool AllocInternalTexture(GLuint* tex, int w, int h) {
    if (!g.gl->glGenTextures || !g.gl->glBindTexture || !g.gl->glTexStorage2D ||
        !g.gl->glTexParameteri || !g.gl->glDeleteTextures) {
        return false;
    }
    g.gl->glGenTextures(1, tex);
    if (!*tex) return false;
    g.gl->glBindTexture(GL_TEXTURE_2D, *tex);
    g.gl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
    g.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g.gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (g.gl->glGetError() != GL_NO_ERROR) {
        g.gl->glDeleteTextures(1, tex);
        *tex = 0;
        return false;
    }
    return true;
}

static bool DetectQcomSad() {
    const GLubyte* exts = g.gl->glGetString(GL_EXTENSIONS);
    if (!exts) return false;
    return std::strstr(reinterpret_cast<const char*>(exts), "GL_QCOM_block_match") != nullptr;
}

// GL_VERSION: "OpenGL ES <major>.<minor> <vendor build info>" — plain scan,
// no locale-dependent functions.
static bool ParseEsVersion(const char* v, int* major, int* minor) {
    const char* tag = v ? std::strstr(v, "OpenGL ES ") : nullptr;
    if (!tag) return false;
    tag += 10;
    if (*tag < '0' || *tag > '9') return false;
    char* end = nullptr;
    const long maj = std::strtol(tag, &end, 10);
    if (!end || *end != '.' || maj <= 0 || maj > 9) return false;
    const long mnr = std::strtol(end + 1, nullptr, 10);
    if (mnr < 0 || mnr > 9) return false;
    *major = static_cast<int>(maj);
    *minor = static_cast<int>(mnr);
    return true;
}

// ---------------------------------------------------------------- passes ---

static bool BudgetLeft(const SwpEstConfig& cfg, uint64_t start) {
    if (!cfg.budgetMicros) return true;
    if (!g.gl->nowMicros) return true;
    return (g.gl->nowMicros() - start) < cfg.budgetMicros;
}

static bool BindTarget(GLuint tex, int w, int h, bool setViewport) {
    if (!g.gl->glBindFramebuffer || !g.gl->glFramebufferTexture2D ||
        !g.gl->glCheckFramebufferStatus || !g.gl->glViewport) {
        return false;
    }
    g.gl->glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
    g.gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    const GLenum st = g.gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        SWP_EST_LOG("GXRSwarp: pass: framebuffer incomplete 0x%x (tex %u %dx%d)",
                    (unsigned)st, (unsigned)tex, w, h);
        return false;
    }
    if (setViewport) g.gl->glViewport(0, 0, w, h);
    return true;
}

static void BindSampler(GLint loc, GLint unit, GLuint tex) {
    g.gl->glActiveTexture(GL_TEXTURE0 + unit);
    g.gl->glBindTexture(GL_TEXTURE_2D, tex);
    g.gl->glUniform1i(loc, unit);
}

static bool PassDraw(const char* tag) {
    (void)tag;  // consumed by the layer's SWP_EST_LOG override; silent here
    g.gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    if (g.gl->glGetError() != GL_NO_ERROR) {
        SWP_EST_LOG("GXRSwarp: pass: GL error in %s", tag);
        DrainErrors();
        return false;
    }
    return true;
}

// MV-gen pass 1: coarse global-motion estimate (1x1 target).
static bool PassGme(const SwpEstConfig& cfg, uint32_t prevTex, uint32_t currTex) {
    g.gl->glUseProgram(g.progGme);
    if (!BindTarget(g.texGme, 1, 1, true)) return false;
    BindSampler(g.gme.prev, 0, prevTex);
    BindSampler(g.gme.curr, 1, currTex);
    g.gl->glUniform2i(g.gme.inputSize, cfg.inputW, cfg.inputH);
    g.gl->glUniform1i(g.gme.block, cfg.block);
    g.gl->glUniform2i(g.gme.gmeStep, cfg.gmeStepX, cfg.gmeStepY);
    g.gl->glUniform2i(g.gme.originStride, cfg.gmeOriginStrideX, cfg.gmeOriginStrideY);
    g.gl->glUniform2i(g.gme.globalMax, cfg.globalMaxX, cfg.globalMaxY);
    g.gl->glUniform1f(g.gme.sadScale, cfg.sadScale);
    return PassDraw("gme");
}

// MV-gen pass 2: block-match SAD over the GME-seeded window.
static bool PassSad(const SwpEstConfig& cfg, uint32_t prevTex, uint32_t currTex) {
    g.gl->glUseProgram(g.progSad);
    if (!BindTarget(g.texL1, cfg.mvW, cfg.mvH, true)) return false;
    BindSampler(g.sad.prev, 0, prevTex);
    BindSampler(g.sad.curr, 1, currTex);
    BindSampler(g.sad.gmeTex, 2, g.texGme);
    g.gl->glUniform2i(g.sad.inputSize, cfg.inputW, cfg.inputH);
    g.gl->glUniform2i(g.sad.mvSize, cfg.mvW, cfg.mvH);
    g.gl->glUniform2i(g.sad.blockGrid, cfg.blockGridW, cfg.blockGridH);
    g.gl->glUniform1i(g.sad.block, cfg.block);
    g.gl->glUniform2i(g.sad.searchSpan, cfg.searchSpanX, cfg.searchSpanY);
    g.gl->glUniform2i(g.sad.globalMax, cfg.globalMaxX, cfg.globalMaxY);
    g.gl->glUniform1f(g.sad.sadScale, cfg.sadScale);
    g.gl->glUniform1f(g.sad.noMotion, cfg.noMotion);
    return PassDraw("sad");
}

// MV-gen pass 3: 9-element sorting-network median of the raw field.
static bool PassMedian(const SwpEstConfig& cfg) {
    g.gl->glUseProgram(g.progMed);
    if (!BindTarget(g.texMed, cfg.mvW, cfg.mvH, true)) return false;
    BindSampler(g.med.l1Raw, 0, g.texL1);
    g.gl->glUniform2i(g.med.mvSize, cfg.mvW, cfg.mvH);
    g.gl->glUniform2i(g.med.stride, cfg.medianStride, cfg.medianStride);
    return PassDraw("median");
}

// MV-gen pass 4: H1 refinement around the median + subpel -> final output map.
static bool PassH1(const SwpEstConfig& cfg, uint32_t prevTex, uint32_t currTex, uint32_t mvTex) {
    g.gl->glUseProgram(g.progH1);
    if (!BindTarget(mvTex, cfg.mvW, cfg.mvH, true)) return false;
    BindSampler(g.h1.prev, 0, prevTex);
    BindSampler(g.h1.curr, 1, currTex);
    BindSampler(g.h1.medTex, 2, g.texMed);
    g.gl->glUniform2i(g.h1.inputSize, cfg.inputW, cfg.inputH);
    g.gl->glUniform2i(g.h1.mvSize, cfg.mvW, cfg.mvH);
    g.gl->glUniform2i(g.h1.blockGrid, cfg.blockGridW, cfg.blockGridH);
    g.gl->glUniform1i(g.h1.block, cfg.block);
    g.gl->glUniform2i(g.h1.globalMax, cfg.globalMaxX, cfg.globalMaxY);
    g.gl->glUniform1f(g.h1.sadScale, cfg.sadScale);
    g.gl->glUniform1f(g.h1.noMotionUv, cfg.noMotionUv);
    return PassDraw("h1");
}

// Extrapolate pipeline: the caller's viewport is kept (the projection view's
// render area); only the FBO target changes.
static bool PassExtrap(const SwpEstConfig& cfg, float scaleFactor,
                       uint32_t mvTex, uint32_t srcTex, uint32_t dstTex) {
    g.gl->glUseProgram(g.progExtrap);
    if (!BindTarget(dstTex, 0, 0, false)) return false;
    BindSampler(g.extrap.src, 0, srcTex);
    BindSampler(g.extrap.mv, 1, mvTex);
    g.gl->glUniform1f(g.extrap.scaleFactor, scaleFactor);
    g.gl->glUniform1f(g.extrap.fovScale, cfg.fovScale);
    g.gl->glUniform1f(g.extrap.clampMag, cfg.clampMagUv);
    g.gl->glUniform1f(g.extrap.srRadius, cfg.srRadiusUv);
    g.gl->glUniform1i(g.extrap.srMode, cfg.srMode);
    return PassDraw("extrap");
}

}  // namespace

// ------------------------------------------------------- internal entries ---

void EstiSetTable(const SwpGlTable* table) {
    g.gl = table;
}

bool EstiGlSetup(const SwpEstConfig& cfg, bool* qcomArmed) {
    if (qcomArmed) *qcomArmed = false;
    if (!g.gl) return false;

    if (!g.gl->glGetString || !g.gl->glGetError || !g.gl->glFlush || !g.gl->glUseProgram ||
        !g.gl->glGenFramebuffers || !g.gl->glDeleteFramebuffers || !g.gl->glBindFramebuffer ||
        !g.gl->glFramebufferTexture2D || !g.gl->glCheckFramebufferStatus ||
        !g.gl->glGenTextures || !g.gl->glDeleteTextures || !g.gl->glActiveTexture ||
        !g.gl->glBindTexture || !g.gl->glTexStorage2D || !g.gl->glTexParameteri ||
        !g.gl->glDrawArrays || !g.gl->glViewport ||
        !g.gl->glUniform1i || !g.gl->glUniform2i || !g.gl->glUniform1f ||
        !g.gl->glGetUniformLocation || !g.gl->glGetIntegerv ||
        !g.gl->glCreateShader || !g.gl->glShaderSource || !g.gl->glCompileShader ||
        !g.gl->glGetShaderiv || !g.gl->glGetShaderInfoLog || !g.gl->glDeleteShader ||
        !g.gl->glCreateProgram || !g.gl->glAttachShader || !g.gl->glLinkProgram ||
        !g.gl->glGetProgramiv || !g.gl->glGetProgramInfoLog || !g.gl->glDeleteProgram) {
        SWP_EST_LOG("GXRSwarp: setup: GL table incomplete");
        return false;
    }

    DrainErrors();

    // GLES 3.2 is the shared contract's requirement (RE-SHARED-IMPL.md).
    const GLubyte* ver = g.gl->glGetString(GL_VERSION);
    if (!ver) {
        SWP_EST_LOG("GXRSwarp: setup: no GL_VERSION (context current?)");
        return false;
    }
    int major = 0, minor = 0;
    if (!ParseEsVersion(reinterpret_cast<const char*>(ver), &major, &minor) ||
        major < 3 || (major == 3 && minor < 2)) {
        SWP_EST_LOG("GXRSwarp: setup: GLES 3.2 required, found: %.60s",
                    reinterpret_cast<const char*>(ver));
        return false;
    }

    // Extension detection (brief: blockMatchSADQCOM present => direct path).
    g.haveQcom = !cfg.forceFallbackSad && DetectQcomSad();
    if (qcomArmed) *qcomArmed = g.haveQcom;

    GLuint vsh = 0;
    if (!CompileShader(GL_VERTEX_SHADER, &gxr::swp_shaders::swp_vert_quad, 1,
                       "quad-vert", &vsh)) {
        return false;
    }

    const char* prefix = g.haveQcom ? gxr::swp_shaders::swp_sad_direct_prefix
                                    : gxr::swp_shaders::swp_sad_fallback_prefix;

    GLuint progGme = 0, progSad = 0, progMed = 0, progH1 = 0, progExtrap = 0;
    bool ok = false;
    do {
        // MV-gen pipeline.
        const char* gmeParts[3] = {gxr::swp_shaders::swp_version_line, prefix,
                                   gxr::swp_shaders::swp_gme_core};
        if (!BuildPassProgram("gme", gmeParts, 3, vsh, &progGme)) break;

        const char* sadDirectParts[1] = {gxr::swp_shaders::swp_sad_frag};
        const char* sadFallbackParts[1] = {gxr::swp_shaders::swp_sad_fallback_frag};
        if (!BuildPassProgram("sad", g.haveQcom ? sadDirectParts : sadFallbackParts, 1,
                              vsh, &progSad)) break;

        const char* medParts[1] = {gxr::swp_shaders::swp_median_frag};
        if (!BuildPassProgram("median", medParts, 1, vsh, &progMed)) break;

        const char* h1Parts[3] = {gxr::swp_shaders::swp_version_line, prefix,
                                  gxr::swp_shaders::swp_h1_core};
        if (!BuildPassProgram("h1", h1Parts, 3, vsh, &progH1)) break;

        // Extrapolate pipeline.
        const char* extrapParts[1] = {gxr::swp_shaders::swp_extrap_frag};
        if (!BuildPassProgram("extrap", extrapParts, 1, vsh, &progExtrap)) break;

        ok = true;
    } while (false);

    g.gl->glDeleteShader(vsh);
    if (!ok) {
        if (g.gl->glDeleteProgram) {
            if (progGme) g.gl->glDeleteProgram(progGme);
            if (progSad) g.gl->glDeleteProgram(progSad);
            if (progMed) g.gl->glDeleteProgram(progMed);
            if (progH1) g.gl->glDeleteProgram(progH1);
            if (progExtrap) g.gl->glDeleteProgram(progExtrap);
        }
        DrainErrors();
        return false;
    }
    g.progGme = progGme;
    g.progSad = progSad;
    g.progMed = progMed;
    g.progH1 = progH1;
    g.progExtrap = progExtrap;

    // Internal intermediates (ours: RGBA16F, NEAREST, CLAMP_TO_EDGE).
    if (!AllocInternalTexture(&g.texGme, 1, 1) ||
        !AllocInternalTexture(&g.texL1, cfg.mvW, cfg.mvH) ||
        !AllocInternalTexture(&g.texMed, cfg.mvW, cfg.mvH)) {
        EstiGlShutdown();
        return false;
    }
    g.gl->glGenFramebuffers(1, &g.fbo);
    if (!g.fbo) {
        EstiGlShutdown();
        return false;
    }

    // Uniform-location cache (strict: every declared uniform must resolve).
    ULocDef gmeDefs[] = {
        {"uPrev", &g.gme.prev}, {"uCurr", &g.gme.curr},
        {"uInputSize", &g.gme.inputSize}, {"uBlock", &g.gme.block},
        {"uGmeStep", &g.gme.gmeStep}, {"uGmeOriginStride", &g.gme.originStride},
        {"uGlobalMax", &g.gme.globalMax}, {"uSadScale", &g.gme.sadScale},
    };
    ULocDef sadDefs[] = {
        {"uPrev", &g.sad.prev}, {"uCurr", &g.sad.curr}, {"uGmeTex", &g.sad.gmeTex},
        {"uInputSize", &g.sad.inputSize}, {"uMvSize", &g.sad.mvSize},
        {"uBlockGrid", &g.sad.blockGrid}, {"uBlock", &g.sad.block},
        {"uSearchSpan", &g.sad.searchSpan}, {"uGlobalMax", &g.sad.globalMax},
        {"uSadScale", &g.sad.sadScale}, {"uNoMotion", &g.sad.noMotion},
    };
    ULocDef medDefs[] = {
        {"uL1Raw", &g.med.l1Raw}, {"uMvSize", &g.med.mvSize},
        {"uMedianStride", &g.med.stride},
    };
    ULocDef h1Defs[] = {
        {"uPrev", &g.h1.prev}, {"uCurr", &g.h1.curr}, {"uMedianTex", &g.h1.medTex},
        {"uInputSize", &g.h1.inputSize}, {"uMvSize", &g.h1.mvSize},
        {"uBlockGrid", &g.h1.blockGrid}, {"uBlock", &g.h1.block},
        {"uGlobalMax", &g.h1.globalMax}, {"uSadScale", &g.h1.sadScale},
        {"uNoMotionUv", &g.h1.noMotionUv},
    };
    ULocDef extrapDefs[] = {
        {"uSrc", &g.extrap.src}, {"uMv", &g.extrap.mv},
        {"uScaleFactor", &g.extrap.scaleFactor}, {"uFovScale", &g.extrap.fovScale},
        {"uClampMag", &g.extrap.clampMag}, {"uSrRadius", &g.extrap.srRadius},
        {"uSrMode", &g.extrap.srMode},
    };
    ok = ResolveUniforms(g.progGme, gmeDefs, 8) &&
         ResolveUniforms(g.progSad, sadDefs, 11) &&
         ResolveUniforms(g.progMed, medDefs, 3) &&
         ResolveUniforms(g.progH1, h1Defs, 10) &&
         ResolveUniforms(g.progExtrap, extrapDefs, 7);
    if (!ok) {
        EstiGlShutdown();
        return false;
    }

    if (g.gl->glGetError() != GL_NO_ERROR) {
        DrainErrors();
        EstiGlShutdown();
        SWP_EST_LOG("GXRSwarp: setup: GL error during allocation");
        return false;
    }

    g.setupOk = true;
    DrainErrors();
    return true;
}

void EstiGlShutdown() {
    if (g.gl && g.gl->glDeleteProgram) {
        if (g.progGme) g.gl->glDeleteProgram(g.progGme);
        if (g.progSad) g.gl->glDeleteProgram(g.progSad);
        if (g.progMed) g.gl->glDeleteProgram(g.progMed);
        if (g.progH1) g.gl->glDeleteProgram(g.progH1);
        if (g.progExtrap) g.gl->glDeleteProgram(g.progExtrap);
    }
    if (g.gl && g.gl->glDeleteTextures) {
        GLuint dead[3] = {g.texGme, g.texL1, g.texMed};
        for (int i = 0; i < 3; ++i) {
            if (dead[i]) g.gl->glDeleteTextures(1, &dead[i]);
        }
    }
    if (g.gl && g.fbo && g.gl->glDeleteFramebuffers) {
        g.gl->glDeleteFramebuffers(1, &g.fbo);
    }
    EstiDropHandles();
}

void EstiDropHandles() {
    const SwpGlTable* keep = g.gl;
    g = Engine{};
    g.gl = keep;
}

bool EstiMvMapOneEye(const SwpEstConfig& cfg, uint32_t prevTex, uint32_t currTex,
                     uint32_t mvTex, SwpEstTickOut* out) {
    if (out) {
        out->ok = false;
        out->budgetExceeded = false;
    }
    if (!out) return false;
    if (!g.setupOk || !g.gl) return false;
    if (!prevTex || !currTex || !mvTex) {
        SWP_EST_LOG("GXRSwarp: mv: zero texture id (prev %u curr %u mv %u)",
                    (unsigned)prevTex, (unsigned)currTex, (unsigned)mvTex);
        return false;
    }

    SaveState saved;
    if (!SaveCallerState(&saved)) {
        SWP_EST_LOG("GXRSwarp: pass: state save failed (context race?)");
        return false;
    }
    DrainErrors();

    const uint64_t start = (cfg.budgetMicros && g.gl->nowMicros) ? g.gl->nowMicros() : 0;
    g.budgetHit = false;
    bool ok = false;
    do {
        if (!PassGme(cfg, prevTex, currTex)) break;
        if (!BudgetLeft(cfg, start)) { g.budgetHit = true; break; }
        if (!PassSad(cfg, prevTex, currTex)) break;
        if (!BudgetLeft(cfg, start)) { g.budgetHit = true; break; }
        if (!PassMedian(cfg)) break;
        if (!BudgetLeft(cfg, start)) { g.budgetHit = true; break; }
        if (!PassH1(cfg, prevTex, currTex, mvTex)) break;
        ok = true;
    } while (false);

    DrainErrors();
    g.gl->glFlush();
    RestoreCallerState(saved);
    out->ok = ok;
    out->budgetExceeded = g.budgetHit;
    return ok;
}

bool EstiExtrapolateOneEye(const SwpEstConfig& cfg, float scaleFactor, uint32_t mvTex,
                           uint32_t srcTex, uint32_t dstTex, SwpEstTickOut* out) {
    if (out) {
        out->ok = false;
        out->budgetExceeded = false;
    }
    if (!out) return false;
    if (!g.setupOk || !g.gl) return false;
    if (!mvTex || !srcTex || !dstTex) {
        SWP_EST_LOG("GXRSwarp: extrap: zero texture id (mv %u src %u dst %u)",
                    (unsigned)mvTex, (unsigned)srcTex, (unsigned)dstTex);
        return false;
    }

    // Conservative clamp: extrapolation runs at (or beyond) the newest history
    // frame; sub-1 factors clamp to a no-op (dst = src copy, safe). An
    // extrapolate-overrun likewise surfaces the budget bit, same as MV-gen.
    if (!(scaleFactor >= 1.0f)) scaleFactor = 1.0f;

    SaveState saved;
    if (!SaveCallerState(&saved)) {
        SWP_EST_LOG("GXRSwarp: pass: state save failed (context race?)");
        return false;
    }
    DrainErrors();

    const uint64_t start = (cfg.budgetMicros && g.gl->nowMicros) ? g.gl->nowMicros() : 0;
    const bool ok = PassExtrap(cfg, scaleFactor, mvTex, srcTex, dstTex);
    if (ok && !BudgetLeft(cfg, start)) out->budgetExceeded = true;

    DrainErrors();
    g.gl->glFlush();
    RestoreCallerState(saved);
    out->ok = ok;
    return ok;
}

}  // namespace swp_internal
}  // namespace gxr
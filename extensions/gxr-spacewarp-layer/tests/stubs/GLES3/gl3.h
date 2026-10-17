#pragma once
// Host-only GLES3 substitute: covers the layer's direct calls AND the sibling
// estimator's GL function-pointer typedefs (SwpGlTable is defined in terms of
// PFNGL*PROC types). Function bodies live in the test TU so tests can assert.
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef int GLboolean;
typedef float GLfloat;
typedef unsigned char GLubyte;
typedef char GLchar;
#ifndef GL_APIENTRYP
#define GL_APIENTRYP *
#endif
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_RGBA16F = 0x881A;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_HALF_FLOAT = 0x140B;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_VENDOR = 0x1F00;

// GL function-pointer typedefs as the real GLES3 header defines them.
typedef GLuint (GL_APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void (GL_APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count,
                                                  const GLchar* const* string,
                                                  const GLint* length);
typedef void (GL_APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (GL_APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void (GL_APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize,
                                                      GLsizei* length, GLchar* infoLog);
typedef void (GL_APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
typedef GLuint (GL_APIENTRYP PFNGLCREATEPROGRAMPROC)();
typedef void (GL_APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (GL_APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (GL_APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void (GL_APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize,
                                                       GLsizei* length, GLchar* infoLog);
typedef void (GL_APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint (GL_APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void (GL_APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint x);
typedef void (GL_APIENTRYP PFNGLUNIFORM2IPROC)(GLint location, GLint x, GLint y);
typedef void (GL_APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat x);
typedef void (GL_APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (GL_APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (GL_APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (GL_APIENTRYP PFNGLFLUSHPROC)();
typedef GLenum (GL_APIENTRYP PFNGLGETERRORPROC)();
typedef void (GL_APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void (GL_APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void (GL_APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment,
                                                          GLenum textarget, GLuint texture,
                                                          GLint level);
typedef GLenum (GL_APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (GL_APIENTRYP PFNGLGENTEXTURESPROC)(GLsizei n, GLuint* textures);
typedef void (GL_APIENTRYP PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint* textures);
typedef void (GL_APIENTRYP PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (GL_APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (GL_APIENTRYP PFNGLTEXSTORAGE2DPROC)(GLenum target, GLsizei levels,
                                                  GLenum internalformat, GLsizei width,
                                                  GLsizei height);
typedef void (GL_APIENTRYP PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef const GLubyte* (GL_APIENTRYP PFNGLGETSTRINGPROC)(GLenum name);
typedef void (GL_APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint* params);

extern "C" {
GLuint glCreateShader(GLenum type);
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string,
                    const GLint* length);
void glCompileShader(GLuint shader);
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
void glDeleteShader(GLuint shader);
GLuint glCreateProgram();
void glAttachShader(GLuint program, GLuint shader);
void glLinkProgram(GLuint program);
void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
void glDeleteProgram(GLuint program);
GLint glGetUniformLocation(GLuint program, const GLchar* name);
void glUniform1i(GLint location, GLint x);
void glUniform2i(GLint location, GLint x, GLint y);
void glUniform1f(GLint location, GLfloat x);
void glUseProgram(GLuint program);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glFlush();
GLenum glGetError();
void glGenFramebuffers(GLsizei n, GLuint* framebuffers);
void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers);
void glBindFramebuffer(GLenum target, GLuint framebuffer);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                            GLint level);
GLenum glCheckFramebufferStatus(GLenum target);
void glGenTextures(GLsizei n, GLuint* textures);
void glDeleteTextures(GLsizei n, const GLuint* textures);
void glBindTexture(GLenum target, GLuint texture);
void glActiveTexture(GLenum texture);
void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width,
                    GLsizei height);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
const GLubyte* glGetString(GLenum name);
void glGetIntegerv(GLenum pname, GLint* params);
}  // extern "C"
#pragma once
// Host-only EGL substitute: only the tiny surface the layer calls into.
typedef void* EGLDisplay;
typedef void* EGLContext;      // opaque pointer on device too
typedef void* EGLImageKHR;     // khronos pointer-sized handle
typedef void* EGLClientBuffer;
typedef void* EGLConfig;       // opaque (openxr_platform.h's GLES binding struct names it)
typedef void* EGLSurface;
typedef int EGLint;
typedef unsigned int EGLenum;
typedef unsigned int EGLBoolean;
constexpr EGLContext EGL_NO_CONTEXT = nullptr;
constexpr EGLDisplay EGL_NO_DISPLAY = nullptr;
constexpr EGLBoolean EGL_TRUE = 1;
constexpr EGLBoolean EGL_FALSE = 0;
constexpr EGLenum EGL_SURFACE_TYPE = 0x3033;          // shape parity with the NDK header
constexpr EGLenum EGL_GL_TEXTURE_2D_KHR = 0x30B0;     // KHR image target the scene lib uses
constexpr EGLenum EGL_NATIVE_BUFFER_ANDROID = 0x3140; // AImageReader buffer target
extern "C" EGLContext eglGetCurrentContext();
#pragma once
// Host-only android/log substitute (mirrors resolution-trace stubs).
constexpr int ANDROID_LOG_INFO = 4;
extern "C" int __android_log_print(int priority, const char* tag, const char* format, ...);
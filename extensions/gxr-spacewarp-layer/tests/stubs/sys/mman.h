#pragma once
// Host-only memory-protection substitute (bodies in the test TU): the hooks
// test records mprotect calls and can force failures.
#include <stddef.h>
constexpr int PROT_READ = 0x00000001;
constexpr int PROT_WRITE = 0x00000002;
extern "C" int mprotect(void* addr, size_t len, int prot);
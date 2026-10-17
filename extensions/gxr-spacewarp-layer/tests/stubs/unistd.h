#pragma once
// Host-only unistd substitute: swp_hooks reads the page size.
constexpr int _SC_PAGESIZE = 30;
extern "C" long sysconf(int name);
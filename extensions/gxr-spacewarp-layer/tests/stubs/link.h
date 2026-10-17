#pragma once
#include <stddef.h>

// Host-only ELF/link substitute: the hooks test controls dl_iterate_phdr with a
// fake module table instead of the real process loader.
struct dl_phdr_info {
    const char* dlpi_name;
    unsigned long dlpi_addr;  // host-side load base (device: link_map address)
    const void* dlpi_phdr;
    unsigned short dlpi_phnum;
};

using DlIteratePhdrCallback = int (*)(struct dl_phdr_info*, size_t, void*);
extern "C" int dl_iterate_phdr(DlIteratePhdrCallback callback, void* data);
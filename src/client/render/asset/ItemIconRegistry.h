#pragma once
#include <span>

namespace ItemIconRegistry {
    struct Entry {
        const char* id;
        const char* begin;
        const char* end;
    };

    std::span<const Entry> entries();
}

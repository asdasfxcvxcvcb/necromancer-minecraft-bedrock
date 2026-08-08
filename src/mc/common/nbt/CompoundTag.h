#pragma once
#include "Tag.h"
#include <mc/Addresses.h>
#include <string>
#include <vector>

namespace SDK {
    enum class TagType : uint8_t {
        End,
        Byte,
        Short,
        Int,
        Int64,
        Float,
        Double,
        ByteArray,
        String,
        List,
        Compound,
        IntArray,
    };

    class ByteTag : public Tag {
    public:
        uint8_t val;
    };

    class ShortTag : public Tag {
    public:
        short val;
    };

    class ListTag : public Tag {
    public:
        std::vector<Tag*> val;
        uint8_t elementType;
    };

    class CompoundTag : public Tag {
    public:
        Tag* get(const std::string& key) {
            if (!Signatures::CompoundTag_get.result) return nullptr;
            struct {
                const char* data;
                size_t len;
            } keyData { key.data(), key.size() };
            return reinterpret_cast<Tag*(__fastcall*)(CompoundTag*, decltype(keyData)*)>(
                Signatures::CompoundTag_get.result)(this, &keyData);
        }
    };
}

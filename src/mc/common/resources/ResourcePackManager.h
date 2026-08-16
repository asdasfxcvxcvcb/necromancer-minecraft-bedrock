#pragma once

#include <string>

#include "mc/deps/core/resource/ResourceLocation.h"
#include "util/memory.h"
#include <mc/Addresses.h>

namespace SDK {
    class ResourcePackManager {
    public:
        bool load(ResourceLocation const& resourceLocation, std::string& resourceStream) const {
            return memory::callVirtual<bool, ResourceLocation const&, std::string&>(
                const_cast<ResourcePackManager*>(this), Signatures::VtableIndex::ResourcePackManager::load,
                resourceLocation, resourceStream);
        }
    };
}

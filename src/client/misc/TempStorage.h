#pragma once

#include <filesystem>

namespace NecromancerTemp {
    std::filesystem::path resolvePath(std::filesystem::path const& relative);
    void cleanup();
}

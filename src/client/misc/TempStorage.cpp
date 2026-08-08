#include "pch.h"
#include "TempStorage.h"

std::filesystem::path NecromancerTemp::resolvePath(std::filesystem::path const& relative) {
    std::error_code ec;
    auto temp = std::filesystem::temp_directory_path(ec);
    if (ec || temp.empty()) {
        wchar_t buffer[MAX_PATH] {};
        DWORD length = GetTempPathW(MAX_PATH, buffer);
        if (length == 0 || length >= MAX_PATH) {
            return {};
        }

        temp = std::filesystem::path(buffer);
    }

    auto root = temp / "Necromancer";
    if (relative.empty()) {
        return root;
    }

    return root / relative;
}

void NecromancerTemp::cleanup() {
    std::error_code ec;
    auto temp = std::filesystem::temp_directory_path(ec);
    if (ec || temp.empty()) {
        wchar_t buffer[MAX_PATH] {};
        DWORD length = GetTempPathW(MAX_PATH, buffer);
        if (length == 0 || length >= MAX_PATH) {
            return;
        }

        temp = std::filesystem::path(buffer);
    }

    auto root = temp / "Necromancer";
    if (root.empty() || root.filename().wstring() != L"Necromancer") {
        return;
    }

    std::filesystem::remove_all(root, ec);
}

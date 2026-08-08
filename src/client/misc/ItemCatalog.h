#pragma once
#include <string>
#include <vector>

class ItemCatalog {
public:
    struct Entry {
        std::string id;
        std::wstring displayName;
        std::wstring searchKey;
        void* block = nullptr;
    };

    static ItemCatalog& get();

    std::vector<Entry> const& entries();
    void rebuild();
    void invalidate() { built = false; }

    [[nodiscard]] std::wstring displayNameFor(std::string const& id);
    [[nodiscard]] void* blockFor(std::string const& id);

private:
    ItemCatalog() = default;

    std::vector<Entry> list;
    bool built = false;
};

#include "pch.h"
#include "ItemCatalog.h"
#include "mc/common/world/Item.h"
#include "mc/common/locale/I18n.h"
#include <algorithm>
#include <map>
#include <unordered_set>

namespace {
    bool safeRead(void const* addr, void* out, size_t size) {
        if (!addr) return false;
        MEMORY_BASIC_INFORMATION mbi {};
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        auto regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (reinterpret_cast<uintptr_t>(addr) + size > regionEnd) return false;
        memcpy(out, addr, size);
        return true;
    }

    template<typename T>
    std::optional<T> safeReadAs(void const* addr) {
        T value {};
        if (!safeRead(addr, &value, sizeof(T))) return std::nullopt;
        return value;
    }

    constexpr size_t blockLegacyNamespacedIdOffset = 0xE0;
    constexpr size_t blockLegacyBackPtrOffset = 0x68;
    constexpr size_t maxProbeOffset = 0x260;
    constexpr size_t maxStateProbeOffset = 0x400;

    std::optional<void*> resolveLegacyBlock(SDK::Item* item, size_t offset, bool extraDeref) {
        auto candidate = safeReadAs<void*>(reinterpret_cast<char const*>(item) + offset);
        if (!candidate || !*candidate) return std::nullopt;
        void* ptr = *candidate;
        if (extraDeref) {
            auto inner = safeReadAs<void*>(ptr);
            if (!inner || !*inner) return std::nullopt;
            ptr = *inner;
        }
        return ptr;
    }

    std::optional<int64_t> legacyBlockHash(void* legacy) {
        auto hash = safeReadAs<int64_t>(reinterpret_cast<char const*>(legacy) + blockLegacyNamespacedIdOffset);
        if (!hash || *hash == 0) return std::nullopt;
        return *hash;
    }

    std::optional<void*> resolveDefaultState(void* legacy, size_t offset) {
        auto candidate = safeReadAs<void*>(reinterpret_cast<char const*>(legacy) + offset);
        if (!candidate || !*candidate || *candidate == legacy) return std::nullopt;
        auto back = safeReadAs<void*>(reinterpret_cast<char const*>(*candidate) + blockLegacyBackPtrOffset);
        if (!back || *back != legacy) return std::nullopt;
        return *candidate;
    }
}

ItemCatalog& ItemCatalog::get() {
    static auto* instance = new ItemCatalog;
    return *instance;
}

std::vector<ItemCatalog::Entry> const& ItemCatalog::entries() {
    if (!built) rebuild();
    return list;
}

void ItemCatalog::rebuild() {
    list.clear();
    built = false;

    auto ci = SDK::ClientInstance::get();
    auto level = ci && ci->minecraft ? ci->minecraft->getLevel() : nullptr;
    if (!level) return;

    void* registry = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(level) + 0x198);
    if (!registry) return;

    auto itemCounters = reinterpret_cast<void***>(reinterpret_cast<uintptr_t>(registry) + 0x38);
    if (!itemCounters[0] || !itemCounters[1]) return;

    built = true;

    auto i18n = SDK::I18n::get();

    std::map<std::pair<size_t, bool>, int> votes;
    std::optional<std::pair<size_t, bool>> layout;
    constexpr int votesNeeded = 12;

    for (auto current = itemCounters[0]; current < itemCounters[1] && !layout; ++current) {
        auto counter = *current;
        auto item = counter ? *reinterpret_cast<SDK::Item**>(counter) : nullptr;
        if (!item) continue;

        int64_t itemHash = item->namespacedId.hash;
        if (itemHash == 0) continue;

        for (size_t offset = 0x8; offset < maxProbeOffset && !layout; offset += 8) {
            for (bool extraDeref : { false, true }) {
                auto legacy = resolveLegacyBlock(item, offset, extraDeref);
                if (!legacy) continue;
                auto hash = legacyBlockHash(*legacy);
                if (!hash || *hash != itemHash) continue;

                auto& count = votes[{ offset, extraDeref }];
                if (++count >= votesNeeded) layout = std::pair { offset, extraDeref };
                break;
            }
        }
    }

    if (!layout && !votes.empty()) {
        auto best = std::ranges::max_element(votes, {}, [](auto const& kv) { return kv.second; });
        if (best->second >= 3) layout = best->first;
    }

    std::map<size_t, int> stateVotes;
    std::optional<size_t> stateLayout;
    if (layout) {
        constexpr int stateVotesNeeded = 12;
        for (auto current = itemCounters[0]; current < itemCounters[1] && !stateLayout; ++current) {
            auto counter = *current;
            auto item = counter ? *reinterpret_cast<SDK::Item**>(counter) : nullptr;
            if (!item) continue;

            auto legacy = resolveLegacyBlock(item, layout->first, layout->second);
            if (!legacy || !legacyBlockHash(*legacy)) continue;

            for (size_t offset = 0x8; offset < maxStateProbeOffset; offset += 8) {
                if (!resolveDefaultState(*legacy, offset)) continue;
                if (++stateVotes[offset] >= stateVotesNeeded) {
                    stateLayout = offset;
                    break;
                }
            }
        }
        if (!stateLayout && !stateVotes.empty()) {
            auto best = std::ranges::max_element(stateVotes, {}, [](auto const& kv) { return kv.second; });
            if (best->second >= 3) stateLayout = best->first;
        }
    }

    std::unordered_set<std::string> seen;

    for (auto current = itemCounters[0]; current < itemCounters[1]; ++current) {
        auto counter = *current;
        auto item = counter ? *reinterpret_cast<SDK::Item**>(counter) : nullptr;
        if (!item) continue;

        std::string id = item->namespacedId.getString();
        if (id.empty() || id == "minecraft:air") continue;
        if (!seen.insert(id).second) continue;

        std::string translate = item->translateName;
        void* block = nullptr;

        if (layout && stateLayout) {
            if (auto legacy = resolveLegacyBlock(item, layout->first, layout->second)) {
                if (legacyBlockHash(*legacy)) {
                    if (auto state = resolveDefaultState(*legacy, *stateLayout)) block = *state;
                }
            }
        }

        std::wstring disp;
        if (i18n && !translate.empty()) {
            for (auto const& locKey : { translate + ".name", translate }) {
                auto loc = i18n->get(locKey);
                if (!loc.empty() && loc != locKey && loc != "%" + locKey) {
                    disp = util::StrToWStr(loc);
                    break;
                }
            }
        }
        if (disp.empty()) {
            auto sep = id.find(':');
            disp = util::StrToWStr(sep == std::string::npos ? id : id.substr(sep + 1));
        }

        std::wstring searchKey = disp + L" " + util::StrToWStr(id);
        std::ranges::transform(searchKey, searchKey.begin(), towlower);

        list.push_back({ std::move(id), std::move(disp), std::move(searchKey), block });
    }

    std::ranges::sort(list, {}, &Entry::displayName);

    Logger::Info("ItemCatalog: {} items", list.size());
}

std::wstring ItemCatalog::displayNameFor(std::string const& id) {
    for (auto const& entry : entries()) {
        if (entry.id == id) return entry.displayName;
    }
    auto sep = id.find(':');
    return util::StrToWStr(sep == std::string::npos ? id : id.substr(sep + 1));
}

void* ItemCatalog::blockFor(std::string const& id) {
    for (auto const& entry : entries()) {
        if (entry.id == id) return entry.block;
    }
    return nullptr;
}

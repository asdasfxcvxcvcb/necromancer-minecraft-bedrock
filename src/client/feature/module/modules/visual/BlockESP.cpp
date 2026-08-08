#include "pch.h"
#include "BlockESP.h"
#include "AntiObs.h"
#include "client/Necromancer.h"
#include "client/misc/RenderFrameState.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/screen/ScreenManager.h"
#include "client/screen/screens/ClickGUI.h"
#include <util/DrawUtil3D.h>
#include <util/DrawContext.h>
#include <util/WorldToScreen.h>
#include <mc/common/locale/I18n.h>
#include <mc/common/world/ItemStack.h>
#include <mc/common/world/Item.h>
#include <mc/common/world/level/BlockSource.h>
#include <mc/common/world/level/block/Block.h>
#include <mc/common/world/level/block/BlockLegacy.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/client/player/LocalPlayer.h>
#include <mc/common/client/renderer/MaterialPtr.h>
#include <mc/common/client/gui/controls/VisualTree.h>
#include <mc/common/client/gui/controls/UIControl.h>
#include <algorithm>
#include <unordered_set>
#include <map>

BlockESP::BlockESP()
    : Module("BlockESP", LocalizeString::get("client.module.blockEsp.name"),
             LocalizeString::get("client.module.blockEsp.desc"), GAME) {
    auto btn = addSetting("blocks", LocalizeString::get("client.module.blockEsp.blocks.name"),
                          LocalizeString::get("client.module.blockEsp.blocks.desc"), blocksButton);
    btn->callback = [this](Setting&) {
        Necromancer::getScreenManager().get<ClickGUI>().openBlockPicker(this);
    };

    addSliderSetting("scanDelay", LocalizeString::get("client.module.blockEsp.scanDelay.name"),
                     LocalizeString::get("client.module.blockEsp.scanDelay.desc"), scanDelay, FloatValue(0.5f),
                     FloatValue(5.f), FloatValue(0.5f));
    addSliderSetting("maxBlocks", LocalizeString::get("client.module.blockEsp.maxBlocks.name"),
                     LocalizeString::get("client.module.blockEsp.maxBlocks.desc"), maxBlocks, FloatValue(16.f),
                     FloatValue(1024.f), FloatValue(16.f));

    auto data = addSetting("blockList", L"blockList", L"", blockData);
    data->visible = false;
    data->callback = [this](Setting&) { parseBlockData(); };

    Eventing::get().listen<RenderLevelEvent, &BlockESP::onRenderLevel>(this);
    Eventing::get().listen<RenderLayerEvent, &BlockESP::onRenderLayer>(this, 0, true);
    Eventing::get().listen<RenderOverlayEvent, &BlockESP::onRenderOverlay>(this);
}

void BlockESP::invalidateScan() {
    scanDirty = true;
    found.clear();
    pending.clear();
    blockLookup.clear();
    scanActive = false;
}

bool BlockESP::hasBlock(std::string const& id) const {
    for (auto& e : entries) {
        if (e->id == id) return true;
    }
    return false;
}

void BlockESP::addBlock(CatalogEntry const& cat) {
    if (cat.id.empty() || hasBlock(cat.id)) return;
    auto e = std::make_shared<BlockEntry>();
    e->id = cat.id;
    e->displayName = cat.displayName;
    bindEntrySettings(*e);
    entries.push_back(e);
    invalidateScan();
    persist();
}

void BlockESP::removeBlock(size_t index) {
    if (index >= entries.size()) return;
    entries.erase(entries.begin() + index);
    invalidateScan();
    persist();
}

void BlockESP::bindEntrySettings(BlockEntry& entry) {
    auto cs = std::make_shared<Setting>("blockEspColor", LocalizeString::get("client.module.blockEsp.entryColor.name"),
                                        LocalizeString::get("client.module.blockEsp.entryColor.desc"));
    cs->value = &entry.color;
    cs->defaultValue = ColorValue(1.f, 0.25f, 0.25f, 1.f);
    cs->callback = [this](Setting&) { persist(); };
    entry.colorSetting = cs;

    auto ts = std::make_shared<Setting>("blockEspThickness",
                                        LocalizeString::get("client.module.blockEsp.entryThickness.name"),
                                        LocalizeString::get("client.module.blockEsp.entryThickness.desc"));
    ts->value = &entry.thickness;
    ts->defaultValue = FloatValue(1.f);
    ts->min = FloatValue(0.5f);
    ts->max = FloatValue(3.f);
    ts->interval = FloatValue(0.1f);
    ts->callback = [this](Setting&) { persist(); };
    entry.thicknessSetting = ts;
}

void BlockESP::persist() {
    auto arr = nlohmann::json::array();
    for (auto& e : entries) {
        nlohmann::json j = nlohmann::json::object();
        j["id"] = e->id;
        j["name"] = util::WStrToStr(e->displayName);
        nlohmann::json col = nlohmann::json::object();
        std::get<ColorValue>(e->color).store(col);
        j["color"] = col;
        j["thickness"] = std::get<FloatValue>(e->thickness).value;
        arr.push_back(j);
    }
    std::get<TextValue>(blockData).str = util::StrToWStr(arr.dump());
}

void BlockESP::parseBlockData() {
    entries.clear();
    auto raw = util::WStrToStr(std::get<TextValue>(blockData).str);
    auto js = nlohmann::json::parse(raw, nullptr, false);
    if (js.is_array()) {
        for (auto& j : js) {
            if (!j.is_object() || !j.contains("id") || !j["id"].is_string()) continue;
            auto e = std::make_shared<BlockEntry>();
            e->id = j["id"].get<std::string>();
            if (e->id.empty() || hasBlock(e->id)) continue;
            e->displayName = j.contains("name") && j["name"].is_string()
                                 ? util::StrToWStr(j["name"].get<std::string>())
                                 : util::StrToWStr(e->id);
            if (j.contains("color") && j["color"].is_object() && j["color"].contains("color1")) {
                e->color = ColorValue(j["color"]);
            }
            if (j.contains("thickness") && j["thickness"].is_number()) {
                e->thickness = FloatValue(std::clamp(j["thickness"].get<float>(), 0.5f, 3.f));
            }
            bindEntrySettings(*e);
            entries.push_back(e);
        }
    }
    invalidateScan();
}

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

    std::optional<std::string> safeReadStdString(void const* addr) {
        struct Raw {
            union {
                char inlineBuf[16];
                char* heapPtr;
            } data;
            size_t size;
            size_t capacity;
        };
        auto raw = safeReadAs<Raw>(addr);
        if (!raw) return std::nullopt;
        if (raw->capacity < 15 || raw->size > raw->capacity || raw->size > 512) return std::nullopt;

        if (raw->capacity == 15) {
            return std::string(raw->data.inlineBuf, raw->size);
        }
        std::vector<char> buf(raw->size + 1, '\0');
        if (!safeRead(raw->data.heapPtr, buf.data(), raw->size)) return std::nullopt;
        return std::string(buf.data(), raw->size);
    }

    constexpr size_t blockLegacyNamespacedIdOffset = 0xE0;
    constexpr size_t blockLegacyTranslateNameOffset = 0x8;

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

    constexpr size_t blockLegacyBackPtrOffset = 0x68;
    constexpr size_t maxStateProbeOffset = 0x400;

    std::optional<void*> resolveDefaultState(void* legacy, size_t offset) {
        auto candidate = safeReadAs<void*>(reinterpret_cast<char const*>(legacy) + offset);
        if (!candidate || !*candidate || *candidate == legacy) return std::nullopt;
        auto back = safeReadAs<void*>(reinterpret_cast<char const*>(*candidate) + blockLegacyBackPtrOffset);
        if (!back || *back != legacy) return std::nullopt;
        return *candidate;
    }
}

void BlockESP::rebuildCatalog() {
    catalog.clear();
    auto ci = SDK::ClientInstance::get();
    auto level = ci && ci->minecraft ? ci->minecraft->getLevel() : nullptr;
    if (!level) return;

    void* registry = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(level) + 0x198);
    if (!registry) return;

    auto itemCounters = reinterpret_cast<void***>(reinterpret_cast<uintptr_t>(registry) + 0x38);
    if (!itemCounters[0] || !itemCounters[1]) return;

    auto i18n = SDK::I18n::get();

    constexpr size_t maxProbeOffset = 0x260;
    constexpr int votesNeeded = 12;

    std::map<std::pair<size_t, bool>, int> votes;
    std::optional<std::pair<size_t, bool>> layout;

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

    std::unordered_set<std::string> seen;
    size_t rejected = 0;

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

    for (auto current = itemCounters[0]; current < itemCounters[1]; ++current) {
        auto counter = *current;
        auto item = counter ? *reinterpret_cast<SDK::Item**>(counter) : nullptr;
        if (!item) continue;

        std::string id;
        std::string translate;
        void* block = nullptr;

        if (layout) {
            auto legacy = resolveLegacyBlock(item, layout->first, layout->second);
            if (!legacy || !legacyBlockHash(*legacy)) {
                rejected++;
                continue;
            }
            auto blockId = safeReadStdString(reinterpret_cast<char const*>(*legacy) +
                                             blockLegacyNamespacedIdOffset + sizeof(int64_t));
            if (!blockId || blockId->empty()) {
                rejected++;
                continue;
            }
            id = std::move(*blockId);
            if (auto blockTranslate =
                    safeReadStdString(reinterpret_cast<char const*>(*legacy) + blockLegacyTranslateNameOffset)) {
                translate = std::move(*blockTranslate);
            }
            if (stateLayout) {
                if (auto state = resolveDefaultState(*legacy, *stateLayout)) block = *state;
            }
        } else {
            id = item->namespacedId.getString();
        }

        if (translate.empty()) translate = item->translateName;
        if (id.empty() || id == "minecraft:air" || !seen.insert(id).second) continue;

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

        catalog.push_back({ block, std::move(id), std::move(disp), std::move(searchKey) });
    }


    std::ranges::sort(catalog, {}, &CatalogEntry::displayName);
}

std::vector<BlockESP::CatalogEntry> const& BlockESP::getCatalog() {
    if (catalog.empty()) rebuildCatalog();
    return catalog;
}

BlockESP::IconDraw BlockESP::findIconSource(std::string const& id) {
    for (auto& cat : getCatalog()) {
        if (cat.id == id) return { {}, cat.block };
    }
    return {};
}

void BlockESP::onRenderLevel(RenderLevelEvent& event) {
    if (entries.empty()) {
        found.clear();
        pending.clear();
        scanActive = false;
        return;
    }

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;
    auto lp = ci->getLocalPlayer();
    auto region = ci->getRegion();
    if (!lp || !region) return;

    Vec3 p = lp->getPos();

    auto now = std::chrono::steady_clock::now();
    auto delayMs = std::chrono::milliseconds(static_cast<int>(std::get<FloatValue>(scanDelay).value * 1000.f));

    if (scanDirty) {
        scanDirty = false;
        blockLookup.clear();
        found.clear();
        pending.clear();
        scanActive = false;
        lastSweep = {};
    }

    constexpr int scanRadius = 96;
    constexpr int scanWidth = scanRadius * 2 + 1;
    constexpr int totalColumns = scanWidth * scanWidth;
    // Time budget instead of a fixed column count: a fixed budget costs the same
    // work no matter how long the frame already took, which drags frame time down
    // further exactly when the game is already struggling.
    constexpr auto scanBudget = std::chrono::microseconds(1500);
    constexpr int budgetCheckInterval = 16;
    constexpr size_t pendingCap = 20000;
    constexpr size_t blockLookupCap = 8192;
    constexpr int minY = -64;
    constexpr int maxY = 319;

    if (!scanActive && now - lastSweep >= delayMs) {
        scanActive = true;
        scanCursor = 0;
        scanCenter = BlockPos { static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)),
                                static_cast<int>(std::floor(p.z)) };
        pending.clear();
    }

    if (scanActive) {
        auto scanStart = std::chrono::steady_clock::now();
        int sinceCheck = 0;
        while (scanCursor < totalColumns && pending.size() < pendingCap) {
            if (++sinceCheck >= budgetCheckInterval) {
                sinceCheck = 0;
                if (std::chrono::steady_clock::now() - scanStart >= scanBudget) break;
            }

            int dx = scanCursor % scanWidth - scanRadius;
            int dz = scanCursor / scanWidth - scanRadius;
            scanCursor++;
            int x = scanCenter.x + dx;
            int z = scanCenter.z + dz;

            for (int y = minY; y <= maxY; y++) {
                auto block = region->getBlock(BlockPos { x, y, z });
                if (!block) continue;

                int idx;
                auto it = blockLookup.find(block);
                if (it == blockLookup.end()) {
                    idx = -1;
                    auto legacy = block->legacyBlock;
                    if (legacy) {
                        std::string id = legacy->namespacedId.getString();
                        for (size_t i = 0; i < entries.size(); i++) {
                            if (entries[i]->id == id) {
                                idx = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    if (blockLookup.size() >= blockLookupCap) blockLookup.clear();
                    blockLookup.emplace(block, idx);
                } else {
                    idx = it->second;
                }

                if (idx >= 0 && pending.size() < pendingCap) {
                    float fdx = static_cast<float>(x) + 0.5f - static_cast<float>(scanCenter.x);
                    float fdy = static_cast<float>(y) + 0.5f - static_cast<float>(scanCenter.y);
                    float fdz = static_cast<float>(z) + 0.5f - static_cast<float>(scanCenter.z);
                    pending.push_back({ BlockPos { x, y, z }, idx, fdx * fdx + fdy * fdy + fdz * fdz });
                }
            }
        }

        if (scanCursor >= totalColumns || pending.size() >= pendingCap) {
            scanActive = false;
            lastSweep = now;
            std::ranges::sort(pending, {}, &FoundBlock::distSq);
            found = std::move(pending);
            pending.clear();
        }
    }

    if (found.empty()) return;

    if (AntiObs::isActive()) return;

    MCDrawUtil3D dc { ci->levelRenderer, SDK::ScreenContext::instance3d, SDK::MaterialPtr::getUIColor() };

    size_t cap = std::min(found.size(), static_cast<size_t>(std::get<FloatValue>(maxBlocks).value));
    for (size_t i = 0; i < cap; i++) {
        auto& f = found[i];
        if (f.entryIdx < 0 || f.entryIdx >= static_cast<int>(entries.size())) continue;
        auto& e = *entries[f.entryIdx];

        auto col = d2d::Color(std::get<ColorValue>(e.color).getMainColor());
        float raw = std::get<FloatValue>(e.thickness).value;
        float th = raw / 10.f;

        Vec3 l { static_cast<float>(f.pos.x), static_cast<float>(f.pos.y), static_cast<float>(f.pos.z) };
        Vec3 h { l.x + 1.f, l.y + 1.f, l.z + 1.f };

        if (raw <= 0.5f) {
            dc.drawBox(AABB { l, h }, col);
        } else {
            std::pair<Vec3, Vec3> edges[12] = {
                { { l.x, l.y, l.z }, { h.x, l.y, l.z } },
                { { h.x, l.y, l.z }, { h.x, l.y, h.z } },
                { { h.x, l.y, h.z }, { l.x, l.y, h.z } },
                { { l.x, l.y, h.z }, { l.x, l.y, l.z } },
                { { l.x, h.y, l.z }, { h.x, h.y, l.z } },
                { { h.x, h.y, l.z }, { h.x, h.y, h.z } },
                { { h.x, h.y, h.z }, { l.x, h.y, h.z } },
                { { l.x, h.y, h.z }, { l.x, h.y, l.z } },
                { { l.x, l.y, l.z }, { l.x, h.y, l.z } },
                { { h.x, l.y, l.z }, { h.x, h.y, l.z } },
                { { h.x, l.y, h.z }, { h.x, h.y, h.z } },
                { { l.x, l.y, h.z }, { l.x, h.y, h.z } },
            };
            for (auto& edge : edges) {
                dc.drawThickLine(edge.first, edge.second, th, col);
            }
        }
    }

    dc.flush();
}

void BlockESP::onRenderOverlay(Event&) {
    if (!AntiObs::isActive()) return;
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;

    D2DUtil dc;
    renderProjectedBoxes(dc);
}

void BlockESP::renderProjectedBoxes(DrawUtil& dc) {
    if (found.empty() || entries.empty()) return;

    auto frame = RenderFrameState::get().latest();
    if (!frame) return;

    size_t cap = std::min(found.size(), static_cast<size_t>(std::get<FloatValue>(maxBlocks).value));
    for (size_t i = 0; i < cap; i++) {
        auto& f = found[i];
        if (f.entryIdx < 0 || f.entryIdx >= static_cast<int>(entries.size())) continue;
        auto& e = *entries[f.entryIdx];

        auto col = d2d::Color(std::get<ColorValue>(e.color).getMainColor());
        float thickness = std::max(std::get<FloatValue>(e.thickness).value, 1.f);

        Vec3 l { static_cast<float>(f.pos.x), static_cast<float>(f.pos.y), static_cast<float>(f.pos.z) };
        Vec3 h { l.x + 1.f, l.y + 1.f, l.z + 1.f };

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        bool anyVisible = false;

        for (int corner = 0; corner < 8; corner++) {
            Vec3 point { (corner & 1) ? h.x : l.x, (corner & 2) ? h.y : l.y, (corner & 4) ? h.z : l.z };
            auto projected = WorldToScreen::convert(point, frame->projection);
            if (!projected) continue;
            anyVisible = true;
            minX = std::min(minX, projected->x);
            minY = std::min(minY, projected->y);
            maxX = std::max(maxX, projected->x);
            maxY = std::max(maxY, projected->y);
        }

        if (!anyVisible) continue;
        if (maxX <= minX || maxY <= minY) continue;

        dc.drawRectangle({ minX, minY, maxX, maxY }, col, thickness);
    }
}

void BlockESP::onRenderLayer(RenderLayerEvent& event) {
    if (iconDraws.empty()) return;
    if (AntiObs::isActive()) return;

    auto view = event.getScreenView();
    if (!view || !view->visualTree || !view->visualTree->rootControl) return;
    if (view->visualTree->rootControl->name != "debug_screen") return;

    if (!Signatures::ItemStack_ItemStackBlock.result || !Signatures::ItemStackBase_destructor.result) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft || !ci->minecraft->getLevel()) return;

    MCDrawUtil dc { event.getUIRenderContext(), Necromancer::get().getFont() };

    for (auto& d : iconDraws) {
        dc.fillRectangle(d.rect, d2d::Color(0.055f, 0.055f, 0.055f, 1.f));
    }
    dc.flush(false);

    for (auto& d : iconDraws) {
        if (!d.block) continue;

        auto legacy = safeReadAs<void*>(reinterpret_cast<char const*>(d.block) + blockLegacyBackPtrOffset);
        if (!legacy || !*legacy) continue;

        float pad = d.rect.getWidth() * 0.08f;
        Vec2 pos { d.rect.left + pad, d.rect.top + pad };
        float scale = (d.rect.getWidth() - pad * 2.f) / 48.f;

        alignas(SDK::ItemStack) char storage[sizeof(SDK::ItemStack)] = {};
        auto stack =
            SDK::ItemStack::constructFromBlock(storage, *reinterpret_cast<SDK::Block const*>(d.block), 1, nullptr);
        if (!stack) continue;

        stack->showPickUp = false;
        stack->wasPickedUp = false;
        stack->pickupTime = {};
        if (stack->getItem()) {
            dc.drawItem(stack, pos, scale, 1.f);
        }
        stack->destruct();
    }

    dc.flush();
}
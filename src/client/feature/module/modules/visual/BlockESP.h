#pragma once
#include "../../Module.h"
#include "client/event/events/RenderLevelEvent.h"
#include "client/event/events/RenderLayerEvent.h"
#include "util/DxUtil.h"
#include "util/LMath.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace SDK {
    class Block;
}

class BlockESP : public Module {
public:
    struct BlockEntry {
        std::string id;
        std::wstring displayName;
        ValueType color = ColorValue(1.f, 0.25f, 0.25f, 1.f);
        ValueType thickness = FloatValue(1.f);
        std::shared_ptr<Setting> colorSetting;
        std::shared_ptr<Setting> thicknessSetting;
    };

    struct CatalogEntry {
        void* block = nullptr;
        std::string id;
        std::wstring displayName;
        std::wstring searchKey;
    };

    struct IconDraw {
        d2d::Rect rect;
        void* block = nullptr;
    };

    BlockESP();

    void onRenderLevel(RenderLevelEvent& event);
    void onRenderLayer(RenderLayerEvent& event);
    void onRenderOverlay(class Event& event);
    void onLeaveGame(class Event& event);

    [[nodiscard]] std::vector<std::shared_ptr<BlockEntry>>& getEntries() { return entries; }
    [[nodiscard]] bool hasBlock(std::string const& id) const;
    void addBlock(CatalogEntry const& cat);
    void removeBlock(size_t index);
    [[nodiscard]] class Setting* findEntrySetting(std::string const& name);

    std::vector<CatalogEntry> const& getCatalog();
    void rebuildCatalog();
    [[nodiscard]] IconDraw findIconSource(std::string const& id);

    void setIconDraws(std::vector<IconDraw>&& draws) { iconDraws = std::move(draws); }
    void clearIconDraws() { iconDraws.clear(); }

    void persist();
    [[nodiscard]] std::string serializedBlocks() const;

private:
    void parseBlockData();
    void bindEntrySettings(BlockEntry& entry);
    void invalidateScan();
    void renderProjectedBoxes(class DrawUtil& dc);

    ValueType blocksButton = ButtonValue();
    ValueType scanDelay = FloatValue(1.5f);
    ValueType maxBlocks = FloatValue(256.f);
    ValueType blockData = TextValue(L"[]");

    std::vector<std::shared_ptr<BlockEntry>> entries;
    std::vector<CatalogEntry> catalog;
    std::vector<IconDraw> iconDraws;

    struct FoundBlock {
        BlockPos pos;
        int entryIdx;
        float distSq;
    };
    std::vector<FoundBlock> found;
    std::vector<FoundBlock> pending;
    std::unordered_map<SDK::Block const*, int> blockLookup;
    std::chrono::steady_clock::time_point lastSweep {};
    BlockPos scanCenter {};
    int scanCursor = 0;
    int scanY = -64;
    bool scanActive = false;
    bool scanDirty = true;
};

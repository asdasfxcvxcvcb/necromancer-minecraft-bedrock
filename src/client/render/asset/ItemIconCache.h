#pragma once
#include <d2d1_1.h>
#include <string>
#include <unordered_map>
#include <wrl/client.h>

namespace SDK {
    class ItemStack;
}
class DrawUtil;
struct Vec2;

// Decodes the embedded item textures into D2D bitmaps so the Direct2D overlay can
// draw real item icons. The game's own item renderer is only reachable through
// MCDrawUtil, which AntiObs deliberately bypasses.
class ItemIconCache {
public:
    static ItemIconCache& get();

    // Returns nullptr when the id has no embedded texture, which is the signal to
    // fall back to a letter badge (custom server items, for example).
    ID2D1Bitmap* getBitmap(std::string const& itemId);

    // Draws an item on whichever renderer is active: the game's item renderer when
    // available, otherwise the embedded texture through D2D. Returns false when
    // neither could produce an icon, so callers can draw their own placeholder.
    static bool drawItem(DrawUtil& dc, SDK::ItemStack* stack, Vec2 const& pos, float size);

    // D2D bitmaps belong to the device context that created them, so everything has
    // to be dropped whenever the render target is rebuilt.
    void invalidate();

private:
    ItemIconCache() = default;

    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap>> cache;
    ID2D1DeviceContext* ownerCtx = nullptr;
};

#include "pch.h"
#include "ItemSwitcher.h"

#include "client/Necromancer.h"
#include "client/event/events/TickEvent.h"
#include "client/screen/ScreenManager.h"
#include "client/screen/screens/ClickGUI.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/player/LocalPlayer.h"
#include "mc/common/world/actor/player/PlayerInventory.h"
#include "mc/common/world/actor/player/Inventory.h"
#include "mc/common/world/ItemStack.h"

ItemSwitcher::ItemSwitcher()
    : Module("ItemSwitcher", LocalizeString::get("client.module.itemSwitcher.name"),
             LocalizeString::get("client.module.itemSwitcher.desc"), GAME) {
    listen<TickEvent>(static_cast<EventListenerFunc>(&ItemSwitcher::onTick));

    auto pickerBtn = addSetting("openPicker", LocalizeString::get("client.module.itemSwitcher.openPicker.name"),
                                LocalizeString::get("client.module.itemSwitcher.openPicker.desc"), openPicker);
    pickerBtn->callback = [this](Setting&) {
        Necromancer::getScreenManager().get<ClickGUI>().openItemSwitcher(this);
    };

    addSetting("switchOnce", LocalizeString::get("client.module.itemSwitcher.switchOnce.name"),
               LocalizeString::get("client.module.itemSwitcher.switchOnce.desc"), switchOnce);

    auto data = addSetting("targetItem", L"targetItem", L"", targetItem);
    data->visible = false;
}

void ItemSwitcher::onEnable() {
    hasSwitched = false;
}

void ItemSwitcher::onTick(Event&) {
    auto ci = SDK::ClientInstance::get();
    auto plr = ci ? ci->getLocalPlayer() : nullptr;
    if (!plr || !plr->supplies || !plr->supplies->inventory) return;
    if (Necromancer::get().getScreenManager().getActiveScreen().has_value()) return;

    std::wstring target = std::get<TextValue>(targetItem).str;
    if (target.empty()) return;

    bool once = std::get<BoolValue>(switchOnce);
    if (once && hasSwitched) return;

    auto inv = plr->supplies->inventory;
    int sel = plr->supplies->selectedSlot;
    if (sel < 0 || sel >= 9) return;

    auto* held = inv->getItem(sel);
    if (held && held->getItem()) {
        std::string heldId = held->getItem()->namespacedId.getString();
        std::wstring heldWide(heldId.begin(), heldId.end());
        if (heldWide == target) {
            if (once) hasSwitched = true;
            return;
        }
    }

    for (int i = 0; i < 9; i++) {
        if (i == sel) continue;
        auto* stack = inv->getItem(i);
        if (!stack || !stack->getItem()) continue;
        std::string stackId = stack->getItem()->namespacedId.getString();
        std::wstring stackWide(stackId.begin(), stackId.end());
        if (stackWide == target) {
            plr->supplies->selectedSlot = i;
            if (once) hasSwitched = true;
            return;
        }
    }
}
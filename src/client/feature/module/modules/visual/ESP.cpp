#include "pch.h"
#include "ESP.h"
#include "AntiObs.h"
#include "client/misc/EntityCache.h"
#include "client/misc/RenderFrameState.h"
#include "client/misc/TargetManager.h"
#include <client/screen/ScreenManager.h>
#include <client/misc/PlayerListManager.h>
#include <util/DrawUtil3D.h>
#include <util/WorldToScreen.h>
#include <client/event/events/RenderOverlayEvent.h>
#include <mc/common/client/gui/controls/UIControl.h>
#include <mc/common/client/gui/controls/VisualTree.h>
#include <mc/common/world/actor/player/Player.h>
#include <mc/common/world/actor/item/ItemActor.h>
#include <mc/common/world/actor/player/PlayerInventory.h>
#include <mc/common/world/actor/player/Inventory.h>
#include <mc/common/world/ItemStack.h>
#include <mc/common/entity/component/ActorEquipmentComponent.h>
#include <mc/common/client/game/MinecraftGame.h>
#include <mc/common/client/renderer/GameRenderer.h>

namespace {
    d2d::Color resolveEntityColor(ColorValue const& colVal, d2d::Color const& base, SDK::Actor* entt) {
        if (!colVal.forceTagColor || !entt || !entt->isPlayer()) return base;
        auto* tag = PlayerListManager::get().getColorTag(reinterpret_cast<SDK::Player*>(entt)->playerName);
        if (!tag) return base;
        return d2d::Color(tag->r, tag->g, tag->b, base.a);
    }

    using EntKind = EntityCache::EntKind;

    bool entityKindAllowed(EntKind kind, bool doPlayers, bool doMobs, bool doItems, bool doOthers) {
        switch (kind) {
        case EntKind::Player: return doPlayers;
        case EntKind::Item: return doItems;
        case EntKind::Mob: return doMobs;
        default: return doOthers;
        }
    }

    struct NameEntry {
        std::wstring text;
        std::chrono::steady_clock::time_point at {};
    };

    std::wstring const& displayNameFor(SDK::Actor* entt, bool isPlayer, bool isItemEnt) {
        thread_local std::unordered_map<uint64_t, NameEntry> cache;
        if (cache.size() > 256) cache.clear();
        auto now = std::chrono::steady_clock::now();
        auto& entry = cache[entt->getRuntimeID()];
        if (!entry.text.empty() && now - entry.at < 500ms) return entry.text;
        entry.at = now;
        if (isPlayer) {
            entry.text = util::StrToWStr(reinterpret_cast<SDK::Player*>(entt)->playerName);
        } else if (isItemEnt) {
            entry.text.clear();
            auto* itemActor = reinterpret_cast<SDK::ItemActor*>(entt);
            if (auto* stack = itemActor->getItemStack(); stack && stack->getItem()) {
                entry.text = util::StrToWStr(stack->getHoverName());
            }
        } else {
            entry.text = util::StrToWStr(entt->getEntityTypeName());
        }
        return entry.text;
    }
}

ESP::ESP()
    : Module("ESP", LocalizeString::get("client.module.esp.name"),
             LocalizeString::get("client.module.esp.desc"), GAME) {
    addSetting("players", LocalizeString::get("client.module.esp.players.name"),
               LocalizeString::get("client.module.esp.players.desc"), players);
    addSetting("mobs", LocalizeString::get("client.module.esp.mobs.name"),
               LocalizeString::get("client.module.esp.mobs.desc"), mobs);
    addSetting("itemEntities", LocalizeString::get("client.module.esp.itemEntities.name"),
               LocalizeString::get("client.module.esp.itemEntities.desc"), itemEntities);
    addSetting("others", LocalizeString::get("client.module.esp.others.name"),
               LocalizeString::get("client.module.esp.others.desc"), others);

    addSetting("targeted", LocalizeString::get("client.module.esp.targeted.name"),
               LocalizeString::get("client.module.esp.targeted.desc"), targeted);
    addSetting("targetedName", LocalizeString::get("client.module.esp.targetedName.name"),
               LocalizeString::get("client.module.esp.targetedName.desc"), targetedName, "targeted"_istrue);
    addSetting("targetedNameColor", LocalizeString::get("client.module.esp.targetedNameColor.name"),
               LocalizeString::get("client.module.esp.targetedNameColor.desc"), targetedNameColor,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "targeted", { 1 }, false },
                   { "targetedName", { 1 }, false },
               }));
    addSetting("targetedHealth", LocalizeString::get("client.module.esp.targetedHealth.name"),
               LocalizeString::get("client.module.esp.targetedHealth.desc"), targetedHealth, "targeted"_istrue);
    addSetting("targetedIcons", LocalizeString::get("client.module.esp.targetedIcons.name"),
               LocalizeString::get("client.module.esp.targetedIcons.desc"), targetedIcons, "targeted"_istrue);

    addSetting("hitbox", LocalizeString::get("client.module.esp.hitbox.name"),
               LocalizeString::get("client.module.esp.hitbox.desc"), hitbox);
    addSetting("hitboxColor", LocalizeString::get("client.module.esp.hitboxColor.name"),
               LocalizeString::get("client.module.esp.hitboxColor.desc"), hitboxColor, "hitbox"_istrue)
        ->supportsTagColor = true;
    addSliderSetting("hitboxThickness", LocalizeString::get("client.module.esp.hitboxThickness.name"),
                     LocalizeString::get("client.module.esp.hitboxThickness.desc"), hitboxThickness,
                     FloatValue(0.1f), FloatValue(1.f), FloatValue(0.1f), "hitbox"_istrue);

    Setting::Condition losVisible { { { "lineOfSight", { 1 }, false } } };

    addSetting("lineOfSight", LocalizeString::get("client.module.esp.lineOfSight.name"),
               LocalizeString::get("client.module.esp.lineOfSight.desc"), lineOfSight);
    addSetting("lineOfSightColor", LocalizeString::get("client.module.esp.lineOfSightColor.name"),
               LocalizeString::get("client.module.esp.lineOfSightColor.desc"), lineOfSightColor, losVisible)
        ->supportsTagColor = true;
    addSliderSetting("lineOfSightThickness", LocalizeString::get("client.module.esp.lineOfSightThickness.name"),
                     LocalizeString::get("client.module.esp.lineOfSightThickness.desc"), lineOfSightThickness,
                     FloatValue(0.1f), FloatValue(1.f), FloatValue(0.1f), losVisible);

    namePos.addEntry(EnumEntry(0, LocalizeString::get("client.module.esp.pos.top.name"),
                               LocalizeString::get("client.module.esp.pos.top.desc")));
    namePos.addEntry(EnumEntry(1, LocalizeString::get("client.module.esp.pos.bottom.name"),
                               LocalizeString::get("client.module.esp.pos.bottom.desc")));

    addSetting("nametag", LocalizeString::get("client.module.esp.nametag.name"),
               LocalizeString::get("client.module.esp.nametag.desc"), nametag);
    addEnumSetting("namePos", LocalizeString::get("client.module.esp.namePos.name"),
                   LocalizeString::get("client.module.esp.namePos.desc"), namePos, "nametag"_istrue);
    addSliderSetting("nameSize", LocalizeString::get("client.module.esp.nameSize.name"),
                     LocalizeString::get("client.module.esp.nameSize.desc"), nameSize, FloatValue(8.f),
                     FloatValue(40.f), FloatValue(1.f), "nametag"_istrue);
    addSetting("nameColor", LocalizeString::get("client.module.esp.nameColor.name"),
               LocalizeString::get("client.module.esp.nameColor.desc"), nameColor, "nametag"_istrue)
        ->supportsTagColor = true;
    addSetting("nametagBg", LocalizeString::get("client.module.esp.nametagBg.name"),
               LocalizeString::get("client.module.esp.nametagBg.desc"), nametagBg, "nametag"_istrue);
    addSetting("nametagBgColor", LocalizeString::get("client.module.esp.nametagBgColor.name"),
               LocalizeString::get("client.module.esp.nametagBgColor.desc"), nametagBgColor,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "nametag", { 1 }, false },
                   { "nametagBg", { 1 }, false },
               }));

    healthMode.addEntry(EnumEntry(0, LocalizeString::get("client.module.esp.healthMode.bar.name"),
                                  LocalizeString::get("client.module.esp.healthMode.bar.desc")));
    healthMode.addEntry(EnumEntry(1, LocalizeString::get("client.module.esp.healthMode.text.name"),
                                  LocalizeString::get("client.module.esp.healthMode.text.desc")));
    healthMode.addEntry(EnumEntry(2, LocalizeString::get("client.module.esp.healthMode.nameBg.name"),
                                  LocalizeString::get("client.module.esp.healthMode.nameBg.desc")));

    healthPos.addEntry(EnumEntry(0, LocalizeString::get("client.module.esp.pos.top.name"),
                                 LocalizeString::get("client.module.esp.pos.top.desc")));
    healthPos.addEntry(EnumEntry(1, LocalizeString::get("client.module.esp.pos.bottom.name"),
                                 LocalizeString::get("client.module.esp.pos.bottom.desc")));
    healthPos.addEntry(EnumEntry(2, LocalizeString::get("client.module.esp.pos.left.name"),
                                 LocalizeString::get("client.module.esp.pos.left.desc")));
    healthPos.addEntry(EnumEntry(3, LocalizeString::get("client.module.esp.pos.right.name"),
                                 LocalizeString::get("client.module.esp.pos.right.desc")));

    addSetting("health", LocalizeString::get("client.module.esp.health.name"),
               LocalizeString::get("client.module.esp.health.desc"), health);
    addEnumSetting("healthMode", LocalizeString::get("client.module.esp.healthMode.name"),
                   LocalizeString::get("client.module.esp.healthMode.desc"), healthMode, "health"_istrue);
    addEnumSetting("healthPos", LocalizeString::get("client.module.esp.healthPos.name"),
                   LocalizeString::get("client.module.esp.healthPos.desc"), healthPos, "health"_istrue);
    addSliderSetting("healthBarThickness", LocalizeString::get("client.module.esp.healthBarThickness.name"),
                     LocalizeString::get("client.module.esp.healthBarThickness.desc"), healthBarThickness,
                     FloatValue(2.f), FloatValue(20.f), FloatValue(1.f),
                     Setting::Condition(std::vector<Setting::SingleCond> {
                         { "health", { 1 }, false },
                         { "healthMode", { 0, 2 }, false },
                     }));
    addSliderSetting("healthTextSize", LocalizeString::get("client.module.esp.healthTextSize.name"),
                     LocalizeString::get("client.module.esp.healthTextSize.desc"), healthTextSize, FloatValue(8.f),
                     FloatValue(40.f), FloatValue(1.f),
                     Setting::Condition(std::vector<Setting::SingleCond> {
                         { "health", { 1 }, false },
                         { "healthMode", { 1 }, false },
                     }));
    addSetting("healthBarOutline", LocalizeString::get("client.module.esp.healthBarOutline.name"),
               LocalizeString::get("client.module.esp.healthBarOutline.desc"), healthBarOutline,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "health", { 1 }, false },
                   { "healthMode", { 0, 2 }, false },
               }));
    addSetting("healthBarOutlineColor", LocalizeString::get("client.module.esp.healthBarOutlineColor.name"),
               LocalizeString::get("client.module.esp.healthBarOutlineColor.desc"), healthBarOutlineColor,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "health", { 1 }, false },
                   { "healthMode", { 0, 2 }, false },
                   { "healthBarOutline", { 1 }, false },
               }));
    addSetting("healthTextOutline", LocalizeString::get("client.module.esp.healthTextOutline.name"),
               LocalizeString::get("client.module.esp.healthTextOutline.desc"), healthTextOutline,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "health", { 1 }, false },
                   { "healthMode", { 1 }, false },
               }));
    addSetting("healthTextOutlineColor", LocalizeString::get("client.module.esp.healthTextOutlineColor.name"),
               LocalizeString::get("client.module.esp.healthTextOutlineColor.desc"), healthTextOutlineColor,
               Setting::Condition(std::vector<Setting::SingleCond> {
                   { "health", { 1 }, false },
                   { "healthMode", { 1 }, false },
                   { "healthTextOutline", { 1 }, false },
               }));

    itemsPos.addEntry(EnumEntry(0, LocalizeString::get("client.module.esp.pos.top.name"),
                                LocalizeString::get("client.module.esp.pos.top.desc")));
    itemsPos.addEntry(EnumEntry(1, LocalizeString::get("client.module.esp.pos.bottom.name"),
                                LocalizeString::get("client.module.esp.pos.bottom.desc")));
    itemsPos.addEntry(EnumEntry(2, LocalizeString::get("client.module.esp.pos.left.name"),
                                LocalizeString::get("client.module.esp.pos.left.desc")));
    itemsPos.addEntry(EnumEntry(3, LocalizeString::get("client.module.esp.pos.right.name"),
                                LocalizeString::get("client.module.esp.pos.right.desc")));

    Setting::Condition itemsVisible { { { "items", { 1 }, false } } };

    addSetting("items", LocalizeString::get("client.module.esp.items.name"),
               LocalizeString::get("client.module.esp.items.desc"), items);
    addEnumSetting("itemsPos", LocalizeString::get("client.module.esp.itemsPos.name"),
                   LocalizeString::get("client.module.esp.itemsPos.desc"), itemsPos, itemsVisible);
    addSliderSetting("itemsSize", LocalizeString::get("client.module.esp.itemsSize.name"),
                     LocalizeString::get("client.module.esp.itemsSize.desc"), itemsSize, FloatValue(10.f),
                     FloatValue(40.f), FloatValue(1.f), itemsVisible);
    addSetting("itemsBg", LocalizeString::get("client.module.esp.itemsBg.name"),
               LocalizeString::get("client.module.esp.itemsBg.desc"), itemsBg, itemsVisible);

    Eventing::get().listen<RenderLevelEvent, &ESP::onRenderLevel>(this);
    Eventing::get().listen<RenderLayerEvent, &ESP::onRenderLayer>(this);
    Eventing::get().listen<RenderOverlayEvent, &ESP::onRenderOverlay>(this);
}

void ESP::afterLoadConfig() {
    if (namePos.getSelectedKey() > 1) namePos.setSelectedKey(0);
}

void ESP::onRenderLevel(RenderLevelEvent& event) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;

    auto level = ci->minecraft->getLevel();
    if (!level) return;

    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    bool isAntiObs = AntiObs::isActive();
    bool doHitbox = std::get<BoolValue>(hitbox) && !isAntiObs;
    bool doLos = std::get<BoolValue>(lineOfSight) && !isAntiObs;
    bool doPlayers = std::get<BoolValue>(players);
    bool doMobs = std::get<BoolValue>(mobs);
    bool doItemEntities = std::get<BoolValue>(itemEntities);
    bool doOthers = std::get<BoolValue>(others);
    bool doTargeted = std::get<BoolValue>(targeted);
    if (!doHitbox && !doLos) return;
    if (!doPlayers && !doMobs && !doItemEntities && !doOthers && !doTargeted) return;

    MCDrawUtil3D dc { ci->levelRenderer, SDK::ScreenContext::instance3d, SDK::MaterialPtr::getUIColor() };

    float alpha = ci->minecraft->timer->alpha;
    float boxTh = std::get<FloatValue>(hitboxThickness).value / 10.f;
    float losTh = std::get<FloatValue>(lineOfSightThickness).value / 10.f;

    auto& hitboxColVal = std::get<ColorValue>(hitboxColor);
    d2d::Color hitboxBase(hitboxColVal.getMainColor());
    auto& losColVal = std::get<ColorValue>(lineOfSightColor);
    d2d::Color losBase(losColVal.getMainColor());

    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp) continue;
        bool typeAllowed3D = entityKindAllowed(view.kind, doPlayers, doMobs, doItemEntities, doOthers);
        if (!typeAllowed3D && !(doTargeted && TargetManager::isTargetedId(view.runtimeId))) continue;
        if (view.invisible) continue;

        Vec3 const& pos = entt->getPos();
        Vec3 const& posOld = entt->getPosOld();
        Vec3 newPos = { std::lerp(posOld.x, pos.x, alpha), std::lerp(posOld.y, pos.y, alpha),
                        std::lerp(posOld.z, pos.z, alpha) };

        AABB bb = entt->getBoundingBox();
        float eyeOffset = pos.y - bb.lower.y;
        bb.rebase(newPos - Vec3 { 0.f, eyeOffset, 0.f } + Vec3 { 0.f, (bb.higher.y - bb.lower.y) / 2.f, 0.f });

        if (doHitbox) {
            dc.drawThickBox(bb, boxTh, resolveEntityColor(hitboxColVal, hitboxBase, entt));
        }

        if (doLos && !view.isItem) {
            float calcYaw = (entt->getRot().y + 90.f) * (pi_f / 180.f);
            float calcPitch = entt->getRot().x * -(pi_f / 180.f);

            Vec3 begin = { newPos.x, NecromancerMath::aequals(bb.lower.y, newPos.y)
                                           ? bb.lower.y + (bb.higher.y - bb.lower.y) * 0.85f
                                           : newPos.y,
                           newPos.z };
            Vec3 end = begin + Vec3 { cos(calcYaw) * cos(calcPitch), sin(calcPitch),
                                      sin(calcYaw) * cos(calcPitch) };

            dc.drawThickLine(begin, end, losTh, resolveEntityColor(losColVal, losBase, entt));
        }
    }

    dc.flush();
}

namespace {
    constexpr float espPad = 6.f;
    constexpr float espGap = 2.f;
    constexpr float hpBarLength = 60.f;

    d2d::Color healthColor(float frac) {
        return { 1.f - std::clamp(frac, 0.f, 1.f), std::clamp(frac, 0.f, 1.f), 0.f, 1.f };
    }
}

void ESP::onRenderLayer(RenderLayerEvent& event) {
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;

    auto* screenView = event.getScreenView();
    if (!screenView || !screenView->visualTree || !screenView->visualTree->rootControl ||
        screenView->visualTree->rootControl->name != "hud_screen")
        return;

    bool isAntiObs = AntiObs::isActive();
    if (isAntiObs) return;

    MCDrawUtil dc { event.getUIRenderContext(), Necromancer::get().getFont() };
    renderEntityOverlay(dc, true, true);
    dc.flush();
}

void ESP::onRenderOverlay(Event&) {
    if (!AntiObs::isActive()) return;
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;

    D2DUtil dc;
    renderProjectedBoxes(dc);
    renderEntityOverlay(dc, true, false);
}

void ESP::renderProjectedBoxes(DrawUtil& dc) {
    if (!std::get<BoolValue>(hitbox)) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;

    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    auto frame = RenderFrameState::get().latest();
    if (!frame || frame->entities.empty()) return;

    bool doPlayers = std::get<BoolValue>(players);
    bool doMobs = std::get<BoolValue>(mobs);
    bool doItemEntities = std::get<BoolValue>(itemEntities);
    bool doOthers = std::get<BoolValue>(others);
    bool doTargeted = std::get<BoolValue>(targeted);
    if (!doPlayers && !doMobs && !doItemEntities && !doOthers && !doTargeted) return;

    auto& hitboxColVal = std::get<ColorValue>(hitboxColor);
    d2d::Color hitboxBase(hitboxColVal.getMainColor());
    float thickness = std::max(std::get<FloatValue>(hitboxThickness).value * 3.f, 1.f);

    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp) continue;
        bool typeAllowed = entityKindAllowed(view.kind, doPlayers, doMobs, doItemEntities, doOthers);
        if (!typeAllowed && !(doTargeted && TargetManager::isTargetedId(view.runtimeId))) continue;
        if (view.invisible) continue;

        auto const* geo = frame->find(view.runtimeId);
        if (!geo) continue;

        AABB const& bb = geo->box;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        bool anyVisible = false;

        for (int corner = 0; corner < 8; corner++) {
            Vec3 point { (corner & 1) ? bb.higher.x : bb.lower.x, (corner & 2) ? bb.higher.y : bb.lower.y,
                         (corner & 4) ? bb.higher.z : bb.lower.z };
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

        dc.drawRectangle({ minX, minY, maxX, maxY }, resolveEntityColor(hitboxColVal, hitboxBase, entt), thickness);
    }
}

void ESP::renderEntityOverlay(DrawUtil& dc, bool emitText, bool emitIcons) {
    bool doName = emitText && std::get<BoolValue>(nametag);
    bool doHealth = emitText && std::get<BoolValue>(health);
    bool doItems = emitIcons && std::get<BoolValue>(items);

    bool doTargeted = std::get<BoolValue>(targeted);
    bool doTargetedName = emitText && std::get<BoolValue>(targetedName);
    bool doTargetedHealth = emitText && std::get<BoolValue>(targetedHealth);
    bool doTargetedIcons = emitIcons && std::get<BoolValue>(targetedIcons);
    bool anyTargetedElement = doTargeted && (doTargetedName || doTargetedHealth || doTargetedIcons);
    if (!doName && !doHealth && !doItems && !anyTargetedElement) return;

    bool doPlayers = std::get<BoolValue>(players);
    bool doMobs = std::get<BoolValue>(mobs);
    bool doItemEntities = std::get<BoolValue>(itemEntities);
    bool doOthers = std::get<BoolValue>(others);
    if (!doPlayers && !doMobs && !doItemEntities && !doOthers && !doTargeted) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;

    auto level = ci->minecraft->getLevel();
    if (!level) return;

    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    auto gr = ci->minecraftGame ? ci->minecraftGame->gameRenderer : nullptr;
    if (!gr) return;

    bool useFrozen = !dc.isMinecraft();
    auto frame = useFrozen ? RenderFrameState::get().latest() : nullptr;
    bool hasFrozen = frame && !frame->entities.empty();
    if (useFrozen && !hasFrozen) return;

    Vec3 camRight;
    std::optional<WorldToScreen::ProjectionContext> projectionContext;
    if (hasFrozen) {
        camRight = frame->camRight;
        projectionContext = frame->projection;
    } else {
        glm::mat4& viewMatrix = gr->lastViewMatrix._m;
        camRight = Vec3 { viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0] }.normalized();
        projectionContext = WorldToScreen::createContext();
    }
    if (!projectionContext) return;

    int namePosKey = namePos.getSelectedKey();
    int healthPosKey = healthPos.getSelectedKey();
    int itemsPosKey = itemsPos.getSelectedKey();
    int healthModeKey = healthMode.getSelectedKey();
    float nameSz = std::get<FloatValue>(nameSize).value;
    float hpTextSz = std::get<FloatValue>(healthTextSize).value;
    float hpBarTh = std::get<FloatValue>(healthBarThickness).value;
    float itemSz = std::get<FloatValue>(itemsSize).value;
    float alpha = ci->minecraft->timer->alpha;

    auto& nameColVal = std::get<ColorValue>(nameColor);
    d2d::Color nameBase(nameColVal.getMainColor());
    d2d::Color targetedNameCol(std::get<ColorValue>(targetedNameColor).getMainColor());

    auto slotCursor = [&](int pos, float pad) {
        switch (pos) {
        case 0: return Vec2 { 0.f, -pad };
        case 1: return Vec2 { 0.f, pad };
        case 2: return Vec2 { -pad, 0.f };
        default: return Vec2 { pad, 0.f };
        }
    };

    std::vector<SDK::ItemStack*> stacks;
    stacks.reserve(5);
    const bool needSideAnchors = ((doName || (doTargeted && doTargetedName)) && namePosKey >= 2) ||
                                 ((doHealth || (doTargeted && doTargetedHealth)) && healthPosKey >= 2) ||
                                 ((doItems || (doTargeted && doTargetedIcons)) && itemsPosKey >= 2);

    auto advance = [&](Vec2& cursor, int pos, float extent, float gap) {
        float step = extent + gap;
        switch (pos) {
        case 0: cursor.y -= step; break;
        case 1: cursor.y += step; break;
        case 2: cursor.x -= step; break;
        default: cursor.x += step; break;
        }
    };

    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp) continue;
        bool isPlayer = view.isPlayer;
        bool isItemEnt = view.isItem;
        bool useTargeted = doTargeted && TargetManager::isTargetedId(view.runtimeId);
        bool typeAllowed = entityKindAllowed(view.kind, doPlayers, doMobs, doItemEntities, doOthers);
        if (!typeAllowed && !useTargeted) continue;
        if (view.invisible) continue;

        bool showName = useTargeted ? doTargetedName : doName;
        bool showHealth = useTargeted ? doTargetedHealth : doHealth;
        bool showIcons = useTargeted ? doTargetedIcons : doItems;

        Vec3 newPos;
        AABB bb;
        bool geometryOk;
        if (hasFrozen) {
            auto const* geo = frame->find(view.runtimeId);
            geometryOk = geo != nullptr;
            if (geometryOk) {
                newPos = geo->interpolatedPos;
                bb = geo->box;
            }
        } else {
            Vec3 const& pos = entt->getPos();
            Vec3 const& posOld = entt->getPosOld();
            newPos = { std::lerp(posOld.x, pos.x, alpha), std::lerp(posOld.y, pos.y, alpha),
                       std::lerp(posOld.z, pos.z, alpha) };
            bb = entt->getBoundingBox();
            float eyeOffset = pos.y - bb.lower.y;
            bb.rebase(newPos - Vec3 { 0.f, eyeOffset, 0.f } + Vec3 { 0.f, (bb.higher.y - bb.lower.y) / 2.f, 0.f });
            geometryOk = true;
        }
        if (!geometryOk) continue;

        float halfWidth = std::max(bb.higher.x - bb.lower.x, bb.higher.z - bb.lower.z) * 0.5f + 0.1f;
        float halfHeight = (bb.higher.y - bb.lower.y) * 0.5f + 0.1f;
        Vec3 center = bb.getCenter();

        auto topOpt = WorldToScreen::convert({ center.x, center.y + halfHeight, center.z }, *projectionContext);
        auto botOpt = WorldToScreen::convert({ center.x, center.y - halfHeight, center.z }, *projectionContext);
        if (!topOpt || !botOpt) continue;

        std::optional<Vec2> leftOpt;
        std::optional<Vec2> rightOpt;
        if (needSideAnchors) {
            leftOpt = WorldToScreen::convert(center - camRight * halfWidth, *projectionContext);
            rightOpt = WorldToScreen::convert(center + camRight * halfWidth, *projectionContext);
            if (!leftOpt || !rightOpt) continue;
        }

        float projH = std::abs(topOpt->y - botOpt->y);
        float s = std::clamp(projH / 55.f, 0.3f, 1.0f);
        float sIcon = std::clamp(projH / 55.f, 0.25f, 0.7f);
        float pad = espPad * s;
        float gap = espGap * s;
        float nameSzE = nameSz * s;
        float hpTextSzE = hpTextSz * s;
        float hpBarThE = hpBarTh * s;
        float hpBarLenE = hpBarLength * s;
        float itemSzE = itemSz * sIcon;

        Vec2 anchors[4] = { *topOpt, *botOpt, {}, {} };
        if (needSideAnchors) {
            anchors[2] = *leftOpt;
            anchors[3] = *rightOpt;
        }

        Vec2 cursors[4] = {};
        for (int i = 0; i < 4; i++) cursors[i] = slotCursor(i, pad);

        std::optional<float> hp, maxHp;
        if (showHealth && view.hasHealth) {
            hp = view.health;
            maxHp = view.maxHealth;
        }

        std::wstring nameText;
        d2d::Rect nameRc;
        d2d::Rect nameBgRc;
        bool nameDrawn = false;
        bool hasNameBg = false;

        if (showName) {
            Vec2 anchor = anchors[namePosKey];
            Vec2& cur = cursors[namePosKey];

            nameText = displayNameFor(entt, isPlayer, isItemEnt);
            if (!nameText.empty()) {
                Vec2 sz = dc.getTextSize(nameText, Renderer::FontSelection::PrimaryRegular, nameSzE);

                if (namePosKey == 0) {
                    nameRc = { anchor.x - sz.x * 0.5f, anchor.y + cur.y - sz.y, anchor.x + sz.x * 0.5f,
                               anchor.y + cur.y };
                } else {
                    nameRc = { anchor.x - sz.x * 0.5f, anchor.y + cur.y, anchor.x + sz.x * 0.5f,
                               anchor.y + cur.y + sz.y };
                }

                if (dc.isMinecraft()) {
                    auto& mcDc = static_cast<MCDrawUtil&>(dc);
                    if (mcDc.font) {
                        float lineHeight = std::max(mcDc.font->getLineHeight(), 1.f);
                        float renderedHeight = nameSzE * (10.f / lineHeight);
                        float renderedTop = nameRc.centerY(renderedHeight);
                        nameRc = { nameRc.left, renderedTop, nameRc.right, renderedTop + renderedHeight };
                    }
                }

                if (std::get<BoolValue>(nametagBg)) {
                    nameBgRc = { nameRc.left - 2.f, nameRc.top - 1.f, nameRc.right + 2.f, nameRc.bottom + 1.f };
                    dc.fillRectangle(nameBgRc,
                                     d2d::Color(std::get<ColorValue>(nametagBgColor).getMainColor()));
                    hasNameBg = true;
                } else {
                    nameBgRc = nameRc;
                }

                nameDrawn = true;
                advance(cur, namePosKey, sz.y + 2.f, gap);
            }
        }

        if (showHealth && hp && maxHp && *maxHp > 0.f) {
            Vec2 anchor = anchors[healthPosKey];
            Vec2& cur = cursors[healthPosKey];
            float frac = std::clamp(*hp / *maxHp, 0.f, 1.f);
            d2d::Color hpCol = healthColor(frac);

            bool barOutlineOn = std::get<BoolValue>(healthBarOutline);
            d2d::Color barOutlineCol = d2d::Color(std::get<ColorValue>(healthBarOutlineColor).getMainColor());

            if (healthModeKey == 2 && nameDrawn && hasNameBg) {
                d2d::Rect fill { nameBgRc.left, nameBgRc.top,
                                 nameBgRc.left + (nameBgRc.right - nameBgRc.left) * frac, nameBgRc.bottom };
                dc.fillRectangle(fill, hpCol);
                if (barOutlineOn) dc.drawRectangle(nameBgRc, barOutlineCol, 1.f);
            } else if (healthModeKey == 1) {
                std::wstring text = std::to_wstring(static_cast<int>(std::ceil(*hp)));
                Vec2 sz = dc.getTextSize(text, Renderer::FontSelection::PrimaryRegular, hpTextSzE, true, false);

                d2d::Rect rc;
                switch (healthPosKey) {
                case 0:
                    rc = { anchor.x - sz.x * 0.5f, anchor.y + cur.y - sz.y, anchor.x + sz.x * 0.5f,
                           anchor.y + cur.y };
                    break;
                case 1:
                    rc = { anchor.x - sz.x * 0.5f, anchor.y + cur.y, anchor.x + sz.x * 0.5f,
                           anchor.y + cur.y + sz.y };
                    break;
                case 2:
                    rc = { anchor.x + cur.x - sz.x, anchor.y - sz.y * 0.5f, anchor.x + cur.x,
                           anchor.y + sz.y * 0.5f };
                    break;
                default:
                    rc = { anchor.x + cur.x, anchor.y - sz.y * 0.5f, anchor.x + cur.x + sz.x,
                           anchor.y + sz.y * 0.5f };
                    break;
                }

                if (std::get<BoolValue>(healthTextOutline)) {
                    d2d::Color outlineCol = d2d::Color(std::get<ColorValue>(healthTextOutlineColor).getMainColor());
                    for (auto offset : { Vec2 { -1.f, 0.f }, Vec2 { 1.f, 0.f }, Vec2 { 0.f, -1.f }, Vec2 { 0.f, 1.f } }) {
                        d2d::Rect outlineRc { rc.left + offset.x, rc.top + offset.y, rc.right + offset.x,
                                              rc.bottom + offset.y };
                        dc.drawText(outlineRc, text, outlineCol, Renderer::FontSelection::PrimaryRegular, hpTextSzE,
                                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
                    }
                }

                dc.drawText(rc, text, hpCol, Renderer::FontSelection::PrimaryRegular, hpTextSzE,
                            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

                advance(cur, healthPosKey, sz.y, gap);
            } else {
                bool horizontal = healthPosKey <= 1;
                float len = hpBarLenE;
                float th = hpBarThE;

                d2d::Rect bg, fill;
                if (horizontal) {
                    float cx = anchor.x;
                    float y0 = healthPosKey == 0 ? anchor.y + cur.y - th : anchor.y + cur.y;
                    bg = { cx - len * 0.5f, y0, cx + len * 0.5f, y0 + th };
                    fill = { bg.left, bg.top, bg.left + len * frac, bg.bottom };
                } else {
                    float x0 = healthPosKey == 2 ? anchor.x + cur.x - th : anchor.x + cur.x;
                    float cy = anchor.y;
                    bg = { x0, cy - len * 0.5f, x0 + th, cy + len * 0.5f };
                    fill = { bg.left, bg.bottom - len * frac, bg.right, bg.bottom };
                }

                dc.fillRectangle(bg, { 0.f, 0.f, 0.f, 0.55f });
                dc.fillRectangle(fill, hpCol);
                if (barOutlineOn) dc.drawRectangle(bg, barOutlineCol, 1.f);

                advance(cur, healthPosKey, th, gap);
            }
        }

        if (nameDrawn) {
            d2d::Color nameCol = useTargeted ? targetedNameCol : resolveEntityColor(nameColVal, nameBase, entt);
            dc.drawText(nameRc, nameText, nameCol, Renderer::FontSelection::PrimaryRegular, nameSzE,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        if (showIcons && !isItemEnt) {
            stacks.clear();
            if (isPlayer) {
                auto player = reinterpret_cast<SDK::Player*>(entt);
                if (player->supplies && player->supplies->inventory) {
                    if (auto held = player->supplies->inventory->getItem(player->supplies->selectedSlot);
                        held && held->getItem()) {
                        stacks.push_back(held);
                    }
                }
                for (int slot = 0; slot < 4; slot++) {
                    if (auto arm = entt->getArmor(slot); arm && arm->getItem()) {
                        stacks.push_back(arm);
                    }
                }
            } else {
                if (auto equip = entt->tryGetComponent<SDK::ActorEquipmentComponent>()) {
                    if (equip->handContainer) {
                        if (auto held = equip->handContainer->getItem(0); held && held->getItem()) {
                            stacks.push_back(held);
                        }
                    }
                    if (equip->armorContainer) {
                        for (int slot = 0; slot < 4; slot++) {
                            if (auto arm = equip->armorContainer->getItem(slot); arm && arm->getItem()) {
                                stacks.push_back(arm);
                            }
                        }
                    }
                }
            }

            if (!stacks.empty()) {
                Vec2 anchor = anchors[itemsPosKey];
                Vec2& cur = cursors[itemsPosKey];
                float count = static_cast<float>(stacks.size());
                bool horizontal = itemsPosKey <= 1;
                float iconGap = std::max(3.f, itemSzE * 0.25f);
                float total = count * itemSzE + (count - 1.f) * iconGap;

                d2d::Rect bg;
                switch (itemsPosKey) {
                case 0:
                    bg = { anchor.x - total * 0.5f - 2.f, anchor.y + cur.y - itemSzE - 2.f,
                           anchor.x + total * 0.5f + 2.f, anchor.y + cur.y + 2.f };
                    break;
                case 1:
                    bg = { anchor.x - total * 0.5f - 2.f, anchor.y + cur.y - 2.f,
                           anchor.x + total * 0.5f + 2.f, anchor.y + cur.y + itemSzE + 2.f };
                    break;
                case 2:
                    bg = { anchor.x + cur.x - itemSzE - 2.f, anchor.y - total * 0.5f - 2.f,
                           anchor.x + cur.x + 2.f, anchor.y + total * 0.5f + 2.f };
                    break;
                default:
                    bg = { anchor.x + cur.x - 2.f, anchor.y - total * 0.5f - 2.f,
                           anchor.x + cur.x + itemSzE + 2.f, anchor.y + total * 0.5f + 2.f };
                    break;
                }
                if (std::get<BoolValue>(itemsBg)) {
                    dc.fillRectangle(bg, { 0.f, 0.f, 0.f, 0.45f });
                }

                if (dc.isMinecraft()) {
                    auto& mcDc = static_cast<MCDrawUtil&>(dc);
                    for (size_t i = 0; i < stacks.size(); i++) {
                        Vec2 iconPos;
                        if (horizontal) {
                            iconPos = { bg.left + 2.f + i * (itemSzE + iconGap), bg.top + 2.f };
                        } else {
                            iconPos = { bg.left + 2.f, bg.top + 2.f + i * (itemSzE + iconGap) };
                        }
                        mcDc.drawItem(stacks[i], iconPos, itemSzE / 48.f, 1.f);
                    }
                }

                advance(cur, itemsPosKey, itemSzE + 4.f, gap);
            }
        }
    }
}

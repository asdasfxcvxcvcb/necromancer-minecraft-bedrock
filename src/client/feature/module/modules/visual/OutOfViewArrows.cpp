#include "pch.h"
#include "OutOfViewArrows.h"
#include "AntiObs.h"
#include "client/Necromancer.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/event/events/RenderLayerEvent.h"
#include "client/misc/EntityCache.h"
#include "client/misc/PlayerListManager.h"
#include "client/misc/RenderFrameState.h"
#include "client/screen/ScreenManager.h"
#include "util/DrawContext.h"
#include "util/WorldToScreen.h"
#include "util/LMath.h"
#include <mc/common/client/game/ClientInstance.h>
#include <mc/common/client/gui/GuiData.h>
#include <mc/common/client/gui/controls/UIControl.h>
#include <mc/common/client/gui/controls/VisualTree.h>
#include <mc/common/client/renderer/game/LevelRendererPlayer.h>
#include <mc/common/world/Minecraft.h>
#include <mc/common/world/actor/Actor.h>
#include <mc/common/world/actor/player/Player.h>
#include <cmath>

namespace {
    bool validPoint(Vec2 const& value) {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    Vec2 rotateAround(Vec2 const& point, Vec2 const& center, float angle) {
        float c = std::cos(angle);
        float s = std::sin(angle);
        Vec2 local { point.x - center.x, point.y - center.y };
        return { center.x + local.x * c - local.y * s, center.y + local.x * s + local.y * c };
    }

    void fillRotatedQuad(DrawUtil& dc, Vec2 const& center, float length, float width, float angle,
                         d2d::Color const& col) {
        if (length <= 0.f || width <= 0.f) return;
        float hl = length * 0.5f;
        float hw = width * 0.5f;
        Vec2 p1 = rotateAround({ center.x - hl, center.y + hw }, center, angle);
        Vec2 p2 = rotateAround({ center.x + hl, center.y + hw }, center, angle);
        Vec2 p3 = rotateAround({ center.x + hl, center.y - hw }, center, angle);
        Vec2 p4 = rotateAround({ center.x - hl, center.y - hw }, center, angle);
        dc.fillTriangle(p1, p2, p3, col);
        dc.fillTriangle(p1, p3, p4, col);
    }

    void fillThickLine(DrawUtil& dc, Vec2 const& from, Vec2 const& to, float width, d2d::Color const& col) {
        Vec2 delta { to.x - from.x, to.y - from.y };
        float len = delta.magnitude();
        if (len <= 0.001f) return;
        Vec2 mid { (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f };
        fillRotatedQuad(dc, mid, len, width, std::atan2(delta.y, delta.x), col);
    }

    void drawArrow(DrawUtil& dc, Vec2 const& center, float angle, float ringRadius, float arrowSize,
                   d2d::Color const& col) {
        if (!validPoint(center) || !std::isfinite(angle) || arrowSize <= 0.f) return;

        Vec2 direction { std::cos(angle), std::sin(angle) };
        Vec2 perpendicular { -direction.y, direction.x };

        float headHalfWidth = std::max(1.5f, arrowSize * 0.6f);
        Vec2 tip { center.x + direction.x * (ringRadius + arrowSize),
                   center.y + direction.y * (ringRadius + arrowSize) };
        Vec2 headBase { center.x + direction.x * ringRadius, center.y + direction.y * ringRadius };
        Vec2 headLeft { headBase.x + perpendicular.x * headHalfWidth,
                        headBase.y + perpendicular.y * headHalfWidth };
        Vec2 headRight { headBase.x - perpendicular.x * headHalfWidth,
                         headBase.y - perpendicular.y * headHalfWidth };
        dc.fillTriangle(tip, headLeft, headRight, col);

        float tailGap = arrowSize * 0.25f;
        float tailLength = arrowSize * 0.7f;
        float tailSpread = arrowSize * 0.55f;
        float tailWidth = std::max(1.5f, arrowSize * 0.22f);
        Vec2 apex { center.x + direction.x * (ringRadius - tailGap),
                    center.y + direction.y * (ringRadius - tailGap) };
        Vec2 tailBack { apex.x - direction.x * tailLength, apex.y - direction.y * tailLength };
        Vec2 tailLeft { tailBack.x + perpendicular.x * tailSpread, tailBack.y + perpendicular.y * tailSpread };
        Vec2 tailRight { tailBack.x - perpendicular.x * tailSpread, tailBack.y - perpendicular.y * tailSpread };
        fillThickLine(dc, apex, tailLeft, tailWidth, col);
        fillThickLine(dc, apex, tailRight, tailWidth, col);
    }
}

OutOfViewArrows::OutOfViewArrows()
    : Module("OutOfViewArrows", LocalizeString::get("client.module.outOfViewArrows.name"),
             LocalizeString::get("client.module.outOfViewArrows.desc"), GAME) {
    addSliderSetting("arrowSize", LocalizeString::get("client.module.outOfViewArrows.arrowSize.name"),
                     LocalizeString::get("client.module.outOfViewArrows.arrowSize.desc"), arrowSize, FloatValue(5.f),
                     FloatValue(40.f), FloatValue(1.f));
    addSliderSetting("space", LocalizeString::get("client.module.outOfViewArrows.space.name"),
                     LocalizeString::get("client.module.outOfViewArrows.space.desc"), space, FloatValue(10.f),
                     FloatValue(150.f), FloatValue(1.f));
    addSliderSetting("range", LocalizeString::get("client.module.outOfViewArrows.range.name"),
                     LocalizeString::get("client.module.outOfViewArrows.range.desc"), range, FloatValue(1.f),
                     FloatValue(50.f), FloatValue(1.f));
    addSetting("players", LocalizeString::get("client.module.outOfViewArrows.players.name"),
               LocalizeString::get("client.module.outOfViewArrows.players.desc"), players);
    addSetting("mobs", LocalizeString::get("client.module.outOfViewArrows.mobs.name"),
               LocalizeString::get("client.module.outOfViewArrows.mobs.desc"), mobs);
    addSetting("playerColor", LocalizeString::get("client.module.outOfViewArrows.playerColor.name"),
               LocalizeString::get("client.module.outOfViewArrows.playerColor.desc"), playerColor, "players"_istrue);
    addSetting("friendColor", LocalizeString::get("client.module.outOfViewArrows.friendColor.name"),
               LocalizeString::get("client.module.outOfViewArrows.friendColor.desc"), friendColor, "players"_istrue);
    addSetting("mobColor", LocalizeString::get("client.module.outOfViewArrows.mobColor.name"),
               LocalizeString::get("client.module.outOfViewArrows.mobColor.desc"), mobColor, "mobs"_istrue);

    Eventing::get().listen<RenderOverlayEvent, &OutOfViewArrows::onRenderOverlay>(this);
    Eventing::get().listen<RenderLayerEvent, &OutOfViewArrows::onRenderLayer>(this);
}

void OutOfViewArrows::onRenderLayer(RenderLayerEvent& event) {
    if (AntiObs::isActive()) return;
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;

    auto* screenView = event.getScreenView();
    if (!screenView || !screenView->visualTree || !screenView->visualTree->rootControl ||
        screenView->visualTree->rootControl->name != "debug_screen")
        return;

    MCDrawUtil dc { event.getUIRenderContext(), Necromancer::get().getFont() };
    dc.setImmediate(false);
    drawArrows(dc);
    dc.flush();
}

void OutOfViewArrows::onRenderOverlay(Event&) {
    if (!AntiObs::isActive()) return;
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;

    D2DUtil dc;
    drawArrows(dc);
}

void OutOfViewArrows::drawArrows(DrawUtil& dc) {
    bool doPlayers = std::get<BoolValue>(players);
    bool doMobs = std::get<BoolValue>(mobs);
    if (!doPlayers && !doMobs) return;

    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft) return;

    auto lp = ci->getLocalPlayer();
    if (!lp) return;

    auto guiData = ci->getGuiData();
    if (!guiData) return;

    bool useFrozen = !dc.isMinecraft();

    std::shared_ptr<const RenderFrameState::Frame> frame;
    WorldToScreen::ProjectionContext projectionContext;
    Vec3 eyePos;
    float playerYaw;

    if (useFrozen) {
        frame = RenderFrameState::get().latest();
        if (!frame || frame->entities.empty()) return;
        projectionContext = frame->projection;
        eyePos = frame->eyePos;
        playerYaw = frame->playerYaw;
    } else {
        auto live = WorldToScreen::createContext();
        if (!live) return;
        projectionContext = *live;
        eyePos = lp->getPos();
        if (ci->levelRenderer && ci->levelRenderer->getLevelRendererPlayer()) {
            eyePos = ci->levelRenderer->getLevelRendererPlayer()->getOrigin();
        }
        playerYaw = lp->getRot().y;
    }

    float alpha = ci->minecraft->timer ? ci->minecraft->timer->alpha : 1.f;

    Vec2 screenSize = guiData->screenSize;
    Vec2 center = { screenSize.x * 0.5f, screenSize.y * 0.5f };

    float maxRange = std::get<FloatValue>(range).value;
    float ringRadius = std::get<FloatValue>(space).value;
    float arrowLen = std::get<FloatValue>(arrowSize).value;

    d2d::Color playerCol(std::get<ColorValue>(playerColor).getMainColor());
    d2d::Color friendCol(std::get<ColorValue>(friendColor).getMainColor());
    d2d::Color mobCol(std::get<ColorValue>(mobColor).getMainColor());

    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp) continue;
        if (view.invisible) continue;

        bool isPlayer = view.isPlayer;
        bool isMob = view.kind == EntityCache::EntKind::Mob;
        if (isPlayer ? !doPlayers : (isMob ? !doMobs : true)) continue;

        Vec3 worldPos;
        if (useFrozen) {
            auto const* geo = frame->find(view.runtimeId);
            if (!geo) continue;
            worldPos = geo->interpolatedPos;
        } else {
            Vec3 const& pos = entt->getPos();
            Vec3 const& posOld = entt->getPosOld();
            worldPos = { std::lerp(posOld.x, pos.x, alpha), std::lerp(posOld.y, pos.y, alpha),
                         std::lerp(posOld.z, pos.z, alpha) };
        }

        float dist = eyePos.distance(worldPos);
        if (dist > maxRange) continue;

        float dirAngle;
        auto screenPos = WorldToScreen::convert(worldPos, projectionContext);
        bool onScreenNow = screenPos && screenPos->x >= 0.f && screenPos->x <= screenSize.x && screenPos->y >= 0.f &&
                           screenPos->y <= screenSize.y;
        if (onScreenNow) continue;

        if (screenPos) {
            Vec2 delta = { screenPos->x - center.x, screenPos->y - center.y };
            if (delta.magnitude() < 0.001f || !validPoint(delta)) continue;
            dirAngle = std::atan2(delta.y, delta.x);
        } else {
            float bearing =
                std::atan2(worldPos.z - eyePos.z, worldPos.x - eyePos.x) * (180.f / pi_f) - 90.f - playerYaw;
            while (bearing > 180.f) bearing -= 360.f;
            while (bearing < -180.f) bearing += 360.f;
            dirAngle = (bearing - 90.f) * (pi_f / 180.f);
        }

        d2d::Color col;
        if (isPlayer) {
            bool isFriend = false;
            if (auto* player = reinterpret_cast<SDK::Player*>(entt); player) {
                isFriend = PlayerListManager::get().isFriend(player->playerName);
            }
            col = isFriend ? friendCol : playerCol;
        } else {
            col = mobCol;
        }
        float halfRange = maxRange * 0.5f;
        if (dist > halfRange && maxRange > 0.001f) {
            float fade = std::clamp(1.f - ((dist - halfRange) / halfRange) * 0.75f, 0.25f, 1.f);
            col.a *= fade;
        }
        drawArrow(dc, center, dirAngle, ringRadius, arrowLen, col);
    }

    dc.flush();
}

#include "pch.h"
#include "Hitboxes.h"
#include "client/misc/EntityCache.h"
#include <util/DrawUtil3D.h>

Hitboxes::Hitboxes()
    : Module("Hitboxes", LocalizeString::get("client.module.hitboxes.legitName"),
             LocalizeString::get("client.module.hitboxes.desc"), GAME) {
    addSetting("transparent", LocalizeString::get("client.module.hitboxes.transparent.name"),
               LocalizeString::get("client.module.hitboxes.transparent.desc"), transparent);
    addSetting("boxColor", LocalizeString::get("client.module.hitboxes.boxColor.name"),
               LocalizeString::get("client.module.hitboxes.boxColor.desc"), boxColor);
    addSetting("showEyeLine", LocalizeString::get("client.module.hitboxes.showEyeLine.name"),
               LocalizeString::get("client.module.hitboxes.showEyeLine.desc"), this->showEyeLine);
    addSetting("eyeLine", LocalizeString::get("client.module.hitboxes.eyeLine.name"),
               LocalizeString::get("client.module.hitboxes.eyeLine.desc"), this->eyeColor, "showEyeLine"_istrue);
    addSetting("showLookingAt", LocalizeString::get("client.module.hitboxes.showLookingAt.name"),
               LocalizeString::get("client.module.hitboxes.showLookingAt.desc"), this->showLine);
    addSetting("lookingAt", LocalizeString::get("client.module.hitboxes.lookingAt.name"),
               LocalizeString::get("client.module.hitboxes.lookingAt.desc"), this->lineColor, "showLookingAt"_istrue);
    addSetting("items", LocalizeString::get("client.module.hitboxes.items.name"),
               LocalizeString::get("client.module.hitboxes.items.desc"), items);

    Eventing::get().listen<RenderLevelEvent, &Hitboxes::onRenderLevel>(this);
}

void Hitboxes::onRenderLevel(RenderLevelEvent& event) {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraft || !ci->minecraft->timer || !ci->levelRenderer || !SDK::ScreenContext::instance3d)
        return;

    auto level = ci->minecraft->getLevel();
    if (!level) return;

    auto lp = ci->getLocalPlayer();

    auto material = std::get<BoolValue>(transparent) ? SDK::MaterialPtr::getSelectionOverlayMaterial()
                                                     : SDK::MaterialPtr::getSelectionBoxMaterial();
    auto dc = MCDrawUtil3D(ci->levelRenderer, SDK::ScreenContext::instance3d, material);

    float alpha = ci->minecraft->timer->alpha;
    bool doItems = std::get<BoolValue>(items);
    bool doEyeLine = std::get<BoolValue>(showEyeLine);
    bool doLookLine = std::get<BoolValue>(showLine);
    auto boxCol = std::get<ColorValue>(boxColor).getMainColor();
    auto lineCol = std::get<ColorValue>(lineColor).getMainColor();
    auto eyeCol = std::get<ColorValue>(eyeColor).getMainColor();

    auto rak = SDK::RakNetConnector::get();
    bool onServer = rak && !rak->ipAddress.empty();

    auto snap = EntityCache::get().snapshot();
    for (auto const& view : snap->views) {
        SDK::Actor* entt = view.actor;
        if (!entt || entt == lp) continue;
        if (view.invisible) continue;
        if (!doItems && view.isItem) continue;

        Vec3 const& pos = entt->getPos();
        Vec3 const& posOld = entt->getPosOld();
        Vec3 newPos = { std::lerp(posOld.x, pos.x, alpha), std::lerp(posOld.y, pos.y, alpha),
                        std::lerp(posOld.z, pos.z, alpha) };

        AABB bb = entt->getBoundingBox();
        float eyeOffset = pos.y - bb.lower.y;
        Vec3 rebasePos =
            newPos.operator-({ 0.f, eyeOffset, 0.f }).operator+({ 0.f, (bb.higher.y - bb.lower.y) / 2.f, 0.f });
        bb.rebase(rebasePos);

        bool willShowLine = doLookLine && (!view.isPlayer || !onServer);

        dc.drawBox(bb, boxCol);
        float eyePos = newPos.y;
        float eyeLine = eyePos;
        bool customEyeLine = false;

        customEyeLine = NecromancerMath::aequals(bb.lower.y, eyePos);
        if (customEyeLine) {
            eyeLine = bb.lower.y + (bb.higher.y - bb.lower.y) * 0.85f;
        }

        if (doEyeLine) {
            dc.drawQuad(Vec3(bb.lower.x, eyeLine, bb.lower.z), Vec3(bb.higher.x, eyeLine, bb.lower.z),
                        Vec3(bb.higher.x, eyeLine, bb.higher.z), Vec3(bb.lower.x, eyeLine, bb.higher.z), eyeCol);
        }

        if (willShowLine) {
            float calcYaw = (entt->getRot().y + 90) * (pi_f / 180);
            float calcPitch = entt->getRot().x * -(pi_f / 180);

            Vec3 offset;
            offset.x = cos(calcYaw) * cos(calcPitch);
            offset.y = sin(calcPitch);
            offset.z = sin(calcYaw) * cos(calcPitch);

            Vec3 begin = newPos;
            begin.y = customEyeLine ? eyeLine : eyePos;
            Vec3 end = begin + offset;

            dc.drawLine(begin, end, lineCol);
        }
    }
    dc.flush();
}

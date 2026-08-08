#include "pch.h"
#include "DamageIndicator.h"
#include "client/misc/EntityCache.h"
#include <client/screen/ScreenManager.h>
#include <util/WorldToScreen.h>
#include <mc/common/world/actor/Actor.h>
#include <random>

DamageIndicator::DamageIndicator()
    : Module("DamageIndicator", LocalizeString::get("client.module.damageIndicator.name"),
             LocalizeString::get("client.module.damageIndicator.desc"), GAME, nokeybind) {
    addSliderSetting("size", LocalizeString::get("client.module.damageIndicator.size.name"),
                     LocalizeString::get("client.module.damageIndicator.size.desc"), textSize, FloatValue(8.f),
                     FloatValue(40.f), FloatValue(1.f));
    addSetting("color", LocalizeString::get("client.module.damageIndicator.color.name"),
               LocalizeString::get("client.module.damageIndicator.color.desc"), textColor);

    this->listen<AttackEvent>(&DamageIndicator::onAttack);
    this->listen<TickEvent>(&DamageIndicator::onTick);
    Eventing::get().listen<RenderLayerEvent, &DamageIndicator::onRenderLayer>(this);
}

void DamageIndicator::onAttack(Event& evG) {
    auto& ev = reinterpret_cast<AttackEvent&>(evG);
    auto* target = ev.getActor();
    if (!target) return;

    auto& entry = tracked[target->getRuntimeID()];
    entry.lastAttack = std::chrono::steady_clock::now();
    if (auto hp = target->getHealth()) {
        entry.lastHealth = *hp;
    }
}

void DamageIndicator::onTick(Event&) {
    auto now = std::chrono::steady_clock::now();
    auto& cache = EntityCache::get();

    for (auto it = tracked.begin(); it != tracked.end();) {
        SDK::Actor* actor = cache.findByRuntimeID(it->first);

        if (!actor || now - it->second.lastAttack > 2.5s) {
            it = tracked.erase(it);
            continue;
        }

        if (auto hp = actor->getHealth()) {
            float delta = it->second.lastHealth - *hp;
            if (delta > 0.05f) {
                spawnPopup(actor, delta);
            }
            it->second.lastHealth = *hp;
        }
        ++it;
    }
}

void DamageIndicator::spawnPopup(SDK::Actor* actor, float amount) {
    AABB bb = actor->getBoundingBox();
    Vec3 center = bb.getCenter();
    float height = bb.higher.y - bb.lower.y;

    static std::mt19937 rng { std::random_device {}() };
    std::uniform_real_distribution<float> dist(-0.35f, 0.35f);

    DamagePopup popup;
    popup.pos = { center.x + dist(rng), bb.lower.y + height * 0.8f, center.z + dist(rng) };
    popup.amount = amount;
    popup.spawnTime = std::chrono::steady_clock::now();
    popups.push_back(popup);
}

static std::wstring formatDamage(float amount) {
    float rounded = std::round(amount * 10.f) / 10.f;
    if (std::abs(rounded - std::round(rounded)) < 0.05f) {
        return std::to_wstring(static_cast<int>(std::lround(rounded)));
    }
    wchar_t buf[16];
    swprintf_s(buf, L"%.1f", rounded);
    return buf;
}

void DamageIndicator::onRenderLayer(RenderLayerEvent& event) {
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;
    if (popups.empty()) return;

    auto projectionContext = WorldToScreen::createContext();
    if (!projectionContext) return;

    MCDrawUtil dc { event.getUIRenderContext(), Necromancer::get().getFont() };

    float size = std::get<FloatValue>(textSize).value;
    d2d::Color baseCol = d2d::Color(std::get<ColorValue>(textColor).getMainColor());

    auto now = std::chrono::steady_clock::now();
    constexpr float lifetime = 0.9f;
    constexpr float riseSpeed = 0.9f;

    for (auto it = popups.begin(); it != popups.end();) {
        float age = std::chrono::duration<float>(now - it->spawnTime).count();
        if (age >= lifetime) {
            it = popups.erase(it);
            continue;
        }

        Vec3 pos = it->pos;
        pos.y += age * riseSpeed;

        auto screen = WorldToScreen::convert(pos, *projectionContext);
        if (screen) {
            float t = age / lifetime;
            float alpha = 1.f - t * t;

            std::wstring text = formatDamage(it->amount);
            Vec2 sz = dc.getTextSize(text, Renderer::FontSelection::PrimaryRegular, size, true, false);
            d2d::Rect rc { screen->x - sz.x * 0.5f, screen->y - sz.y * 0.5f, screen->x + sz.x * 0.5f,
                           screen->y + sz.y * 0.5f };

            d2d::Rect shadowRc { rc.left + 1.f, rc.top + 1.f, rc.right + 1.f, rc.bottom + 1.f };
            dc.drawText(shadowRc, text, d2d::Color(0.f, 0.f, 0.f, 0.55f * alpha),
                        Renderer::FontSelection::PrimaryRegular, size, DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

            d2d::Color col = baseCol;
            col.a *= alpha;
            dc.drawText(rc, text, col, Renderer::FontSelection::PrimaryRegular, size, DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }
        ++it;
    }

    dc.flush();
}

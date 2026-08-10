#include "pch.h"
#include "HUDEditor.h"
#include "client/event/events/RenderOverlayEvent.h"
#include "client/event/events/RenderLayerEvent.h"
#include "client/event/events/ClickEvent.h"

#include "client/event/Eventing.h"
#include "client/event/events/DrawHUDModulesEvent.h"
#include "client/Necromancer.h"
#include "client/input/Keyboard.h"
#include "client/config/ConfigManager.h"
#include "client/localization/LocalizeString.h"
#include "client/feature/module/ModuleManager.h"
#include "client/feature/module/HUDModule.h"
#include "../ScreenManager.h"

#include "mc/common/client/gui/controls/VisualTree.h"
#include "mc/common/client/gui/controls/UIControl.h"
#include <glm/gtc/matrix_transform.hpp>

HUDEditor::HUDEditor()
    : dragMod(nullptr) {
    this->key2 = Necromancer::get().getSecondaryMenuKey();

    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&HUDEditor::onRender, 2, true);
    Eventing::get().listen<RenderLayerEvent>(this, (EventListenerFunc)&HUDEditor::onRenderLayer, 1, true);
    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&HUDEditor::onClick, 4);
    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&HUDEditor::onKey);
}

void HUDEditor::onRender(Event& ev) {
    D2DUtil dc;
    bool mcRenderer = Necromancer::get().useMinecraftRenderer();

    std::vector<d2d::Rect> maskRects = {};

    if (isActive()) {
        Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
            if (mod->isHud() && mod->isEnabled()) {
                auto rMod = reinterpret_cast<HUDModule*>(mod.get());
                if (Necromancer::get().getMenuBlur() && (mcRenderer || rMod->forceMinecraftRenderer()))
                    maskRects.push_back(rMod->getRect());
                if (rMod->isActive()) return;
                addLayer(rMod->getRect());
            }
        });

        auto alpha = Necromancer::getRenderer().getDeltaTime() / 10.f;
        anim = std::lerp(anim, 1.f, alpha);

        float toBlur = Necromancer::get().getMenuBlur().value_or(0.f);
        if (Necromancer::get().getMenuBlur()) dc.drawGaussianBlur(toBlur * anim);
        // cut out stuff, for movable scoreboard and paperdoll in future

        for (auto& control : maskRects) {
            auto bmp = Necromancer::getRenderer().getCopiedBitmap(control);

            dc.ctx->DrawBitmap(bmp);

            bmp->Release();
        }
        Necromancer::getRenderer().getDeviceContext()->Flush();

        auto& cursorPos = SDK::ClientInstance::get()->cursorPos;

        // Center Button
        {
            float buttonWidth = 200.f;
            float buttonHeight = 60.f;

            auto ss = Necromancer::getRenderer().getScreenSize();

            d2d::Rect ssRec = { 0.f, 0.f, ss.width, ss.height };
            Vec2 btnPos = ssRec.center({ 200.f, 60.f });
            d2d::Rect btnRect = { btnPos, Vec2(btnPos.x + buttonWidth, btnPos.y + buttonHeight) };

            d2d::Color oCol = d2d::Color::RGB(0x7, 0x7, 0x7).asAlpha(0.75f);
            static d2d::Color col = oCol;
            d2d::Color outlineCol = d2d::Color::RGB(0, 0, 0).asAlpha(0.28f);

            bool state = shouldSelect(btnRect, cursorPos);
            if (state && justClicked[0]) {
                Necromancer::getScreenManager().exitCurrentScreen();
                Necromancer::getScreenManager().showScreen<ClickGUI>();
                playClickSound();
            }

            col = util::LerpColorState(col, oCol + 0.1f, oCol, state);
            dc.fillRoundedRectangle(btnRect, col, 20.f);
            dc.drawRoundedRectangle(btnRect, outlineCol, 20.f, 2.f, DrawUtil::OutlinePosition::Outside);

            dc.drawText(btnRect, LocalizeString::get("client.ui.hudEditor.modSettings.name"),
                        d2d::Color(0.9f, 0.9f, 0.9f, 1.f), Renderer::FontSelection::PrimaryRegular, 20.f,
                        DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        clearLayers();
    }

    if (isActive()) doDragging();
    doSnapping(dragOffset);
    if (!mcRenderer) {
        renderModules(nullptr);
        keepModulesInBounds(
            Vec2(Necromancer::getRenderer().getScreenSize().width, Necromancer::getRenderer().getScreenSize().height));
    } else {
        keepModulesInBounds(SDK::ClientInstance::get()->getGuiData()->screenSize);
    }
}

void HUDEditor::onClick(Event& evGeneric) {
    auto& ev = reinterpret_cast<ClickEvent&>(evGeneric);

    if (ev.getMouseButton() == 4) {
        ev.setCancelled();
    }

    Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (!mod->isHud()) return;
        auto hudMod = reinterpret_cast<HUDModule*>(mod.get());

        if (!hudMod->isActive()) return;

        if (!shouldSelect(hudMod->getRect(), SDK::ClientInstance::get()->cursorPos)) return;
        ev.setCancelled(true);

        if (ev.getMouseButton() == 4) {
            if (!hudMod->isResizable()) return;
            hudMod->setScale(std::clamp(hudMod->getScale() - static_cast<float>(ev.getWheelDelta()) / 1000.f,
                                        HUDModule::min_scale, HUDModule::max_scale));
        } else if (ev.getMouseButton() == 3) {
            hudMod->setEnabled(false);
        } else if (ev.getMouseButton() == 2) {
            Necromancer::getScreenManager().get<ClickGUI>().jumpToModule(hudMod->name());
            close();
            Necromancer::getScreenManager().showScreen<ClickGUI>(true);
        } else {
            ev.setCancelled(false);
        }
    });
}

void HUDEditor::onRenderLayer(Event& evGeneric) {
    auto& ev = static_cast<RenderLayerEvent&>(evGeneric);
    bool mcRenderer = Necromancer::get().useMinecraftRenderer();

    if (ev.getScreenView()->visualTree->rootControl->name == "debug_screen") {
        if (!mcRenderer) {
            this->renderModules(ev.getUIRenderContext(), true);
            return;
        }

        MCDrawUtil dc = { ev.getUIRenderContext(), Necromancer::get().getFont() };

        // if (isActive()) dc.fillRectangle({ 0.f, 0.f, ss.x, ss.y }, { 0.4f, 0.4f, 0.4f, 0.4f * this->anim });
        dc.setImmediate(false);

        this->renderModules(ev.getUIRenderContext());
        keepModulesInBounds(SDK::ClientInstance::get()->getGuiData()->screenSize);
    }

    if (!SDK::ClientInstance::get()->minecraftGame->isCursorGrabbed()) {
        Necromancer::getModuleManager().forEach([](std::shared_ptr<Module> mod) {
            if (mod->isEnabled() && mod->shouldHoldToToggle()) {
                mod->setEnabled(false);
            }
        });
    }

    // auto view = ev.getScreenView();

    // float uiscale = sdk::ClientInstance::get()->getGuiData()->guiScale;

    // auto root = view->visualTree->rootControl;
    // if (root && root->name == "hud_screen") {
    // controls.clear();
    //  TODO: xor string
    // auto hotbarRend = root->findFirstDescendantWithName("hotbar_renderer");
    // auto paperdoll = root->findFirstDescendantWithName("hud_player");
    // auto playerPos = root->findFirstDescendantWithName("player_position");

    // if (hotbarRend) {
    //	d2d::Rect rec = hotbarRend->getRect();
    //	//rec.bottom = 0.f;
    //	rec.right = rec.left + (rec.getWidth() * 9.f);
    //	rec.right += 1.f;
    //	rec.left -= 1.f;
    //
    //	rec.left *= uiscale;
    //	rec.right *= uiscale;
    //	rec.top *= uiscale;
    //	rec.bottom *= uiscale;
    //	controls.push_back(rec);
    // }

    /*if (paperdoll) {
        d2d::Rect rec = paperdoll->getRect();
        rec.bottom += rec.getHeight();
        //rec.bottom = 0.f;
        if (((sdk::CustomRenderComponent*)paperdoll->uiComponents[4])->rend->timeToClose > 0.05f) {

            rec.left *= uiscale;
            rec.right *= uiscale;
            rec.top *= uiscale;
            rec.bottom *= uiscale;
            controls.push_back(rec);
        }
    }

    if (playerPos) {
        d2d::Rect rec = playerPos->getRect();
        //rec.bottom = 0.f;
        rec.right = rec.left + (rec.getWidth());

        rec.left *= uiscale;
        rec.right *= uiscale;
        rec.top *= uiscale;
        rec.bottom *= uiscale;
        controls.push_back(rec);
    }*/
    //}
}

void HUDEditor::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    if (ev.getKey() == VK_F11) return;
    if (ev.getKey() == VK_ESCAPE) close();

    if (Necromancer::getKeyboard().isMovementKey(ev.getKey())) return;

    ev.setCancelled();
}

void HUDEditor::renderModules(SDK::MinecraftUIRenderContext* ctx, bool forceMinecraftOnly) {
    if (!isActive()) {
        DrawHUDModulesEvent ev {};
        if (Eventing::get().dispatch(ev)) return; // if cancelled
    }

    auto guiData = SDK::ClientInstance::get()->getGuiData();
    if (!guiData) return;

    Vec2 screenSize = guiData->screenSize;
    if (screenSize.x <= 0.f || screenSize.y <= 0.f) return;

    if (!lastScreenSize) {
        lastScreenSize = screenSize;
    } else {
        if (*lastScreenSize != screenSize && lastScreenSize->x > 0.f && lastScreenSize->y > 0.f) {
            Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
                if (mod->isHud()) {
                    HUDModule* rMod = reinterpret_cast<HUDModule*>(mod.get());
                    Vec2 oPos = rMod->getRect().getPos();
                    Vec2 oPercent = oPos / *lastScreenSize;
                    Vec2 new_ = screenSize * oPercent;
                    rMod->setPos(new_);
                }
            });
        }
    }

    lastScreenSize = screenSize;

    if (isActive() || SDK::ClientInstance::get()->minecraftGame->isCursorGrabbed()) {
        Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
            if (!Necromancer::get().useMinecraftRenderer()) {
                if ((forceMinecraftOnly || Necromancer::get().useMinecraftRenderer()) ^
                    static_cast<Module*>(mod.get())->forceMinecraftRenderer())
                    return;
            }
            if (mod->isHud() && mod->isEnabled() && reinterpret_cast<HUDModule*>(mod.get())->isActive()) {
                auto hudModule = static_cast<HUDModule*>(mod.get());
                renderModule(hudModule, ctx);
                hudModule->storePos(ctx ? screenSize
                                        : Vec2(Necromancer::getRenderer().getScreenSize().width,
                                               Necromancer::getRenderer().getScreenSize().height));
            }
        });
    }
}

void HUDEditor::renderModule(HUDModule* mod, SDK::MinecraftUIRenderContext* ctx) {
    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;
    bool hovering = shouldSelect(mod->getRect(), cursorPos);

    if (!ctx) {
        D2DUtil dc;
        if (!dc.ctx) return;
        D2D1::Matrix3x2F oTrans;
        if (isActive()) mod->renderFrame(dc);
        dc.ctx->GetTransform(&oTrans);
        dc.ctx->SetTransform(D2D1::Matrix3x2F::Scale(mod->getScale(), mod->getScale()) *
                             D2D1::Matrix3x2F::Translation(mod->getRect().left, mod->getRect().top));
        mod->render(dc, false, isActive());
        dc.ctx->SetTransform(oTrans);
    } else {
        MCDrawUtil dc { ctx, Necromancer::get().getFont() };
        if (!dc.scn || !dc.scn->matrix) return;
        if (isActive()) mod->renderFrame(dc);
        dc.setImmediate(false);
        dc.flush();
        glm::mat4 transform { 1.f };
        transform = glm::translate(transform,
                                   glm::vec3(mod->getRect().left * dc.guiScale, mod->getRect().top * dc.guiScale, 0.f));
        transform = glm::scale(transform, glm::vec3(mod->getScale(), mod->getScale(), 1.f));
        dc.scn->matrix->matrixStack.push(transform);
        mod->render(dc, false, isActive());
        dc.flush();
        dc.scn->matrix->matrixStack.pop();
    }

    if (isActive()) {
        if (ctx) {
            MCDrawUtil dc { ctx, Necromancer::get().getFont() };
            if (hovering) mod->renderSelected(dc);
            mod->renderPost(dc);
            dc.flush();
        } else {
            D2DUtil dc;
            if (!dc.ctx) return;
            if (hovering) mod->renderSelected(dc);
            mod->renderPost(dc);
        }
    }
}

void HUDEditor::doDragging() {
    auto& cursorPos = SDK::ClientInstance::get()->cursorPos;

    bool isDown = mouseButtons[0];
    if (dragMod) {
        if (!isDown) {
            dragMod = nullptr;
        } else {
            dragMod->setPos(cursorPos - dragOffset);
        }
    } else {
        // Find a dragging element
        if (isDown) {
            bool doDrag = true;
            Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
                if (doDrag) {
                    if (mod->isEnabled() && mod->isHud()) {
                        HUDModule* rMod = static_cast<HUDModule*>(mod.get());
                        if (!rMod->isActive()) return;
                        if (shouldSelect(rMod->getRect(), cursorPos)) {
                            dragMod = rMod;
                            Vec2 pos = rMod->getRect().getPos();
                            dragOffset = cursorPos - pos;
                            doDrag = false;
                        }
                    }
                }
            });
        }
    }
}

void HUDEditor::doSnapping(Vec2 const&) {
    auto ssx = Necromancer::getRenderer().getScreenSize();
    Vec2 ss = { ssx.width, ssx.height };
    auto& mousePos = SDK::ClientInstance::get()->cursorPos;

    std::vector<float> snapLinesX = { 0.f, ss.x / 4.f, ss.x / 2.f, ss.x / 2 + (ss.x / 4), ss.x };
    std::vector<float> snapLinesY = { ss.y / 2.f };

    std::vector<std::pair<float, float>> snapLinesControlsX = {};
    std::vector<float> snapLinesControlsY = {};

    // Be able to snap to the minecraft ui (like hotbar)

    for (auto& rec : controls) {
        if (rec.left > 0.f && rec.bottom < ss.y) snapLinesControlsX.emplace_back(rec.left, rec.top);
        if (rec.right > 0.f && rec.bottom < ss.y) snapLinesControlsX.emplace_back(rec.right, rec.top);
        if (rec.top > 0.f && rec.bottom < ss.y) snapLinesControlsY.push_back(rec.top);
        if (rec.bottom > 0.f && rec.bottom < ss.y) snapLinesControlsY.push_back(rec.bottom);
    }

    if (isActive() && dragMod && Necromancer::get().getDoSnapLines()) {
        auto pos = mousePos - dragOffset;

        float snapRange = 10.f;

        D2DUtil dc;

        Color col = d2d::Color(0.5, 1.0, 1.0);
        float thickness = 1.f;

        dragMod->snappingX = SnapValue();
        dragMod->snappingY = SnapValue();

        auto& snapX = std::get<SnapValue>(dragMod->snappingX);
        auto& snapY = std::get<SnapValue>(dragMod->snappingY);

        for (int i = 0; i < snapLinesX.size(); i++) {
            float snap = snapLinesX[i];
            SnapLine pred(dragMod, snap, false);
            pred.color = col;
            pred.thickness = thickness;

            d2d::Color predColor = pred.color;
            float predThickness = pred.thickness;

            if (std::abs(pos.x - pred.right) < snapRange) {
                snapX.snap(SnapValue::Normal, SnapValue::Right, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ snap, 0 }, { snap, ss.y }, dc.brush, predThickness);
                dragMod->setPos({ snap, dragMod->getRect().getPos().y });
            }

            if (std::abs(pos.x - pred.middle) < snapRange) {
                snapX.snap(SnapValue::Normal, SnapValue::Middle, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ snap, 0 }, { snap, ss.y }, dc.brush, predThickness);
                dragMod->setPos({ pred.middle, dragMod->getRect().getPos().y });
            }

            if (std::abs(pos.x - pred.left) < snapRange) {
                snapX.snap(SnapValue::Normal, SnapValue::Left, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ snap, 0 }, { snap, ss.y }, dc.brush, predThickness);
                dragMod->setPos({ pred.left, dragMod->getRect().getPos().y });
            }
        }

        for (int i = 0; i < snapLinesY.size(); i++) {
            float snap = snapLinesY[i];
            SnapLine pred(dragMod, snap, true);
            pred.color = col;
            pred.thickness = thickness;

            d2d::Color predColor = pred.color;
            float predThickness = pred.thickness;

            if (std::abs(pos.y - pred.right) < snapRange) {
                snapY.snap(SnapValue::Normal, SnapValue::Right, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ 0.f, snap }, { ss.x, snap }, dc.brush, predThickness);
                dragMod->setPos({ dragMod->getRect().getPos().x, snap });
            }

            if (std::abs(pos.y - pred.middle) < snapRange) {
                snapY.snap(SnapValue::Normal, SnapValue::Middle, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ 0.f, snap }, { ss.x, snap }, dc.brush, predThickness);
                dragMod->setPos({ dragMod->getRect().getPos().x, pred.middle });
            }

            if (std::abs(pos.y - pred.left) < snapRange) {
                snapY.snap(SnapValue::Normal, SnapValue::Left, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ 0.f, snap }, { ss.x, snap }, dc.brush, predThickness);
                dragMod->setPos({ dragMod->getRect().getPos().x, pred.left });
            }
        }

        for (int i = 0; i < snapLinesControlsX.size(); i++) {
            auto& snaps = snapLinesControlsX[i];
            float snap = snaps.first;
            if (std::abs(pos.y - snaps.second) > 100.f) {
                continue;
            }

            SnapLine pred(dragMod, snap, false);
            pred.color = d2d::Colors::YELLOW;
            pred.thickness = 0.5f;

            d2d::Color predColor = pred.color;
            float predThickness = pred.thickness;

            if (std::abs(pos.x - pred.right) < 5.f) {
                snapX.snap(SnapValue::MCUI, SnapValue::Right, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ snap, 0 }, { snap, ss.y }, dc.brush, predThickness);
                dragMod->setPos({ snap, dragMod->getRect().getPos().y });
            }

            if (std::abs(pos.x - pred.middle) < 5.f) {
                snapX.snap(SnapValue::MCUI, SnapValue::Middle, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ snap, 0 }, { snap, ss.y }, dc.brush, predThickness);
                dragMod->setPos({ pred.middle, dragMod->getRect().getPos().y });
            }

            if (std::abs(pos.x - pred.left) < 5.f) {
                snapX.snap(SnapValue::MCUI, SnapValue::Left, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ snap, 0 }, { snap, ss.y }, dc.brush, predThickness);
                dragMod->setPos({ pred.left, dragMod->getRect().getPos().y });
            }
        }

        for (int i = 0; i < snapLinesControlsY.size(); i++) {
            auto snap = snapLinesControlsY[i];
            SnapLine pred(dragMod, snap, true);
            pred.color = d2d::Colors::YELLOW;
            pred.thickness = 0.5f;

            d2d::Color predColor = pred.color;
            float predThickness = pred.thickness;

            if (std::abs(pos.y - pred.right) < 5.f) {
                snapY.snap(SnapValue::MCUI, SnapValue::Right, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ 0.f, snap }, { ss.x, snap }, dc.brush, predThickness);
                dragMod->setPos({ dragMod->getRect().getPos().x, snap });
            }

            if (std::abs(pos.y - pred.middle) < 5.f) {
                snapY.snap(SnapValue::MCUI, SnapValue::Middle, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ 0.f, snap }, { ss.x, snap }, dc.brush, predThickness);
                dragMod->setPos({ dragMod->getRect().getPos().x, pred.middle });
            }

            if (std::abs(pos.y - pred.left) < 5.f) {
                snapY.snap(SnapValue::MCUI, SnapValue::Left, i);
                dc.brush->SetColor(predColor.get());
                dc.ctx->DrawLine({ 0.f, snap }, { ss.x, snap }, dc.brush, predThickness);
                dragMod->setPos({ dragMod->getRect().getPos().x, pred.left });
            }
        }
    } else {
        // Keep modules in their snapped state
        Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
            if (mod->isHud() && mod->isEnabled()) {
                auto rMod = static_cast<HUDModule*>(mod.get());
                if (!rMod->isActive()) return;
                auto& snapX = std::get<SnapValue>(rMod->snappingX);
                auto& snapY = std::get<SnapValue>(rMod->snappingY);
                auto pos = rMod->getRect().getPos();
                if (snapX.doSnapping) {
                    if (snapX.type != SnapValue::Module) {
                        auto type = snapX.type;
                        using Snapping = SnapValue;
                        auto place = snapX.position;
                        auto idx = snapX.index;

                        auto idk = snapLinesX[idx];

                        // TODO: controls

                        if (type == Snapping::MCUI && snapLinesControlsX.size() > 0) {
                            idk = snapLinesControlsX[idx].first;
                        }

                        SnapLine snap(rMod, idk, false);
                        switch (place) {
                        case Snapping::Left:
                            rMod->setPos({ snap.left, pos.y });
                            break;
                        case Snapping::Middle:
                            rMod->setPos({ snap.middle, pos.y });
                            break;
                        case Snapping::Right:
                            rMod->setPos({ snap.right, pos.y });
                            break;
                        default:
                            throw std::runtime_error("invalid snapping");
                            break;
                        }
                    }
                }
                pos = rMod->getRect().getPos();
                if (snapY.doSnapping) {
                    auto type = snapY.type;
                    if (snapY.type != SnapValue::Module) {
                        using Snapping = SnapValue;
                        auto place = snapY.position;
                        auto idx = snapY.index;

                        auto idk = snapLinesY[idx];

                        // TODO: controls

                        if (type == Snapping::MCUI && snapLinesControlsY.size() > 0) {
                            idk = snapLinesControlsY[idx];
                        }

                        SnapLine snap(rMod, idk, true);
                        switch (place) {
                        case Snapping::Left:
                            rMod->setPos({ pos.x, snap.left });
                            break;
                        case Snapping::Middle:
                            rMod->setPos({ pos.x, snap.middle });
                            break;
                        case Snapping::Right:
                            rMod->setPos({ pos.x, snap.right });
                            break;
                        default:
                            throw std::runtime_error("invalid snapping");
                            break;
                        }
                    }
                }
            }
        });
    }
}

void HUDEditor::keepModulesInBounds(Vec2 const& ss) {
    Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (mod->isEnabled() && mod->isHud()) {
            HUDModule* rMod = static_cast<HUDModule*>(mod.get());
            if (!rMod->isActive()) return false;
            d2d::Rect rc = rMod->getRect();
            Vec2 modPos = rc.getPos();

            if (rc.left < 0) {
                rMod->setPos({ 0.f, modPos.y });
            }

            if (rc.top < 0) {
                rMod->setPos({ modPos.x, 0.f });
            }

            if (rc.right > ss.x) {
                rMod->setPos({ ss.x - rc.getWidth(), modPos.y });
            }

            if (rc.bottom > ss.y) {
                rMod->setPos({ modPos.x, ss.y - rc.getHeight() });
            }

            auto round2 = [](float& f) {
                f = std::round(f);
            };

            // auto oPos = rc.getPos();
            // round2(rc.left);
            // round2(rc.top);
            // round2(rc.right);
            // round2(rc.bottom);
            // rMod->setRect(rc);
        }
        return false;
    });
}

void HUDEditor::onEnable(bool ignoreAnims) {
    if (ignoreAnims)
        anim = 1.f;
    else
        anim = 0.f;
    mouseButtons = {};
    activeMouseButtons = {};
    justClicked = {};
}

void HUDEditor::onDisable() {
    Necromancer::getConfigManager().saveCurrentConfig();
}

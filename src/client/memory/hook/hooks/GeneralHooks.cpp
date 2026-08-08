#include "pch.h"
#include "GeneralHooks.h"
#include "client/Necromancer.h"
#include "client/misc/ClientMessageQueue.h"
#include "client/feature/command/commandmanager.h"
#include "client/event/Eventing.h"
#include "../Hooks.h"
#include "PlayerHooks.h"
#include "client/event/events/MouseReleaseEvent.h"
#include "client/screen/ScreenManager.h"
#include "mc/common/client/game/GameCore.h"
#include "mc/common/client/game/MouseDevice.h"
#include "mc/common/client/input/ClientInputHandler.h"
#include "mc/common/client/player/LocalPlayer.h"

namespace {
    std::shared_ptr<Hook> MultiPlayerLevel__subTickHook;
    std::shared_ptr<Hook> ChatScreenController_sendChatMesageHook;
    std::shared_ptr<Hook> GameRenderer_renderCurrentFrameHook;
    // std::shared_ptr<Hook> Keyboard_feedHook;
    std::shared_ptr<Hook> MainWindow__windowProcCallbackHook;
    std::shared_ptr<Hook> GameCore_handleMouseInputHook;
    std::shared_ptr<Hook> LoadLibraryWHook;
    std::shared_ptr<Hook> LoadLibraryAHook;
    std::shared_ptr<Hook> AveragePingHook;
    std::shared_ptr<Hook> TurnDeltaHook;
    std::shared_ptr<Hook> ClientInputUpdateSystem_tickBaseInputHook;
    std::shared_ptr<Hook> ViewBobHook;
    std::shared_ptr<Hook> Level_initializeHook;
    std::shared_ptr<Hook> Level_startLeaveGameHook;
    std::shared_ptr<Hook> RenderEntityHook;
    std::shared_ptr<Hook> OutlineSelectionHook;
    std::shared_ptr<Hook> RenderGuiItemNewHook;
    std::shared_ptr<Hook> GetTimeOfDayHook;
    std::shared_ptr<Hook> DimensionHook;
    std::shared_ptr<Hook> FogColorHook;
    std::shared_ptr<Hook> AddMessageHook;
    std::shared_ptr<Hook> UpdatePlayerHook;
    std::shared_ptr<Hook> OnUriHook;
    std::shared_ptr<Hook> GrabCursorHook;
    std::shared_ptr<Hook> BaseActorRenderer_renderTextHook;
    std::shared_ptr<Hook> AppPlatformGDK_releaseMouseHook;
    std::shared_ptr<Hook> ForwardSoundSubtitleHook;

    int mouseButtonToVk(int button) {
        switch (button) {
        case 1:
            return 0x01;
        case 2:
            return 0x02;
        case 3:
            return 0x04;
        case 5:
            return 0x05;
        case 6:
            return 0x06;
        default:
            return 0;
        }
    }
}

void GenericHooks::MultiPlayerLevel__subTick(SDK::Level* level) {
    TickEvent ev(level);
    Necromancer::getEventing().dispatch(ev);
    Necromancer::getClientMessageQueue().doPrint(100);

    MultiPlayerLevel__subTickHook->oFunc<decltype(&MultiPlayerLevel__subTick)>()(level);
}

void* GenericHooks::ChatScreenController_sendChatMessage(void* controller, std::string& message) {
    if (message.starts_with(Necromancer::getCommandManager().prefix)) {
        Necromancer::getCommandManager().runCommand(message);
        return 0;
    }

    ChatEvent ev { message };
    if (Eventing::get().dispatch(ev)) return nullptr;

    return ChatScreenController_sendChatMesageHook->oFunc<decltype(&ChatScreenController_sendChatMessage)>()(controller,
                                                                                                             message);
}

void* GenericHooks::GameRenderer_renderCurrentFrame(void* rend) {
    // this causes the jitter bug
    //{
    //	RenderGameEvent ev{};
    //	Eventing::get().dispatchEvent(ev);
    //}

    return GameRenderer_renderCurrentFrameHook->oFunc<decltype(&GameRenderer_renderCurrentFrame)>()(rend);
}

LRESULT GenericHooks::MainWindow__windowProcCallback(HWND hwnd, UINT msg, WPARAM wParam,
                                                     LPARAM lParam) { // Name from China
    if (msg == WM_SETCURSOR) {
        std::optional<std::reference_wrapper<Screen>> activeScreen = Necromancer::get().getScreenManager().getActiveScreen();
        SDK::GameCore* gameCore = SDK::GameCore::get();
        constexpr uintptr_t mouseGrabbedOffset = 0x778; // GameCore::mMouseGrabbed, set by GDK grab/release mouse.
        const bool gameMouseGrabbed =
            gameCore && *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(gameCore) + mouseGrabbedOffset);

        if (!activeScreen && gameMouseGrabbed) {
            SetCursor(nullptr);
            return TRUE;
        }
    }

    if (msg == WM_MOUSEWHEEL) {
        MouseWheelEvent ev { static_cast<int>(static_cast<short>(HIWORD(wParam))) };
        if (Eventing::get().dispatch(ev)) return TRUE;
    }

    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
        const int key = wParam & 0xFF;
        const bool isDown = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;

        KeyUpdateEvent ev { key, isDown };
        if (Eventing::get().dispatch(ev)) return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
        const bool isDown = msg == WM_XBUTTONDOWN;
        const int xbtn = HIWORD(wParam);
        KeyUpdateEvent keyEv { xbtn == XBUTTON1 ? 0x05 : 0x06, isDown };
        Eventing::get().dispatch(keyEv);
        ClickEvent ev { xbtn == XBUTTON1 ? 5 : 6, static_cast<char>(isDown ? 1 : 0) };
        if (Eventing::get().dispatch(ev)) return TRUE;
    }

    return MainWindow__windowProcCallbackHook->oFunc<decltype(&MainWindow__windowProcCallback)>()(hwnd, msg, wParam,
                                                                                                  lParam);
}

bool GenericHooks::GameCore_handleMouseInput(void* a1, void* a2, void* a3) { // Made up name
    const auto res = GameCore_handleMouseInputHook->oFunc<decltype(&GameCore_handleMouseInput)>()(a1, a2, a3);

    const auto mouse = SDK::MouseDevice::get();
    if (!mouse) return res;

    for (auto it = mouse->inputs.begin(); it != mouse->inputs.end();) {
        if (const int vk = mouseButtonToVk(it->action)) {
            KeyUpdateEvent keyEv { vk, it->data != 0 };
            Eventing::get().dispatch(keyEv);
        }

        ClickEvent ev { it->action, static_cast<char>(it->data) };
        if (Eventing::get().dispatch(ev)) {
            it = mouse->inputs.erase(it);
        } else {
            ++it;
        }
    }

    return res;
}

BOOL __stdcall GenericHooks::hkLoadLibraryW(LPCWSTR lib) {
    // prevent double injections
#ifdef NECROMANCER_BETA
    abort();
#endif
    return 0;
}

int __fastcall GenericHooks::RakPeer_getAveragePing(void* obj, char* guidOrAddy) {
    int ping = AveragePingHook->oFunc<decltype(&RakPeer_getAveragePing)>()(obj, guidOrAddy);
    AveragePingEvent ev { ping };
    Eventing::get().dispatch(ev);
    return ping;
}

void __fastcall GenericHooks::LocalPlayer_applyTurnDelta(void* obj, Vec2& rot) {
    float oSens = 1.f;
    SensitivityEvent sensEv { oSens };
    Eventing::get().dispatch(sensEv);

    rot.x *= oSens;
    rot.y *= oSens;

    TurnDeltaEvent turnEvent { reinterpret_cast<SDK::LocalPlayer*>(obj), rot };
    Eventing::get().dispatch(turnEvent);

    CinematicCameraEvent ev { false };
    Eventing::get().dispatch(ev);

    float intensity = 60.f;
    static std::chrono::time_point lastTime = std::chrono::system_clock::now();
    std::chrono::time_point curTime = std::chrono::system_clock::now();

    auto diff = curTime - lastTime;
    lastTime = curTime;
    float mult = (float)(std::chrono::duration<double, std::milli>(diff).count() / 13.f); // 60ish FPS
    float integ = intensity / mult;

    static Vec2 lerpRot = { 0, 0 };
    if (ev.getValue()) {
        TurnDeltaHook->oFunc<decltype(&LocalPlayer_applyTurnDelta)>()(obj, lerpRot);
        Vec2 extract = rot / (Vec2(integ, integ));
        Vec2 integExtract = lerpRot / (Vec2(integ, integ));
        lerpRot = lerpRot - (integExtract);
        lerpRot = lerpRot + (extract);
        return;
    }
    lerpRot = { 0, 0 };
    TurnDeltaHook->oFunc<decltype(&LocalPlayer_applyTurnDelta)>()(obj, rot);
}

void GenericHooks::ClientInputUpdateSystemInternal_tickUpdateClientInput(uintptr_t** a1, void* a2, uintptr_t* a3,
                                                                         uintptr_t a4, uintptr_t a5, uintptr_t a6,
                                                                         uintptr_t a7, uintptr_t a8, uintptr_t a9,
                                                                         uintptr_t a10, uintptr_t a11, uintptr_t a12,
                                                                         uintptr_t a13, uintptr_t a14, uintptr_t a15,
                                                                         uintptr_t a16, uintptr_t a17, uintptr_t a18) {
    SDK::MoveInputComponent* hand = SDK::ClientInstance::get()->getLocalPlayer()->getMoveInputComponent();
    {
        BeforeMoveEvent ev { hand };
        if (Eventing::get().dispatch(ev)) return;
    }

    // Allow WASD movement while the ClickGUI menu is open. Two things normally
    // stop it: (1) ClickGUI cancels key events so they never reach the game, and
    // (2) opening the menu releases the cursor, which puts the game in "menu mode"
    // where it ignores movement input. (1) is handled in ClickGUI::onKey (movement
    // keys are allowed through). Here we handle (2): for the duration of the input
    // tick only, pretend the cursor is still grabbed so the game computes movement
    // 100% natively (correct strafe, crouch slowdown, sprint, etc). We restore the
    // real value immediately after, so the cursor stays free for the menu UI.
    auto client = SDK::ClientInstance::get();
    bool spoofedGrab = false;
    bool prevGrab = false;
    if (Necromancer::get().getScreenManager().getActiveScreen() && client && client->minecraftGame) {
        prevGrab = client->minecraftGame->isCursorGrabbed();
        if (!prevGrab) {
            client->minecraftGame->setCursorGrabbedRaw(true);
            spoofedGrab = true;
        }
    }

    ClientInputUpdateSystem_tickBaseInputHook
        ->oFunc<decltype(&ClientInputUpdateSystemInternal_tickUpdateClientInput)>()(
            a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18);

    if (spoofedGrab && client && client->minecraftGame) {
        client->minecraftGame->setCursorGrabbedRaw(prevGrab);
    }

    {
        AfterMoveEvent ev { hand };
        Eventing::get().dispatch(ev);
    }
}

void __fastcall GenericHooks::CameraViewBob(void* a, void* b, void* c) {
    BobMovementEvent ev {};
    if (Eventing::get().dispatch(ev)) {
        return;
    }

    return ViewBobHook->oFunc<decltype(&CameraViewBob)>()(a, b, c);
}

bool GenericHooks::Level_initialize(SDK::Level* obj, void* palette, void* settings, void* tickRange, void* experiments,
                                    uint64_t a6) {
    auto o =
        Level_initializeHook->oFunc<decltype(&Level_initialize)>()(obj, palette, settings, tickRange, experiments, a6);
    return o;
}

void* GenericHooks::Level_startLeaveGame(SDK::Level* obj) {
    LeaveGameEvent ev {};
    Eventing::get().dispatch(ev);

    return Level_startLeaveGameHook->oFunc<decltype(&Level_startLeaveGame)>()(obj);
}

void* GenericHooks::ActorRenderDispatcher_render(void* obj, SDK::BaseActorRenderContext* barc, SDK::Actor* entity,
                                                 Vec3& cameraTargetPos, Vec3 const& pos, const Vec2& rot,
                                                 bool affectedByLighting) {
    if (entity) {
        AfterRenderEntityEvent ev { entity, cameraTargetPos };
        Eventing::get().dispatch(ev);
    }

    return RenderEntityHook->oFunc<decltype(&ActorRenderDispatcher_render)>()(obj, barc, entity, cameraTargetPos, pos,
                                                                              rot, affectedByLighting);
}

void GenericHooks::LevelRendererPlayer_renderOutlineSelection(SDK::LevelRendererPlayer* obj, SDK::ScreenContext* scn,
                                                              void* block, void* region, BlockPos pos) {
    OutlineSelectionEvent ev { pos };
    if (!Eventing::get().dispatch(ev)) {
        OutlineSelectionHook->oFunc<decltype(&LevelRendererPlayer_renderOutlineSelection)>()(obj, scn, block, region,
                                                                                             pos);
    }
}

void* GenericHooks::hkRenderGuiItemNew(void* obj, SDK::BaseActorRenderContext* baseActorRenderContext,
                                       SDK::ItemStack* itemStack, int mode, float x, float y, float opacity,
                                       float scale, float a9, bool ench, int unk) {
    auto retAddy = (void*)(Signatures::ItemRenderer_renderGuiItemNew.scan_result + 5); // JMP + jump location
    if (_ReturnAddress() == retAddy) {
        RenderGuiItemEvent ev { itemStack };
        if (Eventing::get().dispatch(ev)) {
            // cancelled
            return nullptr;
        }
    }
    return RenderGuiItemNewHook->oFunc<decltype(&hkRenderGuiItemNew)>()(obj, baseActorRenderContext, itemStack, mode, x,
                                                                        y, opacity, scale, a9, ench, unk);
}

float GenericHooks::hkGetTimeOfDay(SDK::Dimension* obj, int time, float a) {
    auto o = GetTimeOfDayHook->oFunc<decltype(&hkGetTimeOfDay)>()(obj, time, a);
    GetTimeEvent ev { o };
    Eventing::get().dispatch(ev);

    return ev.getTime();
}

void GenericHooks::hkDimensionTick(SDK::Dimension* obj) {
    WeatherEvent ev {};
    Eventing::get().dispatch(ev);

    if (!ev.shouldShowWeather()) {
        obj->weather->data = SDK::Weather::WeatherData {};
    } else {
        switch (ev.getWeatherMode()) {
        case 1:
            obj->weather->data = SDK::Weather::WeatherData {};
            break;
        case 2:
            obj->weather->data.rain = 1.f;
            obj->weather->data.rainTransition = 1.f;
            obj->weather->data.rainTransitionOld = 1.f;
            obj->weather->data.thunder = 0.f;
            obj->weather->data.thunderTransition = 0.f;
            obj->weather->data.thunderTransitionOld = 0.f;
            break;
        case 3:
            obj->weather->data.rain = 1.f;
            obj->weather->data.rainTransition = 1.f;
            obj->weather->data.rainTransitionOld = 1.f;
            obj->weather->data.thunder = 1.f;
            obj->weather->data.thunderTransition = 1.f;
            obj->weather->data.thunderTransitionOld = 1.f;
            break;
        default:
            break;
        }
    }

    DimensionHook->oFunc<decltype(&hkDimensionTick)>()(obj);
}

Color* GenericHooks::hkGetFogColor(SDK::Dimension* obj, Color* out, SDK::Actor* ent, float f) {
    FogColorHook->oFunc<decltype(&hkGetFogColor)>()(obj, out, ent, f);

    FogColorEvent ev { out };
    Eventing::get().dispatch(ev);

    return out;
}

SDK::GuiMessage& GenericHooks::hkAddMessage(void* obj, SDK::GuiMessage& msg) {
    // MessageContext
    std::string& str = msg.message;

    ChatMessageEvent ev { str };
    if (Eventing::get().dispatch(ev)) {
        return *(SDK::ClientInstance::get()->getGuiData()->messages.end() - 1);
    }

    return AddMessageHook->oFunc<decltype(&hkAddMessage)>()(obj, msg);
}

void GenericHooks::hkUpdatePlayer(SDK::CameraComponent* obj, void* a, void* b) {
    UpdatePlayerCameraEvent ev {};
    Eventing::get().dispatch(ev);

    auto oAngles = util::QuaternionToRot(obj->lookAngles);
    auto origAngles = obj->lookAngles;

    if (ev.getNewRot()) {
        obj->lookAngles = util::RotToQuaternion(*ev.getNewRot());
        UpdatePlayerHook->oFunc<decltype(&hkUpdatePlayer)>()(obj, a, b);
        if (!ev.isPersistent()) obj->lookAngles = origAngles;
        return;
    }
    UpdatePlayerHook->oFunc<decltype(&hkUpdatePlayer)>()(obj, a, b);
}

void GenericHooks::hkOnUri(void* obj, void* pUri) {
    OnUriHook->oFunc<decltype(&hkOnUri)>()(obj, pUri);
}

void GenericHooks::hkGrabCursor(SDK::ClientInstance* obj) {
    if (Necromancer::get().getScreenManager().getActiveScreen()) return;
    GrabCursorHook->oFunc<decltype(&hkGrabCursor)>()(obj);
}

void GenericHooks::hkBaseActorRenderer_renderText(void* screenContext, void* viewData, std::string* tagData, void* font,
                                                  void* mesh) {
    const auto old = *tagData;
    std::string buf = *tagData;
    RenderNameTagEvent ev { &buf };
    Eventing::get().dispatch(ev);
    *tagData = buf;

    BaseActorRenderer_renderTextHook->oFunc<decltype(&hkBaseActorRenderer_renderText)>()(screenContext, viewData,
                                                                                         tagData, font, mesh);

    *tagData = old;
}

void GenericHooks::hkAppPlatformGDK_releaseMouse(void* _this) {
    AppPlatformGDK_releaseMouseHook->oFunc<decltype(&hkAppPlatformGDK_releaseMouse)>()(_this);

    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    MouseReleaseEvent ev {};
    Eventing::get().dispatch(ev);
}

void GenericHooks::hkForwardSoundSubtitle(void* screenModel, std::string const& subtitle, unsigned int direction,
                                          unsigned int isOwnSound) {
    if (util::IsPlayingNecromancerSound()) return;

    ForwardSoundSubtitleHook->oFunc<decltype(&hkForwardSoundSubtitle)>()(screenModel, subtitle, direction, isOwnSound);
}

GenericHooks::GenericHooks()
    : HookGroup("General") {
    // LoadLibraryAHook = addHook(reinterpret_cast<uintptr_t>(&::LoadLibraryW), hkLoadLibraryW);
    // LoadLibraryWHook = addHook(reinterpret_cast<uintptr_t>(&::LoadLibraryA), hkLoadLibraryW);

    MultiPlayerLevel__subTickHook =
        addHook(Signatures::MultiPlayerLevel__subTick.result, MultiPlayerLevel__subTick, "Level::tick");

    ChatScreenController_sendChatMesageHook =
        addHook(Signatures::ChatScreenController_sendChatMessage.result, ChatScreenController_sendChatMessage,
                "ChatScreenController::sendChatMessage");

    // GameRenderer_renderCurrentFrameHook = addHook(Signatures::GameRenderer__renderCurrentFrame.result,
    //	GameRenderer_renderCurrentFrame, "GameRenderer::_renderCurrentFrame");

    MainWindow__windowProcCallbackHook = addHook(Signatures::MainWindow__windowProcCallback.result,
                                                 MainWindow__windowProcCallback, "MainWindow::_windowProcCallback");

    GameCore_handleMouseInputHook =
        addHook(Signatures::GameCore_handleMouseInput.result, GameCore_handleMouseInput, "GameCore::handleMouseInput");

    AveragePingHook =
        addHook(Signatures::RakPeer_GetAveragePing.result, RakPeer_getAveragePing, "RakPeer::GetAveragePing");

    TurnDeltaHook = addHook(Signatures::LocalPlayer_applyTurnDelta.result, LocalPlayer_applyTurnDelta,
                            "LocalPlayer::applyTurnDelta");

    ClientInputUpdateSystem_tickBaseInputHook =
        addHook(Signatures::ClientInputUpdateSystemInternal_tickUpdateClientInput.result,
                ClientInputUpdateSystemInternal_tickUpdateClientInput, "ClientInputUpdateSystem::tickBaseInput");

    // ViewBobHook = addHook(Signatures::CameraViewBob.result, CameraViewBob, "`anonymous namespace'::_bobMovement");
    if (Signatures::Vtable::Level.result) {
        Level_initializeHook = addHook(reinterpret_cast<uintptr_t*>(Signatures::Vtable::Level.result)[1],
                                       Level_initialize, "Level::initialize");
        Level_startLeaveGameHook = addHook(reinterpret_cast<uintptr_t*>(Signatures::Vtable::Level.result)[2],
                                           Level_startLeaveGame, "Level::startLeaveGame");
    }

    RenderEntityHook = addHook(Signatures::ActorRenderDispatcher_render.result, ActorRenderDispatcher_render,
                               "ActorRenderDispatcher::render");
    OutlineSelectionHook =
        addHook(Signatures::LevelRendererPlayer_renderOutlineSelection.result,
                LevelRendererPlayer_renderOutlineSelection, "LevelRendererPlayer::renderOutlineSelection");
    // RenderGuiItemNewHook = addHook(Signatures::ItemRenderer_renderGuiItemNew.result, hkRenderGuiItemNew,
    // "ItemRenderer::renderGuiItemNew");

    FogColorHook = addHook(Signatures::Dimension_getSkyColor.result, hkGetFogColor, "Dimension::getFogColor");
    GetTimeOfDayHook = addHook(Signatures::Dimension_getTimeOfDay.result, hkGetTimeOfDay, "Dimension::getTimeOfDay");
    DimensionHook = addHook(Signatures::Dimension_tick.result, hkDimensionTick, "Dimension::tick");
    AddMessageHook = addHook(Signatures::GuiData__addMessage.result, hkAddMessage, "GuiData::_addMessage");
    UpdatePlayerHook =
        addHook(Signatures::_updatePlayer.result, hkUpdatePlayer, "`anonymous namespace'::_updatePlayer");
    OnUriHook = addHook(Signatures::GameArguments__onUri.result, hkOnUri, "GameArguments::_onUri");
    GrabCursorHook = addHook(Signatures::ClientInstance_grabCursor.result, hkGrabCursor, "`ClientInstance::grabCursor");
    BaseActorRenderer_renderTextHook = addHook(Signatures::BaseActorRenderer_renderText.result,
                                               hkBaseActorRenderer_renderText, "BaseActorRenderer::renderText");
    AppPlatformGDK_releaseMouseHook = addHook(Signatures::AppPlatformGDK_releaseMouse.result,
                                              hkAppPlatformGDK_releaseMouse, "AppPlatform::releaseMouse");
    ForwardSoundSubtitleHook = addHook(Signatures::ClientInstanceScreenModel_forwardSoundSubtitle.result,
                                       hkForwardSoundSubtitle, "ClientInstanceScreenModel::forwardSoundSubtitle");
}

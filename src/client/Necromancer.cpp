#include "pch.h"
// Necromancer.cpp : Defines the entry point for the application.
//
#include <cstdint>
#include <limits>

#include "Necromancer.h"
#include "BuildTimestamp.h"
#include "localization/LocalizeString.h"

#include "client/screen/TextBox.h"

#include "feature/module/ModuleManager.h"
#include "feature/command/CommandManager.h"

#include "config/ConfigManager.h"
#include "misc/ClientMessageQueue.h"
#include "misc/TempStorage.h"
#include "misc/PlayerListManager.h"
#include "misc/EntityCache.h"
#include "misc/RenderFrameState.h"
#include "misc/LatencySpoof.h"
#include "input/Keyboard.h"
#include "memory/hook/Hooks.h"
#include "event/Eventing.h"
#include "event/events/KeyUpdateEvent.h"
#include "event/events/RendererInitEvent.h"
#include "event/events/RendererCleanupEvent.h"
#include "misc/ModuleProfiler.h"
#include "event/events/FocusLostEvent.h"
#include "event/events/AppSuspendedEvent.h"
#include "event/events/UpdateEvent.h"
#include "event/events/CharEvent.h"
#include "event/events/ClickEvent.h"
#include "event/events/BobMovementEvent.h"
#include "event/events/LeaveGameEvent.h"

#include "mc/Addresses.h"

#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/game/FontRepository.h"
#include <winrt/windows.ui.viewmanagement.h>
#include <winrt/windows.storage.streams.h>

#include <mc/common/client/gui/ScreenView.h>
#include <mc/common/client/gui/controls/VisualTree.h>
#include <mc/common/client/gui/controls/UIControl.h>

#include "event/events/MouseReleaseEvent.h"
#include "mc/common/client/game/GameCore.h"

using namespace winrt;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Filters;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Storage;

#include "render/Renderer.h"
#include "screen/ScreenManager.h"
#include "render/asset/Assets.h"
#include "resource/Resource.h"

using namespace std;

#include <tlhelp32.h>

namespace {
    alignas(Eventing) char eventing[sizeof(Eventing)] = {};
    alignas(Necromancer) char necromancerBuf[sizeof(Necromancer)] = {};
    alignas(Renderer) char rendererBuf[sizeof(Renderer)] = {};
    alignas(ModuleManager) char mmgrBuf[sizeof(ModuleManager)] = {};
    alignas(ClientMessageQueue) char messageSinkBuf[sizeof(ClientMessageQueue)] = {};
    alignas(CommandManager) char commandMgrBuf[sizeof(CommandManager)] = {};
    alignas(ConfigManager) char configMgrBuf[sizeof(ConfigManager)] = {};
    alignas(SettingGroup) char mainSettingGroup[sizeof(SettingGroup)] = {};
    alignas(NecromancerHooks) char hooks[sizeof(NecromancerHooks)] = {};
    alignas(ScreenManager) char scnMgrBuf[sizeof(ScreenManager)] = {};
    alignas(Assets) char assetsBuf[sizeof(Assets)] = {};
    alignas(Keyboard) char keyboardBuf[sizeof(Keyboard)] = {};
    alignas(Notifications) char notificaitonsBuf[sizeof(Notifications)] = {};

    bool hasInjected = false;

    struct DllMainCall {
        HINSTANCE hinstDLL;
        DWORD fdwReason;
        LPVOID reserved;
    };

    struct ImageRange {
        uintptr_t low = 0;
        uintptr_t high = 0;

        [[nodiscard]] bool contains(uintptr_t address) const noexcept {
            return address >= low && address < high;
        }
    };

    ImageRange getImageRange(HMODULE module) noexcept {
        ImageRange range {};
        if (!module) return range;

        // An HMODULE is the image base, so the size comes straight out of the PE
        // headers. Avoids linking psapi just for GetModuleInformation.
        const auto base = reinterpret_cast<uintptr_t>(module);
        auto const* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return range;

        auto const* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS const*>(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return range;

        range.low = base;
        range.high = base + ntHeaders->OptionalHeader.SizeOfImage;
        return range;
    }

    // Max stack bytes inspected per thread. Deep enough to cover any hook chain,
    // bounded so a runaway stack cannot stall the scan.
    constexpr SIZE_T maxStackScanBytes = 256 * 1024;

    bool threadTouchesImage(HANDLE thread, ImageRange const& range) noexcept {
        CONTEXT context {};
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (!GetThreadContext(thread, &context)) {
            // Cannot inspect it, so we cannot prove it is clear.
            return true;
        }

        if (range.contains(static_cast<uintptr_t>(context.Rip))) {
            return true;
        }

        auto stackPointer = static_cast<uintptr_t>(context.Rsp);
        if (stackPointer == 0) return false;

        MEMORY_BASIC_INFORMATION mbi {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(stackPointer), &mbi, sizeof(mbi)) != sizeof(mbi)) {
            return true;
        }
        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        auto regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        auto scanEnd = std::min(regionEnd, stackPointer + maxStackScanBytes);

        // A return address into our image means the thread is inside a game
        // function that one of our detours called: unmapping now would return
        // it into freed memory.
        for (auto cursor = stackPointer & ~static_cast<uintptr_t>(7); cursor + sizeof(uintptr_t) <= scanEnd;
             cursor += sizeof(uintptr_t)) {
            if (range.contains(*reinterpret_cast<uintptr_t const*>(cursor))) {
                return true;
            }
        }

        return false;
    }

    // True when no other thread has its instruction pointer or a stack return
    // address inside our image. All other threads stay suspended for the whole
    // check so the answer cannot go stale between inspecting and acting on it.
    //
    // Nothing in here may allocate, lock or log while threads are suspended: a
    // suspended thread can hold the heap or logger lock, which would deadlock us.
    bool noThreadInsideImage(ImageRange const& range, std::vector<DWORD>& threadIds,
                             std::vector<HANDLE>& threadHandles) noexcept {
        threadIds.clear();
        threadHandles.clear();

        const auto ownThread = GetCurrentThreadId();
        const auto ownProcess = GetCurrentProcessId();

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;

        THREADENTRY32 entry {};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.dwSize >= FIELD_OFFSET(THREADENTRY32, th32ThreadID) + sizeof(entry.th32ThreadID) &&
                    entry.th32OwnerProcessID == ownProcess && entry.th32ThreadID != ownThread) {
                    threadIds.push_back(entry.th32ThreadID);
                }
                entry.dwSize = sizeof(entry);
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);

        threadHandles.reserve(threadIds.size());
        for (const auto id : threadIds) {
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
            if (thread) threadHandles.push_back(thread);
        }

        bool clear = true;
        for (auto thread : threadHandles) {
            if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                continue;
            }
            if (clear && threadTouchesImage(thread, range)) {
                clear = false;
            }
        }

        for (auto thread : threadHandles) {
            ResumeThread(thread);
        }
        for (auto thread : threadHandles) {
            CloseHandle(thread);
        }
        threadHandles.clear();

        return clear;
    }

    // Refuse to unload rather than unmap under a live thread. Staying resident
    // leaks the image; unmapping too early kills the game.
    constexpr auto drainTimeout = 5s;

    bool waitForThreadsToLeaveImage(HMODULE module) noexcept {
        const auto range = getImageRange(module);
        if (range.low == 0 || range.high <= range.low) {
            Logger::Fatal("Eject: could not resolve image range");
            return false;
        }

        std::vector<DWORD> threadIds;
        std::vector<HANDLE> threadHandles;

        const auto deadline = std::chrono::steady_clock::now() + drainTimeout;
        int attempts = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            ++attempts;
            if (noThreadInsideImage(range, threadIds, threadHandles)) {
                Logger::Info("Eject: image clear of all threads after {} scan(s)", attempts);
                return true;
            }
            std::this_thread::sleep_for(2ms);
        }

        Logger::Fatal("Eject: threads still inside image after {} scans; keeping DLL loaded", attempts);
        return false;
    }

    DWORD WINAPI ejectThread(LPVOID module) {
        auto& necro = Necromancer::get();
        auto* dll = static_cast<HMODULE>(module);

        if (!necro.prepareForUnload(dll)) {
            Logger::Fatal("Eject: pre-unload teardown failed; keeping DLL loaded");
            return 1;
        }

        Logger::Info("Necromancer Client detached.");
        Logger::Shutdown();
        FreeLibraryAndExitThread(dll, 0);
    }

}

#define MVSIG(...) \
    ([]() -> std::pair<SigImpl*, SigImpl*> {\
/*if (SDK::internalVers == SDK::VLATEST) */return {&Signatures::__VA_ARGS__, &Signatures::__VA_ARGS__}; }\
/*if (SDK::internalVers == SDK::V1_20_40) { return {&Signatures_1_20_40::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/ \
/*if (SDK::internalVers == SDK::V1_20_30) { return {&Signatures_1_20_30::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/ \
/*if (SDK::internalVers == SDK::V1_19_51) { return {&Signatures_1_19_51::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/ \
/*return {&Signatures_1_18_12::__VA_ARGS__, &Signatures::__VA_ARGS__}; }*/\
)()

#define NECROMANCER_EXPORT extern "C" __declspec(dllexport)

NECROMANCER_EXPORT const char* NecromancerGetDllVersion() noexcept {
    return Necromancer::version.data();
}

NECROMANCER_EXPORT uint32_t NecromancerGetSupportedMinecraftVersionCount() noexcept {
    const auto count = Necromancer::supportedMinecraftVersions.size();
    if (count > std::numeric_limits<uint32_t>::max()) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(count);
}

NECROMANCER_EXPORT const char* NecromancerGetSupportedMinecraftVersion(uint32_t index) noexcept {
    if (index >= Necromancer::supportedMinecraftVersions.size()) {
        return nullptr;
    }

    return Necromancer::supportedMinecraftVersions[index].data();
}

DWORD __stdcall startThreadImpl(HINSTANCE dll) {
    BEGIN_ERROR_HANDLER
    // Needed for Logger
    new (messageSinkBuf) ClientMessageQueue;
    new (eventing) Eventing();
    new (necromancerBuf) Necromancer;
    new (notificaitonsBuf) Notifications;

    std::filesystem::create_directory(util::GetNecromancerPath());
    std::filesystem::create_directory(util::GetNecromancerPath() / "Assets");
    NecromancerTemp::cleanup();
    Logger::Setup();

    // Profiling is opt-in: the hidden `debug_info` module (or the `profiler`
    // command) turns it on. Leaving it enabled here taxed every single event
    // dispatch with a slot lookup + two chrono reads for no reason.

#ifdef NECROMANCER_CRASH_REPORTING
    DebugExceptionHandler::Install();
#endif

#if defined(NECROMANCER_NIGHTLY)
    Logger::Info("Necromancer Client [NIGHTLY] {}", Necromancer::version);
#elif defined(NECROMANCER_DEBUG)
    Logger::Info("Necromancer Client [DEBUG] {}", Necromancer::version);
#else
    Logger::Info("Necromancer Client {}", Necromancer::version);
#endif

    char path[MAX_PATH] {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);

    DWORD handle;
    DWORD size = GetFileVersionInfoSizeA(path, &handle);

    if (size == 0) {
        Logger::Fatal("Failed to get file version size");
    }

    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoA(path, handle, size, data.data())) {
        Logger::Fatal("Failed to get file version");
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT len = 0;

    if (VerQueryValueA(data.data(), "\\", reinterpret_cast<LPVOID*>(&fileInfo), &len)) {
        const auto major = HIWORD(fileInfo->dwFileVersionMS);
        const auto minor = LOWORD(fileInfo->dwFileVersionMS);
        const auto build = HIWORD(fileInfo->dwFileVersionLS);

        Necromancer::get().gameVersion = std::format("{}.{}.{}", major, minor, build);
    }

    /*winrt::Windows::ApplicationModel::Package package = winrt::Windows::ApplicationModel::Package::Current();
    winrt::Windows::ApplicationModel::PackageVersion version = package.Id().Version();

    {
        std::string rev = std::to_string(version.Build);
        std::string rem = rev.substr(0, rev.size() - 2); // remove 2 digits from end

        int ps = std::stoi(rem);
        std::stringstream ss;
        ss << version.Major << "." << version.Minor << "." << ps;// hacky
        Necromancer::get().gameVersion = ss.str();
    }*/
    Logger::Info("Minecraft {}", Necromancer::get().gameVersion);

    Logger::Info("Loading assets");
    Necromancer::get().dllInst = dll;
    // ... init assets
    Necromancer::get().initL10n();

    Logger::Info("Resolving signatures..");

    int sigCount = 0;
    int deadCount = 0;
    std::vector<std::string> failedSignatures;

    if (Necromancer::supportsMinecraftVersion(Necromancer::get().gameVersion)) {
        // not needed as it will always just be latest
        // SDK::internalVers = vers;
    } else {
        std::stringstream ss;
        ss << "Necromancer Client does not support your version: " << Necromancer::get().gameVersion
           << ". Necromancer only supports the following versions:\n\n";

        for (const auto key : Necromancer::supportedMinecraftVersions) {
            ss << key << "\n";
        }

        Logger::Warn(ss.str());
    }

    std::vector<std::pair<SigImpl*, SigImpl*>> sigList = {
        MVSIG(MainWindow__windowProcCallback),
        MVSIG(LevelRenderer_renderLevel),
        MVSIG(Options_getGamma),
        MVSIG(Options_getPerspective),
        MVSIG(Options_getHideHand),
        MVSIG(ClientInstance_grabCursor),
        MVSIG(ClientInstance_releaseCursor),
        MVSIG(MultiPlayerLevel__subTick),
        MVSIG(ChatScreenController_sendChatMessage),
        MVSIG(GameCore_handleMouseInput),
        MVSIG(MinecraftGame_onDeviceLost),
        MVSIG(RenderController_getOverlayColor),
        MVSIG(ScreenView_setupAndRender),
        MVSIG(KeyMap),
        MVSIG(MinecraftGame__update),
        MVSIG(GpuInfo),
        MVSIG(RakPeer_GetAveragePing),
        MVSIG(ClientInputUpdateSystemInternal_tickUpdateClientInput),
        MVSIG(LocalPlayer_applyTurnDelta),
        MVSIG(ItemStackBase_getHoverName),
        MVSIG(I18n_getI18n),
        MVSIG(ItemStack_ItemStackBlock),
        MVSIG(ItemStackVtable),
        MVSIG(ItemStackBase_destructor),
        MVSIG(Vtable::Level),
        MVSIG(Tessellator_begin),
        MVSIG(Tessellator_vertex),
        MVSIG(Tessellator_color),
        MVSIG(MeshHelpers_renderMeshImmediately),
        MVSIG(BaseActorRenderContext_BaseActorRenderContext),
        MVSIG(ItemRenderer_renderGuiItemNew),
        MVSIG(ActorRenderDispatcher_render),
        MVSIG(MolangVariable__findOrAddVariableIndex),
        MVSIG(MolangVariableMap__getOrAddMolangVariable),
        MVSIG(LevelRendererPlayer_renderOutlineSelection),
        MVSIG(Dimension_getSkyColor),
        MVSIG(Dimension_getTimeOfDay),
        MVSIG(Dimension_tick),
        MVSIG(Misc::thirdPersonNametag),
        MVSIG(ItemStackBase_getDamageValue),
        MVSIG(ContainerManagerModel_getSlot),
        MVSIG(CompoundTag_get),
        MVSIG(ContainerScreenController_handleAutoPlace),
        MVSIG(ContainerScreenController_handleTakePlace),
        MVSIG(ContainerManagerModel_autoPlace),
        MVSIG(ContainerManagerModel_takePlace),
        MVSIG(ItemStackNetManagerClient_addRequestAction),
        MVSIG(ItemStackNetManagerClient_beginRequest),
        MVSIG(ItemStackNetManagerClient_endRequest),
        MVSIG(ContainerManagerModel_transferItems),
        MVSIG(ContainerScreenController_coalesceOrAutoPlaceItems),
        MVSIG(ContainerScreenController_autoPlaceItems),
        MVSIG(ContainerScreenController_getDestinationCollections),
        MVSIG(SlotInfo_ctor),
        MVSIG(MinecraftScreenController_tryExit),
        MVSIG(MinecraftPackets_createPacket),
        MVSIG(RakNetSocket_send),
        MVSIG(Actor_attack),
        MVSIG(GameMode_attack),
        MVSIG(GameMode_buildBlock),
        MVSIG(GuiData__addMessage),
        MVSIG(_updatePlayer),
        MVSIG(GameArguments__onUri),
        MVSIG(RenderMaterialGroup__common),
        MVSIG(GuiData_displayClientMessage),
        MVSIG(ClientInstanceScreenModel_forwardSoundSubtitle),
        MVSIG(BaseActorRenderer_renderText),
        MVSIG(AppPlatformGDK_releaseMouse),
        MVSIG(Misc::Platform_GameCore),
        MVSIG(Misc::mouseDevice),
    };

    new (configMgrBuf) ConfigManager();
    if (!Necromancer::getConfigManager().loadMaster()) {
        Logger::Fatal("Could not load master config!");
    } else {
        Logger::Info("Loaded master config");
    }
    new (mainSettingGroup) SettingGroup("global");

    // The Language setting is a special case because we need it to apply names to other global settings.
    Necromancer::get().initLanguageSetting();
    Necromancer::getConfigManager().applyLanguageConfig("language");

    Necromancer::get().initSettings();
    Necromancer::getConfigManager().applyGlobalConfig();

    new (mmgrBuf) ModuleManager;
    new (commandMgrBuf) CommandManager;
    new (scnMgrBuf) ScreenManager(); // needs to be initialized before renderer

    PlayerListManager::get();
    EntityCache::get();
    RenderFrameState::get();

    new (rendererBuf) Renderer();
    new (assetsBuf) Assets();

    for (auto& entry : sigList) {
        if (!entry.first->mod) continue;
        auto res = entry.first->resolve();
        if (!res) {
            Logger::Warn("Signature FAILED to resolve: {}", entry.first->name);
            failedSignatures.push_back(std::string(entry.first->name));
            deadCount++;
        } else {
            entry.second->result = entry.first->result;
            entry.second->scan_result = entry.first->scan_result;
            sigCount++;
        }
    }
    Logger::Info("Resolved {} signatures ({} dead)", sigCount, deadCount);

    if (!failedSignatures.empty()) {
        std::wstring ss;
        ss += L"Failed to resolve " + std::to_wstring(deadCount) + L" signature(s). The following signatures could not be found in the game binary:\n\n";
        for (const auto& name : failedSignatures) {
            ss += util::StrToWStr(name) + L"\n";
        }
        ss += L"\nThis usually means the game has been updated and Necromancer needs an update. Please report this to the developer.";
        MessageBoxW(nullptr, ss.c_str(), L"Necromancer - Signature Scan Failed", MB_ICONERROR | MB_OK);
    }

    MH_Initialize();
    new (hooks) NecromancerHooks();

    // Socket-layer latency hook. Installed disabled (0ms); Backtrack's Fake
    // Latency slider drives it.
    LatencySpoof::init();

    new (keyboardBuf) Keyboard(reinterpret_cast<int*>(Signatures::KeyMap.result));

    Logger::Info("Waiting for game to load..");

    while (!SDK::ClientInstance::get()) {
        std::this_thread::sleep_for(10ms);
    }

    Necromancer::get().initialize(dll);

    Logger::Info("Initialized Necromancer Client");
    return 0ul;
    END_ERROR_HANDLER
}

DWORD __stdcall startThread(LPVOID context) {
#ifdef NECROMANCER_CRASH_REPORTING
    return static_cast<DWORD>(DebugExceptionHandler::RunWithSehGuard(
        [](void* param) -> std::uintptr_t {
            return startThreadImpl(static_cast<HINSTANCE>(param));
        },
        context, "Caught SEH exception in Necromancer startup thread"));
#else
    return startThreadImpl(static_cast<HINSTANCE>(context));
#endif
}

BOOL WINAPI DllMainImpl(HINSTANCE hinstDLL, // handle to DLL module
                        DWORD fdwReason,    // reason for calling function
                        LPVOID reserved)    // reserved
{
    BEGIN_ERROR_HANDLER
    if (GetModuleHandleA("Minecraft.Windows.exe") != GetModuleHandleA(NULL)) return TRUE;

    if (fdwReason == DLL_PROCESS_ATTACH) {
        hasInjected = true;

        DisableThreadLibraryCalls(hinstDLL);
        CloseHandle(CreateThread(nullptr, 0, startThread, hinstDLL, 0, nullptr));
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        hasInjected = false;
    }
    return TRUE; // Successful DLL_PROCESS_ATTACH.
    END_ERROR_HANDLER
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, // handle to DLL module
                    DWORD fdwReason,    // reason for calling function
                    LPVOID reserved)    // reserved
{
#ifdef NECROMANCER_CRASH_REPORTING
    DllMainCall call { hinstDLL, fdwReason, reserved };
    return static_cast<BOOL>(DebugExceptionHandler::RunWithSehGuard(
        [](void* context) -> std::uintptr_t {
            auto* call = static_cast<DllMainCall*>(context);
            return DllMainImpl(call->hinstDLL, call->fdwReason, call->reserved);
        },
        &call, "Caught SEH exception in Necromancer DllMain"));
#else
    return DllMainImpl(hinstDLL, fdwReason, reserved);
#endif
}

Necromancer& Necromancer::get() noexcept {
    return *std::launder(reinterpret_cast<Necromancer*>(necromancerBuf));
}

ModuleManager& Necromancer::getModuleManager() noexcept {
    return *std::launder(reinterpret_cast<ModuleManager*>(mmgrBuf));
}

CommandManager& Necromancer::getCommandManager() noexcept {
    return *std::launder(reinterpret_cast<CommandManager*>(commandMgrBuf));
}

ConfigManager& Necromancer::getConfigManager() noexcept {
    return *std::launder(reinterpret_cast<ConfigManager*>(configMgrBuf));
}

ClientMessageQueue& Necromancer::getClientMessageQueue() noexcept {
    return *std::launder(reinterpret_cast<ClientMessageQueue*>(messageSinkBuf));
}

SettingGroup& Necromancer::getSettings() noexcept {
    return *std::launder(reinterpret_cast<SettingGroup*>(mainSettingGroup));
}

NecromancerHooks& Necromancer::getHooks() noexcept {
    return *std::launder(reinterpret_cast<NecromancerHooks*>(hooks));
}

Eventing& Necromancer::getEventing() noexcept {
    return *std::launder(reinterpret_cast<Eventing*>(eventing));
}

Renderer& Necromancer::getRenderer() noexcept {
    return *std::launder(reinterpret_cast<Renderer*>(rendererBuf));
}

ScreenManager& Necromancer::getScreenManager() noexcept {
    return *std::launder(reinterpret_cast<ScreenManager*>(scnMgrBuf));
}

Assets& Necromancer::getAssets() noexcept {
    return *std::launder(reinterpret_cast<Assets*>(assetsBuf));
}

Keyboard& Necromancer::getKeyboard() noexcept {
    return *std::launder(reinterpret_cast<Keyboard*>(keyboardBuf));
}

Notifications& Necromancer::getNotifications() noexcept {
    return *std::launder(reinterpret_cast<Notifications*>(notificaitonsBuf));
}

std::optional<float> Necromancer::getMenuBlur() {
    if (std::get<BoolValue>(this->menuBlurEnabled)) {
        return std::get<FloatValue>(this->menuBlur);
    }
    return std::nullopt;
}

std::vector<std::string> Necromancer::getNecromancerUsers() {
    return necromancerUsers;
}

int Necromancer::getSelectedLanguage() {
    if (!l10nData) return 0;
    return l10nData->resolveLanguageSetting(clientLanguage.getSelectedKey());
}

void Necromancer::queueEject() noexcept {
    if (this->shouldEject.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    Logger::Info("Eject: queued");
}

bool Necromancer::isEjectQueued() const noexcept {
    return this->shouldEject.load(std::memory_order_acquire);
}

bool Necromancer::isEjectReadyForRenderThread() const noexcept {
    return this->shouldEject.load(std::memory_order_acquire) &&
           this->mainThreadEjectCleanupComplete.load(std::memory_order_acquire);
}

void Necromancer::setRenderHookActive(bool active) noexcept {
    this->renderHookActive.store(active, std::memory_order_release);
}

bool Necromancer::isRenderHookActive() const noexcept {
    return this->renderHookActive.load(std::memory_order_acquire);
}

bool Necromancer::prepareForUnload(HMODULE module) noexcept {
    if (this->unloadPrepared.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    try {
        Logger::Info("Eject: pre-unload teardown start");

        ModuleProfiler::get().shutdown();
        Logger::Info("Eject: profiler stopped");

        PlayerListManager::get().shutdown();
        Logger::Info("Eject: player-list writer stopped");

        LatencySpoof::shutdown();
        Logger::Info("Eject: latency worker stopped");

        // Stop new entries into our code before waiting for the ones already
        // running to leave. Doing this the other way round lets a thread walk
        // into a detour we already decided was drained.
        auto disableStatus = Necromancer::getHooks().disable();
        Logger::Info("Eject: hooks disabled ({})", static_cast<int>(disableStatus));
        if (disableStatus != MH_OK && disableStatus != MH_ERROR_DISABLED &&
            disableStatus != MH_ERROR_NOT_INITIALIZED) {
            this->unloadPrepared.store(false, std::memory_order_release);
            return false;
        }

        // Trampolines must stay mapped until every thread has left our code:
        // a detour still in flight will call the original through one.
        if (!waitForThreadsToLeaveImage(module)) {
            this->unloadPrepared.store(false, std::memory_order_release);
            return false;
        }

        auto uninitializeStatus = MH_Uninitialize();
        Logger::Info("Eject: MinHook uninitialized ({})", static_cast<int>(uninitializeStatus));
        if (uninitializeStatus != MH_OK && uninitializeStatus != MH_ERROR_NOT_INITIALIZED) {
            this->unloadPrepared.store(false, std::memory_order_release);
            return false;
        }

        Necromancer::getHooks().releaseHookStorage();

#ifdef NECROMANCER_CRASH_REPORTING
        DebugExceptionHandler::Uninstall();
#endif

        return true;
    } catch (std::exception const& exception) {
        Logger::Fatal("Eject: pre-unload teardown exception: {}", exception.what());
    } catch (...) {
        Logger::Fatal("Eject: unknown pre-unload teardown exception");
    }

    this->unloadPrepared.store(false, std::memory_order_release);
    return false;
}

void Necromancer::completeEjectFromRenderThread() noexcept {
    if (!this->isEjectReadyForRenderThread()) {
        return;
    }

    if (this->unloadStarted.load(std::memory_order_acquire)) {
        return;
    }

    Logger::Info("Eject: render thread cleanup start");

    if (Necromancer::getRenderer().hasInitialized()) {
        this->releaseDeferredD2DResources();
        Necromancer::getAssets().unloadAll();
        Necromancer::getRenderer().shutdownForEject();
    }

    if (!this->unloadStarted.exchange(true, std::memory_order_acq_rel)) {
        CloseHandle(CreateThread(nullptr, 0, ejectThread, dllInst, 0, nullptr));
        Logger::Info("Eject: unload thread spawned");
    }
}

SDK::Font* Necromancer::getFont() {
    auto ci = SDK::ClientInstance::get();
    if (!ci || !ci->minecraftGame) return nullptr;
    auto repo = ci->minecraftGame->getFontRepository();
    if (!repo) return nullptr;
    switch (this->hudFontMode.getSelectedKey()) {
    case 1:
        return repo->getMinecraftFont();
    case 2:
        return repo->getSmoothFont();
    case 0:
    default:
        return repo->getMinecraftFont();
    }
}

void Necromancer::applyHUDFontFamily() {
    auto& renderer = getRenderer();
    if (!renderer.getDWriteFactory()) return;

    bool wantMinecraft = this->hudFontMode.getSelectedKey() == 1 && renderer.hasEmbeddedMinecraftFont();
    renderer.updatePrimaryFont(wantMinecraft ? EmbeddedFontCollection::familyName() : L"Segoe UI");
    std::wstring wantSecondary =
        wantMinecraft ? EmbeddedFontCollection::familyName() : std::get<TextValue>(secondaryFont).str;
    renderer.updateSecondaryFont(wantSecondary);
}

void Necromancer::initialize(HINSTANCE hInst) {
    this->dllInst = hInst;

    Necromancer::getEventing().listen<UpdateEvent, &Necromancer::onUpdate>(this, 2);
    Necromancer::getEventing().listen<KeyUpdateEvent, &Necromancer::onKey>(this, 2);
    Necromancer::getEventing().listen<RendererInitEvent, &Necromancer::onRendererInit>(this, 2);
    Necromancer::getEventing().listen<RendererCleanupEvent, &Necromancer::onRendererCleanup>(this, 2);
    Necromancer::getEventing().listen<AppSuspendedEvent, &Necromancer::onSuspended>(this, 2);
    Necromancer::getEventing().listen<CharEvent, &Necromancer::onChar>(this, 2);
    Necromancer::getEventing().listen<ClickEvent, &Necromancer::onClick>(this, 2);
    Necromancer::getEventing().listen<BobMovementEvent, &Necromancer::onBobView>(this, 2);
    Necromancer::getEventing().listen<LeaveGameEvent, &Necromancer::onLeaveGame>(this, 2);
    Necromancer::getEventing().listen<RenderLayerEvent, &Necromancer::onRenderLayer>(this, 2);
    Necromancer::getEventing().listen<RenderOverlayEvent, &Necromancer::onRenderOverlay>(this, 2);
    Necromancer::getEventing().listen<TickEvent, &Necromancer::onTick>(this, 2);
    Necromancer::getEventing().listen<MouseReleaseEvent, &Necromancer::onMouseRelease>(this, 2);

    Logger::Info("Initialized Hooks");
    getHooks().enable();
    Logger::Info("Enabled Hooks");

    // doesn't work, maybe it's stored somewhere else too
    // if (SDK::internalVers < SDK::V1_20) {
    //    patchKey();
    //}
}

void Necromancer::threadsafeInit() {
    this->gameThreadId = std::this_thread::get_id();
    // TODO: necromancer beta only
    // if (SDK::ClientInstance::get()->minecraftGame->xuid.size() > 0) wnd->postXUID();

    Necromancer::getConfigManager().applyModuleConfig();
    Necromancer::getConfigManager().applyKeybindConfig();

    Necromancer::getRenderer().setShouldInit();

    Necromancer::getCommandManager().prefix = Necromancer::get().getCommandPrefix();
    Necromancer::getNotifications().push(LocalizeString::get("client.intro.welcome"));
    Necromancer::getNotifications().push(
        util::FormatWString(LocalizeString::get("client.intro.menubutton"),
                            { util::StrToWStr(util::KeyToString(Necromancer::get().getMenuKey().value)) }));
}

void Necromancer::updateModuleBlocking() {
    getModuleManager().forEach([](std::shared_ptr<Module> mod) {
        if (mod->isBlocked()) mod->setBlocked(false);
    });
}

std::string Necromancer::getBuildTimestamp() {
#if defined(NECROMANCER_BUILD_TIMESTAMP)
    return NECROMANCER_BUILD_TIMESTAMP;
#else
    return NecromancerBuild::getTimestamp();
#endif
}

std::wstring Necromancer::GetCurrentModuleFilePath(HMODULE hModule) {
    std::vector<wchar_t> buffer(MAX_PATH);

    DWORD result = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));

    if (result > 0 && result < buffer.size()) {
        return std::wstring(buffer.data());
    } else if (result >= buffer.size()) {
        buffer.resize(result + 1);
        result = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (result > 0 && result < buffer.size()) {
            return std::wstring(buffer.data());
        }
    }

    return std::wstring(L"couldn't get file path");
}

void Necromancer::initSettings() {
    {
        auto set = std::make_shared<Setting>("menuKey", LocalizeString::get("client.settings.menuKey.name"),
                                             LocalizeString::get("client.settings.menuKey.desc"));
        set->value = &this->menuKey;
        set->callback = [this](Setting& set) {
            Necromancer::getScreenManager().get<ClickGUI>().key = this->getMenuKey();
        };
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("secondaryMenuKey", LocalizeString::get("client.settings.secondaryMenuKey.name"),
                                             LocalizeString::get("client.settings.secondaryMenuKey.desc"));
        set->value = &this->secondaryMenuKey;
        set->callback = [this](Setting& set) {
            Necromancer::getScreenManager().get<HUDEditor>().key2 = this->getSecondaryMenuKey();
            Necromancer::getScreenManager().get<ClickGUI>().key2 = this->getSecondaryMenuKey();
        };
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("ejectKey", LocalizeString::get("client.settings.ejectKey.name"),
                                             LocalizeString::get("client.settings.ejectKey.desc"));
        set->value = &this->ejectKey;
        this->getSettings().addSetting(set);
    }
    {
        auto set =
            std::make_shared<Setting>("menuBlurEnabled", LocalizeString::get("client.settings.menuBlurEnabled.name"),
                                      LocalizeString::get("client.settings.menuBlurEnabled.desc"));
        set->value = &this->menuBlurEnabled;
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("useDX11", LocalizeString::get("client.settings.useDX11.name"),
                                             LocalizeString::get("client.settings.useDX11.desc"));
        set->value = &this->useDX11;
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("forceDisableVSync",
                                             LocalizeString::get("client.settings.forceDisableVSync.name"),
                                             LocalizeString::get("client.settings.forceDisableVSync.desc"));
        set->value = &this->forceDisableVSync;
        set->callback = [this](Setting&) {
            DXHooks::CheckForceDisableVSync();
        };
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("commandPrefix", LocalizeString::get("client.settings.commandPrefix.name"),
                                             LocalizeString::get("client.settings.commandPrefix.desc"));
        set->value = &this->commandPrefix;
        set->visible = false;
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("menuIntensity", LocalizeString::get("client.settings.menuIntensity.name"),
                                             LocalizeString::get("client.settings.menuIntensity.desc"));
        set->value = &this->menuBlur;
        set->min = FloatValue(1.f);
        set->max = FloatValue(30.f);
        set->interval = FloatValue(1.f);
        this->getSettings().addSetting(set);
    }
    {
        auto set = std::make_shared<Setting>("accentColor", LocalizeString::get("client.settings.accentColor.name"),
                                             LocalizeString::get("client.settings.accentColor.desc"));
        set->value = &this->accentColor;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("minViewBob", L"Minimal View Bob (UNSTABLE)",
                                             L"Only bob the item in hand, not the camera");
        set->value = &this->minimalViewBob;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("textShadow", LocalizeString::get("client.settings.textShadow.name"),
                                            LocalizeString::get("client.settings.textShadow.desc"));
        set->value = &this->textShadow;
        this->getSettings().addSetting(set);
    }

#ifdef NECROMANCER_DEBUG
    {
        auto set = std::make_shared<Setting>("debugTextRects", L"Debug Text Rects",
                                             L"Draw text bounds and highlight likely text overflow.");
        set->value = &this->debugTextRects;
        this->getSettings().addSetting(set);
    }
#endif

    {
        auto set = std::make_shared<Setting>("secondaryFont", LocalizeString::get("client.settings.secondaryFont.name"),
                                             LocalizeString::get("client.settings.secondaryFont.desc"));
        set->value = &this->secondaryFont;
        this->getSettings().addSetting(set);
    }

    {
        // auto set = std::make_shared<Setting>("broadcastClientUsage", "Necromancer Client Presence", "If you leave this on,
        // others with Necromancer will see that you are using Necromancer and you will see other people who use Necromancer.");
        // set->value = &this->broadcastUsage;
        // this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>(""
                                             "centerCursor",
                                             LocalizeString::get("client.settings.centerCursor.name"),
                                             LocalizeString::get("client.settings.centerCursor.desc"));
        set->value = &this->centerCursorMenus;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("snapLines", LocalizeString::get("client.settings.snapLines.name"),
                                             LocalizeString::get("client.settings.snapLines.desc"));
        set->value = &this->snapLines;
        this->getSettings().addSetting(set);
    }

    {
        auto set = std::make_shared<Setting>("rgbSpeed", LocalizeString::get("client.settings.rgbSpeed.name"),
                                             LocalizeString::get("client.settings.rgbSpeed.desc"));
        set->value = &this->rgbSpeed;
        set->min = FloatValue(0.f);
        set->max = FloatValue(3.f);
        set->interval = FloatValue(0.1f);
        this->getSettings().addSetting(set);
    }
}

void Necromancer::queueForUIRender(std::function<void(SDK::MinecraftUIRenderContext* ctx)> callback) {
    this->uiRenderQueue.push(callback);
}

void Necromancer::queueForClientThread(std::function<void()> callback) {
    this->clientThreadQueue.push(callback);
}

void Necromancer::queueForDXRender(std::function<void(ID2D1DeviceContext* ctx)> callback) {
    this->dxRenderQueue.push(callback);
}

void Necromancer::deferD2DResourceRelease(IUnknown* resource) noexcept {
    if (!resource) {
        return;
    }

    std::lock_guard lock(this->deferredD2DReleaseMutex);
    this->deferredD2DReleases.push_back(resource);
}

void Necromancer::releaseDeferredD2DResources() noexcept {
    std::vector<IUnknown*> resources;
    {
        std::lock_guard lock(this->deferredD2DReleaseMutex);
        resources.swap(this->deferredD2DReleases);
    }

    for (auto* resource : resources) {
        resource->Release();
    }
}

void Necromancer::initL10n() {
    l10nData = LocalizeData();
}

void Necromancer::initLanguageSetting() {
    auto set = std::make_shared<Setting>("language", LocalizeString::get("client.settings.language.name"),
                                         LocalizeString::get("client.settings.language.desc"));
    set->enumData = &this->clientLanguage;
    set->value = set->enumData->getValue();
    set->userUpdateCallback = [](Setting&) {
        Necromancer::get().onLanguageChanged();
    };

    set->enumData->addEntry({ LocalizeData::systemDefaultLanguageSettingValue,
                              LocalizeString::get("client.settings.language.systemDefault.name") });

    for (int i = 0; auto& lang : l10nData->getLanguages()) {
        set->enumData->addEntry({ i + 1, util::StrToWStr(lang->name) });
        i++;
    }
    this->getSettings().addSetting(set);

    this->hudFontMode.addEntry({ 0, LocalizeString::get("client.module.font.client.name") });
    this->hudFontMode.addEntry({ 1, LocalizeString::get("client.module.font.minecraft.name") });
    this->hudFontMode.addEntry({ 2, LocalizeString::get("client.settings.mcRendererFont.notoSans.name") });
    this->hudFontMode.setSelectedKey(0);
}

void Necromancer::onLanguageChanged() {
    Necromancer::getRenderer().refreshTextFormats();
    Necromancer::getSettings().refreshLocalization();

    Necromancer::getModuleManager().forEach([](std::shared_ptr<Module> mod) {
        mod->refreshLocalization();
    });

    Necromancer::getCommandManager().refreshLocalization();
    Necromancer::getScreenManager().get<ClickGUI>().requestModuleListRebuild();
    Necromancer::getScreenManager().get<ClickGUI>().refreshLocalization();
}

void Necromancer::onUpdate(Event&) {
    timings.update();
    auto now = std::chrono::system_clock::now();
    static auto lastSend = now;

    while (!this->clientThreadQueue.empty()) {
        auto& latest = this->clientThreadQueue.front();
        latest();
        this->clientThreadQueue.pop();
    }

    if (this->shouldEject.load(std::memory_order_acquire)) {
        if (!this->mainThreadEjectCleanupComplete.load(std::memory_order_acquire)) {
            // Both of these touch game and config state owned by this thread,
            // so they must not be moved onto the eject thread.
            Necromancer::getScreenManager().exitCurrentScreen();
            Necromancer::getConfigManager().saveCurrentConfig();
            this->mainThreadEjectCleanupComplete.store(true, std::memory_order_release);
            Logger::Info("Eject: main thread cleanup done");
        }
        return;
    }

    if (std::get<BoolValue>(centerCursorMenus) && SDK::ClientInstance::get()->minecraftGame->isCursorGrabbed()) {
        RECT r = { 0, 0, 0, 0 };
        GetClientRect(SDK::GameCore::get()->hwnd, &r);
        SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    }

    necromancerUsers = necromancerUsersDirty;

    if (!hasInit) {
        threadsafeInit();
        hasInit = true;
    }
    getKeyboard().findTextInput();

    static bool lastDX11 = std::get<BoolValue>(this->useDX11);
    if (std::get<BoolValue>(useDX11) != lastDX11) {
        if (lastDX11) {
            Necromancer::getClientMessageQueue().display(
                util::WFormat(LocalizeString::get("client.settings.dx11EnabledMsg.name")));
        } else {
            Necromancer::getRenderer().setShouldReinit();
        }
        lastDX11 = std::get<BoolValue>(useDX11);
    }

    rgbHue += SDK::ClientInstance::get()->minecraft->timer->alpha * 0.005f * std::get<FloatValue>(rgbSpeed);
    if (rgbHue > 1.f) {
        rgbHue = 0.f;
    }
}

void Necromancer::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    const int key = ev.getKey();
    const bool down = ev.isDown();
    if (key < 0 || key >= static_cast<int>(pressedKeys.size())) {
        return;
    }
    const bool wasDown = pressedKeys[key];
    pressedKeys[key] = down;

    if (key == std::get<KeyValue>(ejectKey) && down && !wasDown &&
        !Necromancer::getScreenManager().get<ClickGUI>().hasActiveSetting()) {
        this->queueEject();
        Logger::Info("Uninject key pressed");

        ev.setCancelled();
        return;
    }

    if (down) {
        for (auto& tb : textBoxes) {
            if (tb->isSelected()) {
                tb->onKeyDown(key);
            }
        }
    }
}

void Necromancer::onClick(Event& evGeneric) {
    auto& ev = reinterpret_cast<ClickEvent&>(evGeneric);
    timings.onClick(ev.getMouseButton(), ev.isDown());
}

void Necromancer::onChar(Event& evGeneric) {
    auto& ev = reinterpret_cast<CharEvent&>(evGeneric);
    for (auto tb : textBoxes) {
        if (tb->isSelected()) {
            if (ev.isChar()) {
                tb->onChar(ev.getChar());
            } else {
                auto ch = ev.getChar();
                switch (ch) {
                case 0x1:
                    util::SetClipboardText(tb->getText());
                    break;
                case 0x2:
                    tb->setSelected(false);
                    break;
                case 0x3:
                    tb->reset();
                    break;
                default:
                    break;
                }
            }
            ev.setCancelled(true);
        }
    }
}

void Necromancer::onRendererInit(Event&) {
    getAssets().unloadAll(); // should be safe even if we didn't load resources yet
    getAssets().loadAll();

    applyHUDFontFamily();

    this->hudBlurBitmap = getRenderer().getCopiedBitmap();
    getRenderer().getDeviceContext()->CreateEffect(CLSID_D2D1GaussianBlur, gaussianBlurEffect.GetAddressOf());

    gaussianBlurEffect->SetInput(0, hudBlurBitmap.Get());
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
                                 std::get<FloatValue>(this->hudBlurIntensity));
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    gaussianBlurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);

    getRenderer().getDeviceContext()->CreateBitmapBrush(hudBlurBitmap.Get(), this->hudBlurBrush.GetAddressOf());
}

void Necromancer::onRendererCleanup(Event& ev) {
    this->hudBlurBitmap = nullptr;
    this->gaussianBlurEffect = nullptr;
    this->hudBlurBrush = nullptr;
}

void Necromancer::onSuspended(Event& ev) {
    Necromancer::getConfigManager().saveCurrentConfig();
    Logger::Info("Saved config");
}

void Necromancer::onBobView(Event& ev) {
    if (std::get<BoolValue>(this->minimalViewBob)) {
        reinterpret_cast<Cancellable&>(ev).setCancelled(true);
    }
}

void Necromancer::onLeaveGame(Event& ev) {
    getRenderer().clearTextCache();
}

void Necromancer::onRenderLayer(Event& evG) {
    auto& ev = reinterpret_cast<RenderLayerEvent&>(evG);
    while (!this->uiRenderQueue.empty()) {
        auto& latest = this->uiRenderQueue.front();
        latest(ev.getUIRenderContext());
        this->uiRenderQueue.pop();
    }
}

void Necromancer::onRenderOverlay(Event& evG) {
    auto& ev = reinterpret_cast<RenderOverlayEvent&>(evG);

    this->releaseDeferredD2DResources();

    std::wstring wantSecondary = this->hudFontMode.getSelectedKey() == 1 &&
                                         getRenderer().hasEmbeddedMinecraftFont()
                                     ? EmbeddedFontCollection::familyName()
                                     : std::get<TextValue>(secondaryFont).str;
    if (getRenderer().getFontFamily2() != wantSecondary) {
        getRenderer().updateSecondaryFont(wantSecondary);
    }

    while (!this->dxRenderQueue.empty()) {
        auto& latest = this->dxRenderQueue.front();
        latest(ev.getDeviceContext());
        this->dxRenderQueue.pop();
    }
}

void Necromancer::onPacketReceive(Event&) {
    // disabled
}

void Necromancer::onTick(Event& ev) {
    updateModuleBlocking();
}

void Necromancer::onMouseRelease(Event& ev) {
    if (std::get<BoolValue>(centerCursorMenus)) {
        RECT r = { 0, 0, 0, 0 };
        GetClientRect(SDK::GameCore::get()->hwnd, &r);
        SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    }
}

void Necromancer::loadLanguageConfig(std::shared_ptr<Setting> languageSetting) {
    this->getSettings().forEach([&](std::shared_ptr<Setting> set) {
        if (set->name() == languageSetting->name()) {
            std::visit(
                [&](auto&& obj) {
                    *set->value = obj;
                    set->update();
                },
                languageSetting->resolvedValue);
        }
    });
}

void Necromancer::loadConfig(SettingGroup& gr) {
    gr.forEach([&](std::shared_ptr<Setting> set) {
        this->getSettings().forEach([&](std::shared_ptr<Setting> modSet) {
            if (modSet->name() == set->name()) {
                std::visit(
                    [&](auto&& obj) {
                        *modSet->value = obj;
                        if (modSet->value->index() != (size_t)Setting::Type::Button) modSet->update();
                    },
                    set->resolvedValue);
            }
        });
    });
}

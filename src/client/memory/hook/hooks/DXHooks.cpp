#include "DXHooks.h"
#include "client/Necromancer.h"
#include "client/render/Renderer.h"
#include "mc/common/client/game/GameCore.h"
#include "mc/common/client/game/Options.h"
#include "pch.h"

namespace {
    typedef HRESULT(WINAPI* CreateSwapChainForHWND_t)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                      IDXGISwapChain1**);

    bool tearingSupported = false;
    bool isForceDisableVSync = false;
    std::shared_ptr<Hook> PresentHook;
    std::shared_ptr<Hook> FullscreenHook;
    std::shared_ptr<Hook> ResizeBuffersHook;
    std::shared_ptr<Hook> ResizeBuffers3Hook;
    std::shared_ptr<Hook> ExecuteCommandListsHook;

    IDXGISwapChain* presentTearingChain = nullptr;
    bool presentTearingCached = false;
    bool presentTearingDirty = true;
}

void DXHooks::CheckForceDisableVSync() {
    if (Necromancer::get().shouldForceDisableVSync()) {
        isForceDisableVSync = true;
    } else {
        isForceDisableVSync = false;
    }
    presentTearingDirty = true;
}

void DXHooks::CheckTearingSupport() {
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory5)))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                                                    sizeof(allowTearing)))) {
            tearingSupported = allowTearing != FALSE;
        }
    }
}

CreateSwapChainForHWND_t origCreateSwapChain = nullptr;

HRESULT WINAPI DXHooks::CreateSwapChainForHWNDHook(IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
                                                   const DXGI_SWAP_CHAIN_DESC1* desc,
                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                   IDXGIOutput* output, IDXGISwapChain1** swapChain) {
    if (device) {
        ComPtr<ID3D12CommandQueue> queue;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue))) && queue) {
            auto lock = Necromancer::getRenderer().lock();
            Necromancer::getRenderer().setCommandQueue(queue.Get());
        }
    }

    DXGI_SWAP_CHAIN_DESC1 modifiedDesc = *desc;
    if (tearingSupported && isForceDisableVSync) {
        modifiedDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    return origCreateSwapChain(factory, device, hwnd, &modifiedDesc, pFullscreenDesc, output, swapChain);
}

HRESULT __stdcall DXHooks::SwapChain_Present(IDXGISwapChain* chain, UINT SyncInterval, UINT Flags) {
    try {
        auto& renderer = Necromancer::getRenderer();
        Necromancer::get().setRenderHookActive(true);
        if (Necromancer::get().isEjectReadyForRenderThread()) {
            auto lock = renderer.lock();
            Necromancer::get().completeEjectFromRenderThread();
        } else if (!renderer.isResizeInProgress()) {
            auto lock = renderer.lock();
            if (!renderer.isResizeInProgress()) {
                if (renderer.hasInitialized()) {
                    renderer.render();
                } else {
                    renderer.init(chain);
                }
            }
        }
    } catch (...) {
        Necromancer::get().setRenderHookActive(false);
        return PresentHook->oFunc<decltype(&SwapChain_Present)>()(chain, SyncInterval, Flags);
    }

    // static bool hasKilled = false;
    // if (!hasKilled) {
    //	ComPtr<ID3D12Device> d3d12Device;
    //	chain->GetDevice(IID_PPV_ARGS(&d3d12Device));
    //	if (d3d12Device) ((ID3D12Device5*)d3d12Device.Get())->RemoveDevice(); // kill dx
    //	hasKilled = true;
    // }

    UINT presentFlags = Flags;
    UINT syncInterval = SyncInterval;
    if (Necromancer::get().shouldForceDisableVSync()) {
        syncInterval = 0;
        if (tearingSupported) {
            if (presentTearingDirty || presentTearingChain != chain) {
                presentTearingChain = chain;
                presentTearingDirty = false;
                presentTearingCached = false;
                ComPtr<IDXGISwapChain1> chain1;
                if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&chain1))) && chain1) {
                    DXGI_SWAP_CHAIN_DESC1 desc1 = {};
                    if (SUCCEEDED(chain1->GetDesc1(&desc1)) &&
                        (desc1.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)) {
                        presentTearingCached = true;
                    }
                }
            }
            if (presentTearingCached) {
                presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
            }
        }
    }

    const HRESULT result = PresentHook->oFunc<decltype(&SwapChain_Present)>()(chain, syncInterval, presentFlags);
    Necromancer::get().setRenderHookActive(false);
    return result;
}

HRESULT __stdcall DXHooks::SwapChain_SetFullscreenState(IDXGISwapChain* chain, BOOL Fullscreen, IDXGIOutput* pTarget) {
    bool resizeStarted = false;
    try {
        Necromancer::getRenderer().beginResize();
        resizeStarted = true;
    } catch (...) {
    }

    const HRESULT result =
        FullscreenHook->oFunc<decltype(&SwapChain_SetFullscreenState)>()(chain, Fullscreen, pTarget);

    if (resizeStarted) {
        try {
            Necromancer::getRenderer().endResize();
        } catch (...) {
        }
    }
    return result;
}

HRESULT __stdcall DXHooks::SwapChain_ResizeBuffers(IDXGISwapChain* chain, UINT BufferCount, UINT Width, UINT Height,
                                                   DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    presentTearingDirty = true;
    bool resizeStarted = false;
    try {
        Necromancer::getRenderer().beginResize();
        resizeStarted = true;
    } catch (...) {
    }
    UINT newFlags = SwapChainFlags;
    if (tearingSupported && isForceDisableVSync) {
        newFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    const HRESULT result = ResizeBuffersHook->oFunc<decltype(&SwapChain_ResizeBuffers)>()(chain, BufferCount, Width,
                                                                                          Height, NewFormat, newFlags);
    if (FAILED(result)) {
        Logger::Warn("ResizeBuffers failed: hr=0x{:08X} w={} h={} bufs={}", static_cast<unsigned>(result), Width, Height, BufferCount);
    }
    if (resizeStarted) {
        try {
            Necromancer::getRenderer().endResize();
        } catch (...) {
        }
    }
    return result;
}

HRESULT __stdcall DXHooks::SwapChain3_ResizeBuffers(IDXGISwapChain* chain, UINT BufferCount, UINT Width, UINT Height,
                                                    DXGI_FORMAT NewFormat, UINT SwapChainFlags,
                                                    const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue) {
    presentTearingDirty = true;
    bool resizeStarted = false;
    try {
        Necromancer::getRenderer().beginResize();
        resizeStarted = true;
    } catch (...) {
    }
    UINT newFlags = SwapChainFlags;
    if (tearingSupported && isForceDisableVSync) {
        newFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    const HRESULT result = ResizeBuffers3Hook->oFunc<decltype(&SwapChain3_ResizeBuffers)>()(
        chain, BufferCount, Width, Height, NewFormat, newFlags, pCreationNodeMask, ppPresentQueue);
    if (FAILED(result)) {
        Logger::Warn("ResizeBuffers1 failed: hr=0x{:08X} w={} h={} bufs={}", static_cast<unsigned>(result), Width, Height, BufferCount);
    }
    if (resizeStarted) {
        try {
            Necromancer::getRenderer().endResize();
        } catch (...) {
        }
    }
    return result;
}

HRESULT __stdcall DXHooks::CommandQueue_ExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists,
                                                            ID3D12CommandList* const* ppCommandLists) {
    if (queue) {
        auto desc = queue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            {
                auto lock = Necromancer::getRenderer().lock();
                Necromancer::getRenderer().setCommandQueue(queue);
            }
        }
    }

    return ExecuteCommandListsHook->oFunc<decltype(&CommandQueue_ExecuteCommandLists)>()(queue, NumCommandLists,
                                                                                         ppCommandLists);
}

DXHooks::DXHooks()
    : HookGroup("DirectX") {
    ComPtr<IDXGIFactory> factory;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D11DeviceContext> dctx;
    ComPtr<ID3D12CommandQueue> cqueue;

    DXHooks::CheckForceDisableVSync();
    DXHooks::CheckTearingSupport();

    ThrowIfFailed(CreateDXGIFactory(IID_PPV_ARGS(&factory)));
    ThrowIfFailed(factory->EnumAdapters(0, adapter.GetAddressOf()));

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = 100;
    swapChainDesc.BufferDesc.Height = 100;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    WNDCLASSEXW wnd { 0 };
    ZeroMemory(&wnd, sizeof(WNDCLASSEX));

    wnd.cbSize = sizeof(WNDCLASSEX);
    wnd.hInstance = Necromancer::get().dllInst;
    wnd.lpszClassName = L"dummywnd";
    wnd.lpfnWndProc = DefWindowProc;
    wnd.lpszMenuName = 0;
    wnd.style = CS_SAVEBITS | CS_DROPSHADOW;

    RegisterClassExW(&wnd);

    HWND hWnd = CreateWindowExW(0, L"dummywnd", L"hi", WS_MINIMIZEBOX, 0, 0, 100, 100, nullptr, nullptr,
                                Necromancer::get().dllInst, nullptr);

    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE;

    D3D_FEATURE_LEVEL lvl[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };

    D3D_FEATURE_LEVEL featureLevel;
    ThrowIfFailed(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, lvl, 2,
                                                 D3D11_SDK_VERSION, &swapChainDesc, swapChain.GetAddressOf(),
                                                 device.GetAddressOf(), &featureLevel, dctx.GetAddressOf()));

    uintptr_t* vftable = *reinterpret_cast<uintptr_t**>(swapChain.Get());
    uintptr_t* cqueueVftable = nullptr;

    {
        // DX12 only
        //
        //  dummy device
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12)))) {
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

            ThrowIfFailed(device12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cqueue)));
            cqueueVftable = *reinterpret_cast<uintptr_t**>(cqueue.Get());
        }
    }

    DestroyWindow(hWnd);
    UnregisterClassW(L"dummywnd", Necromancer::get().dllInst);

    ComPtr<IDXGIFactory2> factory2;
    if (SUCCEEDED(factory.As(&factory2))) {
        void** vtable = *(void***)factory2.Get();
        MH_CreateHook(vtable[15], DXHooks::CreateSwapChainForHWNDHook, (void**)&origCreateSwapChain);
        MH_EnableHook(vtable[15]);
    }

    PresentHook = addHook(vftable[8], SwapChain_Present, "IDXGISwapChain::Present");

    FullscreenHook = addHook(vftable[10], SwapChain_SetFullscreenState, "IDXGISwapChain::SetFullscreenState");

    // We need both ResizeBuffers hooks as DX11 uses IDXGISwapChain::ResizeBuffers and DX12 uses
    // IDXGISwapChain3::ResizeBuffers
    ResizeBuffersHook = addHook(vftable[13], SwapChain_ResizeBuffers, "IDXGISwapChain::ResizeBuffers");
    ResizeBuffers3Hook = addHook(vftable[39], SwapChain3_ResizeBuffers, "IDXGISwapChain3::ResizeBuffers");

    // Needed for D3D11On12 for DX12
    if (cqueueVftable)
        ExecuteCommandListsHook =
            addHook(cqueueVftable[10], CommandQueue_ExecuteCommandLists, "ID3D12CommandQueue::executeCommandLists");
    PresentHook->enable();
    FullscreenHook->enable();
    ResizeBuffersHook->enable();
    ResizeBuffers3Hook->enable();
    if (cqueueVftable) ExecuteCommandListsHook->enable();
}

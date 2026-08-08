#include "pch.h"
#include "Keyboard.h"
#include "client/event/events/CharEvent.h"
#include "client/event/events/FocusLostEvent.h"
#include "mc/common/client/input/ClientInputHandler.h"
#include "mc/common/client/game/GameCore.h"
#include <chrono>
#include <client/Necromancer.h>

Keyboard::Keyboard(int* gameKeyMap)
    : keyMap(gameKeyMap) {
    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&Keyboard::onKey, 4);
    Eventing::get().listen<FocusLostEvent>(this, (EventListenerFunc)&Keyboard::onFocusLost, 4);
}

// TODO: better method
void Keyboard::findTextInput() {
    bool backspaceHeld = keyMapAdjusted['\b'];
    static std::chrono::high_resolution_clock::time_point backspaceStart = std::chrono::high_resolution_clock::now();
    if (!backspaceHeld) {
        backspaceStart = std::chrono::high_resolution_clock::now();
    }
    auto timeNow = std::chrono::high_resolution_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeNow - backspaceStart).count();

    const SHORT capsState = GetKeyState(VK_CAPITAL);
    const BYTE capsByte = static_cast<BYTE>(((capsState & 0x8000) != 0 ? 0x80 : 0) | (capsState & 0x1));

    for (int i = 0; i < 256; i++) {
        winKeyMap[i] = static_cast<BYTE>(keyMapAdjusted[i]);
        if (i == VK_SHIFT) {
            winKeyMap[i] = keyMapAdjusted[i] ? 0x80 : 0x0;
        }
        winKeyMap[VK_CAPITAL] = capsByte;

        bool isDown = keyMapAdjusted[i];
        bool oldIsDown = keyMapOld[i];

        if (isDown && !oldIsDown && i != VK_ESCAPE) {
            if (keyMapAdjusted[VK_RETURN]) {
                onChar((char)2, false);
            }
            if (keyMapAdjusted[VK_CONTROL]) {
                if (i == 'V') {
                    std::wstring str = util::GetClipboardText();
                    for (wchar_t ch : str) {
                        onChar(ch);
                    }
                } else if (i == 'C') {
                    onChar((wchar_t)1, false);
                } else if (i == 'A') {
                    onChar(wchar_t(3), false);
                }
            } else {
                uint64_t dwChars = 0;
                DWORD dwScanCode = 0;
                (reinterpret_cast<WORD*>(&dwScanCode))[1] = static_cast<WORD>(keyMapAdjusted[i] != 0);
                int res = ToUnicode(i, dwScanCode, winKeyMap, (LPWSTR)&dwChars, 4, 0);
                if (res > 0) {
                    wchar_t* chars = reinterpret_cast<wchar_t*>(&dwChars);
                    onChar(chars[0]);

                    if (res == 2) { // If there's an extra character
                        onChar(chars[1]);
                    }
                }
            }
        }
    }

    if (ms > 350) {
        onChar('\b');
    }

    memcpy(keyMapOld, keyMapAdjusted, 0x100 * sizeof(int));
}

bool Keyboard::isKeyDown(int vKey) {
    if (vKey <= 0 || vKey >= 0x100) return false;
    if (vKey <= VK_XBUTTON2) return keyMapAdjusted[vKey] != 0;
    return keyMap[vKey] != 0;
}

bool Keyboard::isKeyDownHardware(int vKey) {
    if (vKey <= 0 || vKey >= 0x100) return false;

    auto gameCore = SDK::GameCore::get();
    if (!gameCore || !gameCore->hwnd) return false;

    HWND foreground = GetForegroundWindow();
    if (foreground != gameCore->hwnd && GetAncestor(foreground, GA_ROOT) != gameCore->hwnd) return false;

    if (vKey == VK_LBUTTON || vKey == VK_RBUTTON) {
        if (GetSystemMetrics(SM_SWAPBUTTON)) vKey = vKey == VK_LBUTTON ? VK_RBUTTON : VK_LBUTTON;
    }

    return (GetAsyncKeyState(vKey) & 0x8000) != 0;
}

int Keyboard::getMappedKey(std::string const& name) {
    return SDK::ClientInstance::get()->inputHandler->mappingFactory->defaultKeyboardLayout->findValue(name);
}

bool Keyboard::isMovementKey(int key) {
    struct Bind {
        const char* name;
        int fallback;
    };
    static const Bind binds[] = {
        { "forward", 'W' }, { "back", 'S' }, { "left", 'A' },
        { "right", 'D' },   { "jump", VK_SPACE }, { "sneak", VK_SHIFT },
    };

    for (const auto& b : binds) {
        int mapped = getMappedKey(b.name);
        if (mapped == 0) mapped = b.fallback;
        if (key == mapped) return true;
    }
    return false;
}

void Keyboard::releaseMovementKeys() {
    struct Bind {
        const char* name;
        int fallback;
    };
    static const Bind binds[] = {
        { "forward", 'W' }, { "back", 'S' }, { "left", 'A' },
        { "right", 'D' },   { "jump", VK_SPACE }, { "sneak", VK_SHIFT },
    };

    for (const auto& b : binds) {
        int mapped = getMappedKey(b.name);
        if (mapped == 0) mapped = b.fallback;
        if (mapped >= 0 && mapped < 0x100) {
            keyMapAdjusted[mapped] = 0;
            if (keyMap) keyMap[mapped] = 0;
        }
    }
}

void Keyboard::onChar(wchar_t ch, bool isChar) {
    {
        CharEvent ev { ch, isChar };
        Eventing::get().dispatch(ev);
    }
}

void Keyboard::onKey(Event& evGeneric) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evGeneric);
    this->keyMapAdjusted[ev.getKey()] = ev.isDown();
}

void Keyboard::onFocusLost(Event&) {
    memset(keyMapAdjusted, 0, sizeof(keyMapAdjusted));
}

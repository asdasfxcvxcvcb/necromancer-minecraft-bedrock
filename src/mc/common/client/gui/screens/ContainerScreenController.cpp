#include "pch.h"
#include "ContainerScreenController.h"
#include "mc/Addresses.h"
#include "mc/common/world/ItemStack.h"
#include "util/Logger.h"
#include <unordered_map>
#include <vector>

namespace {
    struct SlotInfo {
        std::string collection;
        int slot;
    };
    static_assert(sizeof(SlotInfo) == 40, "SlotInfo must match the game's 40 byte layout");

    struct DstCollection {
        std::string name;
        unsigned char stopWhenSatisfied;
    };
    static_assert(sizeof(DstCollection) == 40, "DstCollection must match the game's 40 byte layout");

    int resolveVtableIndex(void* thisPtr, uintptr_t funcAddr, int fallback, const char* what) {
        if (!funcAddr) {
            Logger::Warn("[ChestStealer] {} : signature not resolved, using fallback index {}", what, fallback);
            return fallback;
        }
        auto vtable = *reinterpret_cast<uintptr_t**>(thisPtr);
        if (!vtable) return fallback;

        static std::unordered_map<uintptr_t, int> cache;
        auto key = reinterpret_cast<uintptr_t>(vtable) ^ (funcAddr << 1);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        int idx = fallback;
        bool found = false;
        for (int i = 0; i < 128; i++) {
            if (vtable[i] == funcAddr) {
                idx = i;
                found = true;
                break;
            }
        }
        cache.emplace(key, idx);
        if (found) {
            Logger::Info("[ChestStealer] {} : vtable {:X} -> index {}", what, reinterpret_cast<uintptr_t>(vtable), idx);
        } else {
            uintptr_t fallbackPtr = vtable[fallback];
            Logger::Warn("[ChestStealer] {} : func {:X} not found in vtable {:X}, using fallback index {} (holds {:X})",
                         what, funcAddr, reinterpret_cast<uintptr_t>(vtable), fallback, fallbackPtr);
        }
        return idx;
    }

    bool sehReadQword(void* p, uintptr_t& out) {
        __try {
            out = *reinterpret_cast<uintptr_t*>(p);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            out = 0;
            return false;
        }
    }

    SDK::ItemStack* sehGetSlot(uintptr_t fn, void* mgr, const std::string* coll, int slot) {
        __try {
            return reinterpret_cast<SDK::ItemStack*(__fastcall*)(void*, const std::string&, int)>(fn)(mgr, *coll,
                                                                                                      slot);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    bool sehReadByte(void* p, int& out) {
        __try {
            out = *reinterpret_cast<unsigned char*>(p);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            out = -100;
            return false;
        }
    }

    int sehCallAutoPlace(void* mgr, int idx, const void* src, int count, const void* dstVec, void* outVec,
                         uintptr_t& target, unsigned long& exCode, uintptr_t& exAddr) {
        __try {
            using Fn = int(__fastcall*)(void*, const void*, int, const void*, void*);
            auto vtable = *reinterpret_cast<Fn**>(mgr);
            target = reinterpret_cast<uintptr_t>(vtable[idx]);
            return vtable[idx](mgr, src, count, dstVec, outVec);
        } __except (exCode = GetExceptionCode(),
                    exAddr = reinterpret_cast<uintptr_t>((GetExceptionInformation())->ExceptionRecord->ExceptionAddress),
                    EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    bool sehCallTransfer(void* mgr, int idx, const void* src, const void* dstVec, const void* srcColl, bool& out) {
        __try {
            using Fn = bool(__fastcall*)(void*, const void*, const void*, const void*);
            out = (*reinterpret_cast<Fn**>(mgr))[idx](mgr, src, dstVec, srcColl);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            out = false;
            return false;
        }
    }

    bool sehCallTryExit(void* controller, int idx, int& request) {
        __try {
            request = memory::callVirtual<int>(controller, idx);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            request = -1;
            return false;
        }
    }

    int sehCallTakePlace(void* mgr, int idx, const void* dst, int count, const void* src, unsigned long& exCode,
                         uintptr_t& exAddr) {
        __try {
            using Fn = int(__fastcall*)(void*, const void*, int, const void*);
            auto vtable = *reinterpret_cast<Fn**>(mgr);
            exAddr = reinterpret_cast<uintptr_t>(vtable[idx]);
            return vtable[idx](mgr, dst, count, src);
        } __except (exCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }
}

void SDK::ContainerScreenController::_handleTakePlace(const std::string& viewName, int slot, bool b) {
    int idx = resolveVtableIndex(this, Signatures::ContainerScreenController_handleTakePlace.result,
                                 Signatures::VtableIndex::ContainerScreenController::handleTakePlace, "takePlace");
    memory::callVirtual<int>(this, idx, viewName, slot, b);
}

void* SDK::ContainerScreenController::_getSelectedSlotInfo() {
    return memory::callVirtual<void*>(
        this, Signatures::VtableIndex::ContainerScreenController::getSelectedSlotInfo);
}

void SDK::ContainerScreenController::handleAutoPlace(const std::string& collection, int slot) {
    int idx = resolveVtableIndex(this, Signatures::ContainerScreenController_handleAutoPlace.result,
                                 Signatures::VtableIndex::ContainerScreenController::handleAutoPlace, "autoPlace");
    memory::callVirtual<void, const std::string&, int>(this, idx, collection, slot);
}

SDK::ItemStack* SDK::ContainerScreenController::getItemStack(const std::string& collection, int slot) {
    if (!Signatures::ContainerManagerModel_getSlot.result) return nullptr;
    auto mgr = this->containerManager;
    if (!mgr) return nullptr;
    uintptr_t buckets = 0;
    if (!sehReadQword(reinterpret_cast<char*>(mgr) + 0x40, buckets) || !buckets) return nullptr;
    return sehGetSlot(Signatures::ContainerManagerModel_getSlot.result, mgr, &collection, slot);
}

void SDK::ContainerScreenController::autoPlaceSlot(const std::string& collection, int slot, int64_t count) {
    (void)count;
    auto mgr = this->containerManager;
    if (!mgr) return;
    uintptr_t buckets = 0;
    if (!sehReadQword(reinterpret_cast<char*>(mgr) + 0x40, buckets) || !buckets) return;

    SlotInfo src { collection, slot };
    static const std::vector<DstCollection> dstCollections = [] {
        std::vector<DstCollection> v;
        v.push_back({ "combined_hotbar_and_inventory_items", 0 });
        return v;
    }();
    alignas(8) unsigned char outFilled[96] = {};

    uintptr_t netMgr = 0;
    sehReadQword(reinterpret_cast<char*>(mgr) + 0xA0, netMgr);

    uintptr_t target = 0;
    unsigned long exCode = 0;
    uintptr_t exAddr = 0;
    int moved = sehCallAutoPlace(mgr, Signatures::VtableIndex::ContainerManagerModel::autoPlace, &src, 0x7FFFFFFF,
                                 &dstCollections, outFilled, target, exCode, exAddr);

    if (moved < 0) {
        Logger::Warn("[ChestStealer] autoPlace faulted for {}[{}] (target={:X} netMgr={:X} exCode={:X} exAddr={:X})",
                     collection, slot, target, netMgr, exCode, exAddr);
        return;
    }
    if (moved == 0) {
        Logger::Warn("[ChestStealer] autoPlace moved nothing for {}[{}] (target={:X} netMgr={:X})", collection, slot,
                     target, netMgr);
    } else {
        Logger::Info("[ChestStealer] autoPlace moved {} from {}[{}]", moved, collection, slot);
    }

    if (*reinterpret_cast<void**>(outFilled)) {
        static bool warnedLeak = false;
        if (!warnedLeak) {
            warnedLeak = true;
            Logger::Warn("[ChestStealer] out-slot vector allocated by the game is being leaked "
                         "(need a sig for its destructor at static 0x1402B3990 to release it safely)");
        }
    }
}

int SDK::ContainerScreenController::transferSlot(const std::string& srcColl, int srcSlot, const std::string& dstColl,
                                                 int dstSlot) {
    auto mgr = this->containerManager;
    if (!mgr) return 0;
    uintptr_t buckets = 0;
    if (!sehReadQword(reinterpret_cast<char*>(mgr) + 0x40, buckets) || !buckets) return 0;

    SlotInfo from { srcColl, srcSlot };
    SlotInfo to { dstColl, dstSlot };

    unsigned long exCode = 0;
    uintptr_t exAddr = 0;
    int moved = sehCallTakePlace(mgr, Signatures::VtableIndex::ContainerManagerModel::takePlace, &to, 0x7FFFFFFF,
                                 &from, exCode, exAddr);

    if (moved < 0) {
        Logger::Warn("[ChestStealer] transferSlot faulted for {}[{}] -> {}[{}] (exCode={:X} exAddr={:X})", srcColl,
                     srcSlot, dstColl, dstSlot, exCode, exAddr);
        return 0;
    }
    if (moved == 0) {
        Logger::Warn("[ChestStealer] transferSlot moved nothing for {}[{}] -> {}[{}]", srcColl, srcSlot, dstColl,
                     dstSlot);
    } else {
        Logger::Info("[ChestStealer] transferSlot moved {} from {}[{}] -> {}[{}]", moved, srcColl, srcSlot, dstColl,
                     dstSlot);
    }
    return moved;
}

bool SDK::ContainerScreenController::tryExit() {
    int idx = resolveVtableIndex(this, Signatures::MinecraftScreenController_tryExit.result,
                                 Signatures::VtableIndex::ContainerScreenController::tryExit, "tryExit");
    int request = 0;
    if (!sehCallTryExit(this, idx, request)) {
        Logger::Warn("[ChestStealer] tryExit faulted at vtable index {}", idx);
        return false;
    }
    Logger::Info("[ChestStealer] tryExit returned ViewRequest {}", request);
    return request != 0;
}

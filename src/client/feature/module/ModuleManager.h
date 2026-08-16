#pragma once
#include "client/manager/Manager.h"
#include "client/event/Event.h"
#include "client/event/Listener.h"
#include "Module.h"

class ModuleManager final : public Listener, public Manager<Module> {
public:
    ModuleManager();
    ~ModuleManager();

    static bool equalsIgnoreCase(std::string_view left, std::string_view right) {
        if (left.size() != right.size()) return false;
        for (size_t i = 0; i < left.size(); ++i) {
            unsigned char a = static_cast<unsigned char>(left[i]);
            unsigned char b = static_cast<unsigned char>(right[i]);
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) return false;
        }
        return true;
    }

    virtual std::shared_ptr<Module> find(std::string const& name) {
        std::string_view resolvedName = equalsIgnoreCase(name, "ForwardTrack") ? "AfterTrack" : std::string_view(name);
        for (auto& item : this->items) {
            if (equalsIgnoreCase(resolvedName, item->name())) return item;
        }
        return nullptr;
    }

    [[nodiscard]] size_t size() const { return items.size(); }

    void onKey(Event& ev);
};

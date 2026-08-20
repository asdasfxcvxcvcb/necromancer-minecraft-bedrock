#pragma once
#include "client/event/Listener.h"
#include "client/event/Event.h"
#include "ConditionGraph.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <vector>

struct KeybindEdit {
    std::string module;
    std::string setting;
    size_t valueType = 0;
    nlohmann::json value;
};

struct Keybind {
    std::string name;
    std::string parent;
    int key = 0;
    int type = 0;
    int kind = 0;
    bool hidden = false;
    bool showOnlyWhenActive = false;
    bool active = false;
    bool wasDown = false;
    bool gateWasOpen = true;
    std::vector<KeybindEdit> edits;
    bool applied = false;
    std::vector<nlohmann::json> previous;
    std::vector<nlohmann::json> appliedValues;
    ConditionGraph graph;
};

class KeybindManager : public Listener {
public:
    static constexpr int TypeToggle = 0;
    static constexpr int TypeHold = 1;

    static constexpr int KindNormal = 0;
    static constexpr int KindIf = 1;

    static KeybindManager& get();

    std::vector<Keybind>& getBinds() { return binds; }
    Keybind* findBind(std::string const& name);
    bool createBind(std::string name, int kind = KindNormal);
    bool renameBind(std::string const& oldName, std::string newName);
    bool deleteBind(std::string const& name);
    void setKey(std::string const& name, int key);
    void setType(std::string const& name, int type);
    void setHidden(std::string const& name, bool hidden);
    void setShowOnlyWhenActive(std::string const& name, bool showOnlyWhenActive);
    bool setParent(std::string const& name, std::string const& parent);
    bool canBeParentOf(std::string const& childName, std::string const& parentName);
    bool isGateOpen(Keybind const& bind);

    void markDirty() { dirty = true; }
    static std::wstring describeGraph(Keybind const& bind);

    void recordEdit(std::string const& bindName, std::string const& module, std::string const& setting,
                    size_t valueType, nlohmann::json value);
    void removeEdit(std::string const& bindName, std::string const& module, std::string const& setting);
    static KeybindEdit* findEdit(Keybind& bind, std::string const& module, std::string const& setting);
    static std::wstring describeEdit(KeybindEdit const& edit);
    static std::string settingOwnerModule(class Setting* set);

    bool isShown(Keybind const& bind) const { return !bind.hidden && (bind.active || !bind.showOnlyWhenActive); }

    void beginConfigSave();
    void endConfigSave();

    void onUpdate(Event& ev);
    void onTick(Event& ev);
    void onAttack(Event& ev);
    void onLeaveGame(Event& ev);

    nlohmann::json toJson() const;
    void fromJson(nlohmann::json const& j);

private:
    KeybindManager();
    ~KeybindManager() = default;

    void migrateLegacyFile();
    void persistNow();
    void sanitizeParents();

    void applyBind(Keybind& bind);
    void restoreBind(Keybind& bind);
    void deactivateBind(Keybind& bind);
    int gateDepth(Keybind const& bind);
    bool applyEdit(KeybindEdit const& edit, nlohmann::json const& value, nlohmann::json* outPrevious,
                   nlohmann::json* outEffective);
    bool readEditCurrent(KeybindEdit const& edit, nlohmann::json& out);
    class Setting* resolveEdit(KeybindEdit const& edit);

    std::vector<Keybind> binds;
    std::vector<std::pair<int, size_t>> evalOrder;
    std::vector<std::pair<class Setting*, nlohmann::json>> saveStash;
    bool dirty = false;
    std::chrono::steady_clock::time_point lastSave {};

    ConditionRuntime condRuntime;
    CondEvalContext condContext;
};

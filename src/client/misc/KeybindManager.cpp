#include "pch.h"
#include "KeybindManager.h"
#include "client/event/Eventing.h"
#include "client/event/events/UpdateEvent.h"
#include "client/event/events/TickEvent.h"
#include "client/event/events/AttackEvent.h"
#include "client/event/events/LeaveGameEvent.h"
#include "client/Necromancer.h"
#include "client/input/Keyboard.h"
#include "client/config/ConfigManager.h"
#include "client/feature/module/ModuleManager.h"
#include "client/feature/module/modules/visual/BlockESP.h"
#include "client/feature/setting/Setting.h"
#include "client/localization/LocalizeString.h"
#include <algorithm>
#include <iomanip>

namespace {
    bool equalsCI(std::string const& a, std::string const& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) {
            if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }

    bool jsonValuesEqual(nlohmann::json const& a, nlohmann::json const& b) {
        if (a.is_number() && b.is_number()) {
            return std::abs(a.get<double>() - b.get<double>()) < 1e-4;
        }
        if (a.is_object() && b.is_object()) {
            if (a.size() != b.size()) return false;
            for (auto it = a.begin(); it != a.end(); ++it) {
                auto other = b.find(it.key());
                if (other == b.end() || !jsonValuesEqual(it.value(), *other)) return false;
            }
            return true;
        }
        return a == b;
    }

    bool applyColorJson(ColorValue& out, nlohmann::json const& value) {
        if (!value.is_object() || !value.contains("color1") || !value["color1"].is_object()) return false;
        auto& c1 = value["color1"];
        if (!c1.contains("r") || !c1.contains("g") || !c1.contains("b") || !c1.contains("a")) return false;
        if (!c1["r"].is_number() || !c1["g"].is_number() || !c1["b"].is_number() || !c1["a"].is_number()) return false;
        out.color1.r = std::clamp(c1["r"].get<float>(), 0.f, 1.f);
        out.color1.g = std::clamp(c1["g"].get<float>(), 0.f, 1.f);
        out.color1.b = std::clamp(c1["b"].get<float>(), 0.f, 1.f);
        out.color1.a = std::clamp(c1["a"].get<float>(), 0.f, 1.f);
        if (value.contains("isRGB") && value["isRGB"].is_boolean()) out.isRGB = value["isRGB"].get<bool>();
        if (value.contains("forceTagColor") && value["forceTagColor"].is_boolean()) {
            out.forceTagColor = value["forceTagColor"].get<bool>();
        }
        return true;
    }

    Setting* findSettingForEdit(KeybindEdit const& edit) {
        SettingGroup* group = nullptr;
        if (edit.module == "global") {
            group = &Necromancer::getSettings();
        } else {
            auto mod = Necromancer::getModuleManager().find(edit.module);
            if (!mod) return nullptr;
            group = mod->settings.get();
            if (mod->name() == "BlockESP") {
                if (auto* entrySet = std::static_pointer_cast<BlockESP>(mod)->findEntrySetting(edit.setting))
                    return entrySet;
            }
        }
        Setting* found = nullptr;
        group->forEach([&](std::shared_ptr<Setting> set) {
            if (!found && set->name() == edit.setting) found = set.get();
        });
        return found;
    }

    bool writeRawEditValue(Setting* set, nlohmann::json const& value) {
        if (!set || !set->value) return false;
        switch (set->value->index()) {
        case (size_t)Setting::Type::Bool:
            if (!value.is_boolean()) return false;
            std::get<BoolValue>(*set->value).value = value.get<bool>();
            return true;
        case (size_t)Setting::Type::Float:
            if (!value.is_number()) return false;
            std::get<FloatValue>(*set->value).value = value.get<float>();
            return true;
        case (size_t)Setting::Type::Enum:
            if (!value.is_number()) return false;
            std::get<EnumValue>(*set->value).val = value.get<int>();
            return true;
        case (size_t)Setting::Type::Text:
            if (!value.is_string()) return false;
            std::get<TextValue>(*set->value).str = util::StrToWStr(value.get<std::string>());
            return true;
        case (size_t)Setting::Type::Color:
            return applyColorJson(std::get<ColorValue>(*set->value), value);
        default:
            return false;
        }
    }
}

KeybindManager& KeybindManager::get() {
    static auto* instance = new KeybindManager;
    return *instance;
}

KeybindManager::KeybindManager() {
    migrateLegacyFile();
    Eventing::get().listen<UpdateEvent>(this, (EventListenerFunc)&KeybindManager::onUpdate);
    Eventing::get().listen<TickEvent>(this, (EventListenerFunc)&KeybindManager::onTick);
    Eventing::get().listen<AttackEvent>(this, (EventListenerFunc)&KeybindManager::onAttack);
    Eventing::get().listen<LeaveGameEvent>(this, (EventListenerFunc)&KeybindManager::onLeaveGame);
}

void KeybindManager::onTick(Event&) {
    auto ci = SDK::ClientInstance::get();
    condRuntime.onTickSample(ci ? ci->getLocalPlayer() : nullptr);
}

void KeybindManager::onAttack(Event& ev) {
    condRuntime.onAttack(reinterpret_cast<AttackEvent&>(ev).getActor());
}

void KeybindManager::onLeaveGame(Event&) {
    condRuntime.reset();
    for (auto& bind : binds) bind.graph.resetRuntime();
}

void KeybindManager::persistNow() {
    dirty = false;
    lastSave = std::chrono::steady_clock::now();
    Necromancer::getConfigManager().saveCurrentConfig();
}

Keybind* KeybindManager::findBind(std::string const& name) {
    for (auto& bind : binds) {
        if (bind.name == name) return &bind;
    }
    return nullptr;
}

bool KeybindManager::createBind(std::string name, int kind) {
    if (name.empty()) return false;
    for (auto& bind : binds) {
        if (equalsCI(bind.name, name)) return false;
    }
    Keybind bind;
    bind.name = std::move(name);
    bind.kind = kind == KindIf ? KindIf : KindNormal;
    binds.push_back(std::move(bind));
    persistNow();
    return true;
}

bool KeybindManager::renameBind(std::string const& oldName, std::string newName) {
    auto* bind = findBind(oldName);
    if (!bind || newName.empty()) return false;
    for (auto& other : binds) {
        if (other.name != oldName && equalsCI(other.name, newName)) return false;
    }
    for (auto& other : binds) {
        if (other.parent == oldName) other.parent = newName;
    }
    bind->name = std::move(newName);
    persistNow();
    return true;
}

bool KeybindManager::deleteBind(std::string const& name) {
    auto it = std::find_if(binds.begin(), binds.end(), [&](Keybind const& b) { return b.name == name; });
    if (it == binds.end()) return false;
    if (it->applied) restoreBind(*it);

    std::string orphanParent = it->parent;
    binds.erase(it);

    for (auto& other : binds) {
        if (other.parent == name) other.parent = orphanParent;
    }

    persistNow();
    return true;
}

void KeybindManager::setKey(std::string const& name, int key) {
    auto* bind = findBind(name);
    if (!bind) return;
    bind->key = key;
    bind->wasDown = false;
    persistNow();
}

void KeybindManager::setType(std::string const& name, int type) {
    auto* bind = findBind(name);
    if (!bind) return;
    if (bind->kind == KindIf) return;
    if (bind->applied) restoreBind(*bind);
    bind->type = type == TypeHold ? TypeHold : TypeToggle;
    bind->active = false;
    bind->wasDown = false;
    persistNow();
}

void KeybindManager::setHidden(std::string const& name, bool hidden) {
    auto* bind = findBind(name);
    if (!bind) return;
    bind->hidden = hidden;
    persistNow();
}

void KeybindManager::setShowOnlyWhenActive(std::string const& name, bool showOnlyWhenActive) {
    auto* bind = findBind(name);
    if (!bind) return;
    bind->showOnlyWhenActive = showOnlyWhenActive;
    persistNow();
}

bool KeybindManager::canBeParentOf(std::string const& childName, std::string const& parentName) {
    if (parentName.empty()) return true;
    if (equalsCI(childName, parentName)) return false;
    if (!findBind(parentName)) return false;

    std::string cursor = parentName;
    for (size_t guard = 0; guard < binds.size() + 1 && !cursor.empty(); guard++) {
        if (equalsCI(cursor, childName)) return false;
        auto* node = findBind(cursor);
        if (!node) break;
        cursor = node->parent;
    }
    return true;
}

bool KeybindManager::setParent(std::string const& name, std::string const& parent) {
    auto* bind = findBind(name);
    if (!bind) return false;
    if (!canBeParentOf(name, parent)) return false;

    bind->parent = parent;
    if (!isGateOpen(*bind)) deactivateBind(*bind);
    persistNow();
    return true;
}

bool KeybindManager::isGateOpen(Keybind const& bind) {
    std::string cursor = bind.parent;
    for (size_t guard = 0; guard < binds.size() + 1 && !cursor.empty(); guard++) {
        auto* parent = findBind(cursor);
        if (!parent || !parent->active) return false;
        cursor = parent->parent;
    }
    return true;
}

int KeybindManager::gateDepth(Keybind const& bind) {
    int depth = 0;
    std::string cursor = bind.parent;
    while (!cursor.empty() && depth <= static_cast<int>(binds.size())) {
        auto* parent = findBind(cursor);
        if (!parent) break;
        depth++;
        cursor = parent->parent;
    }
    return depth;
}

void KeybindManager::deactivateBind(Keybind& bind) {
    if (bind.applied) restoreBind(bind);
    bind.active = false;
    bind.wasDown = false;
}

KeybindEdit* KeybindManager::findEdit(Keybind& bind, std::string const& module, std::string const& setting) {
    for (auto& edit : bind.edits) {
        if (edit.module == module && edit.setting == setting) return &edit;
    }
    return nullptr;
}

void KeybindManager::recordEdit(std::string const& bindName, std::string const& module, std::string const& setting,
                                size_t valueType, nlohmann::json value) {
    auto* bind = findBind(bindName);
    if (!bind) return;
    if (auto* edit = findEdit(*bind, module, setting)) {
        edit->valueType = valueType;
        edit->value = std::move(value);
    } else {
        bind->edits.push_back({ module, setting, valueType, std::move(value) });
    }
    dirty = true;
}

void KeybindManager::removeEdit(std::string const& bindName, std::string const& module, std::string const& setting) {
    auto* bind = findBind(bindName);
    if (!bind) return;
    std::erase_if(bind->edits,
                  [&](KeybindEdit const& e) { return e.module == module && e.setting == setting; });
    persistNow();
}

Setting* KeybindManager::resolveEdit(KeybindEdit const& edit) {
    return findSettingForEdit(edit);
}

std::string KeybindManager::settingOwnerModule(Setting* set) {
    if (!set) return "";
    bool isGlobal = false;
    Necromancer::getSettings().forEach([&](std::shared_ptr<Setting> s) {
        if (s.get() == set) isGlobal = true;
    });
    if (isGlobal) return "global";

    std::string owner;
    Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        if (!owner.empty() || !mod->settings) return;
        if (mod->name() == "BlockESP") {
            if (std::static_pointer_cast<BlockESP>(mod)->findEntrySetting(set->name()) == set) {
                owner = mod->name();
                return;
            }
        }
        bool found = false;
        mod->settings->forEach([&](std::shared_ptr<Setting> s) {
            if (!found && s.get() == set) found = true;
        });
        if (found) owner = mod->name();
    });
    return owner;
}

std::wstring KeybindManager::describeEdit(KeybindEdit const& edit) {
    std::wstring moduleName = util::StrToWStr(edit.module);
    std::wstring settingName = util::StrToWStr(edit.setting);
    if (edit.module == "global") {
        moduleName = LocalizeString::get("client.ui.clickGui.keybinds.global.name").value();
    } else if (auto mod = Necromancer::getModuleManager().find(edit.module)) {
        moduleName = mod->getDisplayName();
    }

    Setting* set = findSettingForEdit(edit);
    if (set) settingName = set->getDisplayName();

    std::wstring valueText = L"?";
    switch (edit.valueType) {
    case (size_t)Setting::Type::Bool:
        if (edit.value.is_boolean()) {
            valueText = edit.value.get<bool>()
                ? LocalizeString::get("client.ui.clickGui.keybinds.valueOn.name").value()
                : LocalizeString::get("client.ui.clickGui.keybinds.valueOff.name").value();
        }
        break;
    case (size_t)Setting::Type::Float:
        if (edit.value.is_number()) {
            std::wstringstream ss;
            ss << edit.value.get<float>();
            valueText = ss.str();
        }
        break;
    case (size_t)Setting::Type::Enum:
        if (edit.value.is_number()) {
            int idx = edit.value.get<int>();
            auto* entries = set && set->enumData ? set->enumData->getEntries() : nullptr;
            if (entries && idx >= 0 && idx < static_cast<int>(entries->size())) valueText = entries->at(idx).name();
            else valueText = std::to_wstring(idx);
        }
        break;
    case (size_t)Setting::Type::Text:
        if (edit.value.is_string()) {
            std::wstring text = util::StrToWStr(edit.value.get<std::string>());
            if (text.size() > 24) text = text.substr(0, 24) + L"\x2026";
            valueText = text;
        }
        break;
    case (size_t)Setting::Type::Color:
        if (edit.value.is_object() && edit.value.contains("color1") && edit.value["color1"].is_object()) {
            auto& c1 = edit.value["color1"];
            if (c1.contains("r") && c1.contains("g") && c1.contains("b") && c1["r"].is_number() &&
                c1["g"].is_number() && c1["b"].is_number()) {
                std::wstringstream ss;
                ss << std::hex << std::setfill(L'0') << std::setw(2)
                   << static_cast<int>(std::clamp(c1["r"].get<float>(), 0.f, 1.f) * 255.f + 0.5f);
                ss << std::setw(2) << static_cast<int>(std::clamp(c1["g"].get<float>(), 0.f, 1.f) * 255.f + 0.5f);
                ss << std::setw(2) << static_cast<int>(std::clamp(c1["b"].get<float>(), 0.f, 1.f) * 255.f + 0.5f);
                valueText = L"#" + ss.str();
            }
        }
        break;
    default:
        break;
    }
    return moduleName + L"." + settingName + L" = " + valueText;
}

bool KeybindManager::applyEdit(KeybindEdit const& edit, nlohmann::json const& value, nlohmann::json* outPrevious,
                               nlohmann::json* outEffective) {
    auto* set = resolveEdit(edit);
    if (!set || !set->value) return false;
    if (set->value->index() != edit.valueType) return false;

    switch (set->value->index()) {
    case (size_t)Setting::Type::Bool: {
        if (!value.is_boolean()) return false;
        auto& v = std::get<BoolValue>(*set->value);
        if (outPrevious) *outPrevious = v.value;
        v.value = value.get<bool>();
        if (outEffective) *outEffective = v.value;
        set->update();
        set->userUpdate();
        return true;
    }
    case (size_t)Setting::Type::Float: {
        if (!value.is_number()) return false;
        float f = value.get<float>();
        auto* mn = std::get_if<FloatValue>(&set->min);
        auto* mx = std::get_if<FloatValue>(&set->max);
        if (mn && mx) f = std::clamp(f, mn->value, mx->value);
        auto& v = std::get<FloatValue>(*set->value);
        if (outPrevious) *outPrevious = v.value;
        v.value = f;
        if (outEffective) *outEffective = v.value;
        set->update();
        set->userUpdate();
        return true;
    }
    case (size_t)Setting::Type::Enum: {
        if (!value.is_number()) return false;
        int idx = value.get<int>();
        if (set->enumData) {
            int count = static_cast<int>(set->enumData->getEntries()->size());
            idx = count > 0 ? std::clamp(idx, 0, count - 1) : 0;
        }
        auto& v = std::get<EnumValue>(*set->value);
        if (outPrevious) *outPrevious = v.val;
        v.val = idx;
        if (outEffective) *outEffective = v.val;
        set->update();
        set->userUpdate();
        return true;
    }
    case (size_t)Setting::Type::Text: {
        if (!value.is_string()) return false;
        auto& v = std::get<TextValue>(*set->value);
        if (outPrevious) v.store(*outPrevious);
        v.str = util::StrToWStr(value.get<std::string>());
        if (outEffective) v.store(*outEffective);
        set->update();
        set->userUpdate();
        return true;
    }
    case (size_t)Setting::Type::Color: {
        auto& v = std::get<ColorValue>(*set->value);
        if (outPrevious) v.store(*outPrevious);
        if (!applyColorJson(v, value)) {
            if (outPrevious) outPrevious->clear();
            return false;
        }
        if (outEffective) v.store(*outEffective);
        set->update();
        set->userUpdate();
        return true;
    }
    default:
        return false;
    }
}

bool KeybindManager::readEditCurrent(KeybindEdit const& edit, nlohmann::json& out) {
    auto* set = resolveEdit(edit);
    if (!set || !set->value) return false;
    switch (set->value->index()) {
    case (size_t)Setting::Type::Bool:
        out = std::get<BoolValue>(*set->value).value;
        return true;
    case (size_t)Setting::Type::Float:
        out = std::get<FloatValue>(*set->value).value;
        return true;
    case (size_t)Setting::Type::Enum:
        out = std::get<EnumValue>(*set->value).val;
        return true;
    case (size_t)Setting::Type::Text:
        std::get<TextValue>(*set->value).store(out);
        return true;
    case (size_t)Setting::Type::Color:
        std::get<ColorValue>(*set->value).store(out);
        return true;
    default:
        return false;
    }
}

void KeybindManager::applyBind(Keybind& bind) {
    if (bind.applied) return;
    bind.previous.assign(bind.edits.size(), nlohmann::json());
    bind.appliedValues.assign(bind.edits.size(), nlohmann::json());
    for (size_t i = 0; i < bind.edits.size(); i++) {
        applyEdit(bind.edits[i], bind.edits[i].value, &bind.previous[i], &bind.appliedValues[i]);
    }
    bind.applied = true;
}

void KeybindManager::restoreBind(Keybind& bind) {
    if (!bind.applied) return;
    for (size_t i = bind.edits.size(); i-- > 0;) {
        if (i >= bind.previous.size() || i >= bind.appliedValues.size()) break;
        if (bind.previous[i].is_null() || bind.appliedValues[i].is_null()) continue;
        nlohmann::json current;
        if (!readEditCurrent(bind.edits[i], current)) continue;
        if (!jsonValuesEqual(current, bind.appliedValues[i])) continue;
        applyEdit(bind.edits[i], bind.previous[i], nullptr, nullptr);
    }
    bind.previous.clear();
    bind.appliedValues.clear();
    bind.applied = false;
}

void KeybindManager::beginConfigSave() {
    saveStash.clear();
    for (auto it = binds.rbegin(); it != binds.rend(); ++it) {
        auto& bind = *it;
        if (!bind.applied) continue;
        for (size_t i = bind.edits.size(); i-- > 0;) {
            if (i >= bind.previous.size() || i >= bind.appliedValues.size()) break;
            if (bind.previous[i].is_null() || bind.appliedValues[i].is_null()) continue;
            nlohmann::json current;
            if (!readEditCurrent(bind.edits[i], current)) continue;
            if (!jsonValuesEqual(current, bind.appliedValues[i])) continue;
            auto* set = resolveEdit(bind.edits[i]);
            if (!writeRawEditValue(set, bind.previous[i])) continue;
            saveStash.emplace_back(set, std::move(current));
        }
    }
}

void KeybindManager::endConfigSave() {
    for (auto it = saveStash.rbegin(); it != saveStash.rend(); ++it) {
        writeRawEditValue(it->first, it->second);
    }
    saveStash.clear();
}

std::wstring KeybindManager::describeGraph(Keybind const& bind) {
    if (bind.kind != KindIf) return {};

    auto const& nodes = bind.graph.getNodes();
    if (nodes.empty()) {
        return LocalizeString::get("client.ui.clickGui.keybinds.cond.empty.name").value();
    }

    auto const* rootNode = bind.graph.find(bind.graph.getRoot());
    if (!rootNode) return LocalizeString::get("client.ui.clickGui.keybinds.cond.empty.name").value();

    size_t leaves = 0;
    for (auto const& node : nodes) {
        if (!ConditionGraph::isLogic(node.kind)) leaves++;
    }

    std::wstring label =
        LocalizeString::get(std::string("client.ui.clickGui.keybinds.cond.") + ConditionGraph::kindKey(rootNode->kind) +
                            ".name")
            .value();
    if (label.empty()) label = util::StrToWStr(ConditionGraph::kindKey(rootNode->kind));

    if (leaves <= 1 && !ConditionGraph::isLogic(rootNode->kind)) return label;
    return label + L" (" + std::to_wstring(leaves) + L")";
}

void KeybindManager::onUpdate(Event&) {
    if (dirty && std::chrono::steady_clock::now() - lastSave > 1s) persistNow();

    auto ci = SDK::ClientInstance::get();
    bool inUI = true;
    if (ci && ci->minecraftGame) inUI = !ci->minecraftGame->isCursorGrabbed();

    evalOrder.clear();
    evalOrder.reserve(binds.size());
    for (size_t i = 0; i < binds.size(); i++) {
        evalOrder.push_back({ gateDepth(binds[i]), i });
    }
    std::ranges::stable_sort(evalOrder, [](auto const& a, auto const& b) { return a.first < b.first; });

    SDK::LocalPlayer* localPlayer = ci ? ci->getLocalPlayer() : nullptr;
    condContext.reset(localPlayer);

    for (auto const& [depth, index] : evalOrder) {
        auto& bind = binds[index];

        if (!isGateOpen(bind)) {
            deactivateBind(bind);
            bind.graph.resetRuntime();
            bind.gateWasOpen = false;
            continue;
        }

        if (bind.kind == KindIf) {
            bind.gateWasOpen = true;
            bind.wasDown = false;

            bool canEvaluate = localPlayer && !bind.graph.empty();
            if (!canEvaluate) bind.graph.resetRuntime();
            bool newActive = canEvaluate && bind.graph.evaluate(condContext, condRuntime);
            if (newActive != bind.active) {
                bind.active = newActive;
                if (bind.active) applyBind(bind);
                else {
                    restoreBind(bind);
                    bind.graph.resetRuntime();
                }
            }
            continue;
        }

        bool down = bind.key > 0 && !inUI && Necromancer::getKeyboard().isKeyDownHardware(bind.key);

        if (!bind.gateWasOpen) {
            bind.gateWasOpen = true;
            bind.wasDown = down;
        }

        if (bind.type == TypeHold) {
            bool newActive = down;
            if (newActive != bind.active) {
                bind.active = newActive;
                if (bind.active) applyBind(bind);
                else restoreBind(bind);
            }
        } else if (down && !bind.wasDown) {
            bind.active = !bind.active;
            if (bind.active) applyBind(bind);
            else restoreBind(bind);
        }
        bind.wasDown = down;
    }
}

nlohmann::json KeybindManager::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& bind : binds) {
        nlohmann::json b;
        b["name"] = bind.name;
        b["parent"] = bind.parent;
        b["key"] = bind.key;
        b["type"] = bind.type;
        b["kind"] = bind.kind;
        b["hidden"] = bind.hidden;
        b["showOnlyWhenActive"] = bind.showOnlyWhenActive;
        if (bind.kind == KindIf) b["graph"] = bind.graph.toJson();
        b["edits"] = nlohmann::json::array();
        for (auto& edit : bind.edits) {
            nlohmann::json e;
            e["module"] = edit.module;
            e["setting"] = edit.setting;
            e["valueType"] = edit.valueType;
            e["value"] = edit.value;
            b["edits"].push_back(std::move(e));
        }
        arr.push_back(std::move(b));
    }
    return arr;
}

void KeybindManager::fromJson(nlohmann::json const& j) {
    if (!j.is_array()) return;
    binds.clear();
    for (auto& b : j) {
        if (!b.is_object()) continue;
        Keybind bind;
        bind.name = b.contains("name") && b["name"].is_string() ? b["name"].get<std::string>() : "";
        if (bind.name.empty()) continue;
        bind.parent = b.contains("parent") && b["parent"].is_string() ? b["parent"].get<std::string>() : "";
        bind.key = b.contains("key") && b["key"].is_number() ? std::clamp(b["key"].get<int>(), 0, 0xFF) : 0;
        bind.type = b.contains("type") && b["type"].is_number() && b["type"].get<int>() == TypeHold ? TypeHold
                                                                                                   : TypeToggle;
        bind.kind = b.contains("kind") && b["kind"].is_number() && b["kind"].get<int>() == KindIf ? KindIf
                                                                                                 : KindNormal;
        if (bind.kind == KindIf && b.contains("graph")) bind.graph.fromJson(b["graph"]);
        bind.hidden = b.contains("hidden") && b["hidden"].is_boolean() ? b["hidden"].get<bool>() : false;
        bind.showOnlyWhenActive = b.contains("showOnlyWhenActive") && b["showOnlyWhenActive"].is_boolean()
            ? b["showOnlyWhenActive"].get<bool>()
            : false;
        if (b.contains("edits") && b["edits"].is_array()) {
            for (auto& e : b["edits"]) {
                if (!e.is_object()) continue;
                KeybindEdit edit;
                edit.module = e.contains("module") && e["module"].is_string() ? e["module"].get<std::string>() : "";
                edit.setting = e.contains("setting") && e["setting"].is_string() ? e["setting"].get<std::string>() : "";
                if (edit.module.empty() || edit.setting.empty()) continue;
                edit.valueType = e.contains("valueType") && e["valueType"].is_number_unsigned()
                    ? e["valueType"].get<size_t>()
                    : 0;
                edit.value = e.contains("value") ? e["value"] : nlohmann::json();
                if (edit.value.is_null()) continue;
                bind.edits.push_back(std::move(edit));
            }
        }
        binds.push_back(std::move(bind));
    }

    sanitizeParents();
}

void KeybindManager::sanitizeParents() {
    for (auto& bind : binds) {
        if (bind.parent.empty()) continue;

        auto* parent = findBind(bind.parent);
        if (!parent || equalsCI(bind.parent, bind.name)) {
            bind.parent.clear();
            continue;
        }
        bind.parent = parent->name;
    }

    for (auto& bind : binds) {
        if (bind.parent.empty()) continue;

        std::string cursor = bind.parent;
        for (size_t guard = 0; guard <= binds.size(); guard++) {
            if (cursor.empty()) break;
            if (equalsCI(cursor, bind.name)) {
                bind.parent.clear();
                break;
            }
            auto* node = findBind(cursor);
            if (!node) break;
            cursor = node->parent;
        }
    }
}

void KeybindManager::migrateLegacyFile() {
    try {
        auto path = util::GetNecromancerPath() / "keybinds.json";
        if (!std::filesystem::exists(path)) return;
        std::ifstream ifs(path, std::ios::binary);
        nlohmann::json j = nlohmann::json::parse(ifs, nullptr, false);
        ifs.close();
        if (j.is_object() && j["binds"].is_array()) fromJson(j["binds"]);
        std::filesystem::remove(path);
    } catch (...) {
    }
}

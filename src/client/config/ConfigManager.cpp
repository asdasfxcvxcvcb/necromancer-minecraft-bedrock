#include "pch.h"
#include <algorithm>
#include "ConfigManager.h"
#include "client/Necromancer.h"
#include "client/feature/module/ModuleManager.h"
#include "client/misc/KeybindManager.h"

namespace {
    constexpr std::wstring_view AUTO_CONFIG_NAME = L"Auto Config";

    std::optional<bool> getLegacyDetectLanguageValue(SettingGroup& group) {
        std::optional<bool> legacyDetectLanguage;
        group.forEach([&](std::shared_ptr<Setting> set) {
            if (set->name() != "detectLanguage") return;

            if (auto* value = std::get_if<BoolValue>(&set->resolvedValue)) {
                legacyDetectLanguage = value->value;
            }
        });

        return legacyDetectLanguage;
    }

    void migrateLegacyLanguageValue(Setting& languageSetting, std::optional<bool> legacyDetectLanguage) {
        if (!legacyDetectLanguage) return;

        auto* languageValue = std::get_if<EnumValue>(&languageSetting.resolvedValue);
        if (!languageValue) return;

        const int legacyLanguageValue = std::max(languageValue->val, 0);
        languageValue->val = (*legacyDetectLanguage && legacyLanguageValue == 0) ? 0 : legacyLanguageValue + 1;
    }
}

ConfigManager::ConfigManager() {
    auto folder = util::GetNecromancerPath() / "Configs";
    std::filesystem::create_directory(folder);
    auto legacyPath = folder / "default.json";
    auto path = folder / (std::wstring(AUTO_CONFIG_NAME) + L".json");
    std::error_code ec;
    if (std::filesystem::exists(legacyPath, ec) && !std::filesystem::exists(path, ec)) {
        std::filesystem::rename(legacyPath, path, ec);
    }
    masterConfig = std::make_shared<Config>(path);
}

bool ConfigManager::loadMaster() {
    return load(masterConfig);
}

void ConfigManager::applyLanguageConfig(std::string_view languageSettingName) {
    for (auto& item : loadedConfig->getOutput()) {
        if (Necromancer::getSettings().name() == item->name()) {
            std::shared_ptr<Setting> languageSetting;
            const auto legacyDetectLanguage = getLegacyDetectLanguageValue(*item);

            item->forEach([&](std::shared_ptr<Setting> set) {
                if (set->name() == languageSettingName) {
                    languageSetting = set;
                }
            });

            if (languageSetting) {
                migrateLegacyLanguageValue(*languageSetting, legacyDetectLanguage);
                Necromancer::get().loadLanguageConfig(languageSetting);
            }

            Necromancer::get().loadConfig(*item.get());
        }
    }
}

void ConfigManager::applyGlobalConfig() {
    for (auto& item : loadedConfig->getOutput()) {
        if (Necromancer::getSettings().name() == item->name()) {
            Necromancer::get().loadConfig(*item.get());
        }
    }
}

void ConfigManager::applyModuleConfig() {
    for (auto& item : loadedConfig->getOutput()) {
        auto mod = Necromancer::getModuleManager().find(item->name());
        if (!mod) {
            Logger::Warn("Could not find {} as module in config", item->name());
        } else {
            mod->loadConfig(*item.get());
        }
    }
}

void ConfigManager::applyKeybindConfig() {
    if (!loadedConfig) return;
    auto& extra = loadedConfig->getExtra();
    if (!extra.contains("keybinds")) return;
    KeybindManager::get().fromJson(extra["keybinds"]);
}

bool ConfigManager::saveCurrentConfig() {
    if (!masterConfig) return false;
    return save(masterConfig);
}

bool ConfigManager::saveTo(std::wstring const& name) {
    auto cleanName = sanitizeConfigName(name);
    if (cleanName.empty()) return false;

    std::filesystem::path path = getConfigPath(cleanName);
    if (!isPathInConfigDir(path)) return false;

    auto cfg = std::make_shared<Config>(path);
    return save(cfg);
}

bool ConfigManager::loadUserConfig(std::wstring const& name) {
    auto cleanName = sanitizeConfigName(name);
    if (cleanName.empty()) return false;

    auto path = getConfigPath(cleanName);
    if (!isPathInConfigDir(path) || !std::filesystem::exists(path)) return false;

    auto config = std::make_shared<Config>(path);
    return load(std::move(config));
}

bool ConfigManager::loadAndApply(std::wstring const& name) {
    if (!saveCurrentConfig()) return false;
    if (!loadUserConfig(name)) return false;
    applyGlobalConfig();
    applyModuleConfig();
    applyKeybindConfig();
    return true;
}

bool ConfigManager::renameUserConfig(std::wstring const& oldName, std::wstring const& newName) {
    auto cleanOldName = sanitizeConfigName(oldName);
    auto cleanNewName = sanitizeConfigName(newName);
    if (cleanOldName.empty() || cleanNewName.empty()) return false;
    if (cleanOldName == AUTO_CONFIG_NAME || cleanNewName == AUTO_CONFIG_NAME) return false;

    auto oldPath = getConfigPath(cleanOldName);
    auto newPath = getConfigPath(cleanNewName);
    if (!isPathInConfigDir(oldPath) || !isPathInConfigDir(newPath)) return false;
    if (!std::filesystem::exists(oldPath) || std::filesystem::exists(newPath)) return false;

    std::error_code ec;
    std::filesystem::rename(oldPath, newPath, ec);
    if (ec) return false;

    if (loadedConfig && loadedConfig->getPath() == oldPath) {
        loadedConfig = std::make_shared<Config>(newPath);
        load(loadedConfig);
    }

    return true;
}

bool ConfigManager::deleteUserConfig(std::wstring const& name) {
    auto cleanName = sanitizeConfigName(name);
    if (cleanName.empty() || cleanName == AUTO_CONFIG_NAME) return false;

    auto path = getConfigPath(cleanName);
    if (!isPathInConfigDir(path) || !std::filesystem::exists(path)) return false;

    if (loadedConfig && loadedConfig->getPath() == path) {
        loadMaster();
        applyGlobalConfig();
        applyModuleConfig();
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return !ec;
}

std::vector<ConfigManager::UserConfigInfo> ConfigManager::listUserConfigs() {
    std::vector<UserConfigInfo> result;
    std::filesystem::create_directory(getUserPath());

    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(getUserPath(), ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != L".json") continue;

        UserConfigInfo info;
        info.name = entry.path().stem().wstring();
        info.path = entry.path();
        info.exists = true;
        info.autosave = false;
        info.protectedConfig = info.name == AUTO_CONFIG_NAME;
        result.push_back(std::move(info));
    }

    std::ranges::sort(result, [](UserConfigInfo const& a, UserConfigInfo const& b) {
        if (a.protectedConfig != b.protectedConfig) return a.protectedConfig > b.protectedConfig;
        return a.name < b.name;
    });
    return result;
}

std::wstring ConfigManager::getLoadedConfigName() const {
    if (!loadedConfig) return L"";
    return loadedConfig->getPath().stem().wstring();
}

std::filesystem::path ConfigManager::getUserPath() {
    return util::GetNecromancerPath() / "Configs";
}

bool ConfigManager::load(std::shared_ptr<Config> cfg) {
    auto res = cfg->load();
    if (res != std::nullopt) return false;
    loadedConfig = cfg;
    return true;
}

bool ConfigManager::save(std::shared_ptr<Config> cfg) {
    std::vector<SettingGroup*> groups = {};
    groups.push_back(&Necromancer::getSettings());

    Necromancer::getModuleManager().forEach([&](std::shared_ptr<Module> mod) {
        groups.push_back(mod->settings.get());
    });

    cfg->getExtra()["keybinds"] = KeybindManager::get().toJson();
    KeybindManager::get().beginConfigSave();
    auto res = cfg->save(groups);
    KeybindManager::get().endConfigSave();
    return res == std::nullopt;
}

std::wstring ConfigManager::sanitizeConfigName(std::wstring name) const {
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    while (!name.empty() && iswspace(name.back())) name.pop_back();

    for (auto& ch : name) {
        switch (ch) {
        case L'\\':
        case L'/':
        case L':':
        case L'*':
        case L'?':
        case L'"':
        case L'<':
        case L'>':
        case L'|':
            ch = L'_';
            break;
        default:
            break;
        }
    }

    if (name == L"." || name == L"..") return L"";
    if (name.size() > 64) name.resize(64);
    if (name.size() > 5 && name.substr(name.size() - 5) == L".json") name.resize(name.size() - 5);
    return name;
}

std::filesystem::path ConfigManager::getConfigPath(std::wstring const& name) const {
    return util::GetNecromancerPath() / "Configs" / (name + L".json");
}

bool ConfigManager::isPathInConfigDir(std::filesystem::path const& path) const {
    std::error_code ec;
    auto root = std::filesystem::weakly_canonical(util::GetNecromancerPath() / "Configs", ec);
    if (ec) return false;
    auto target = std::filesystem::weakly_canonical(path.parent_path(), ec);
    if (ec) return false;
    return root == target;
}

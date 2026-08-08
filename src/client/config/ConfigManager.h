#pragma once
#include "client/config/Config.h"

class ConfigManager final : public Manager<Config> {
public:
    struct UserConfigInfo {
        std::wstring name;
        std::filesystem::path path;
        bool exists = false;
        bool autosave = false;
        bool protectedConfig = false;
    };

    ConfigManager();
    ~ConfigManager() = default;

    bool loadMaster();

    void applyLanguageConfig(std::string_view languageSettingName);
    void applyGlobalConfig();
    void applyModuleConfig();
    void applyKeybindConfig();

    bool saveCurrentConfig();
    bool saveTo(std::wstring const& name);
    bool loadUserConfig(std::wstring const& name);
    bool loadAndApply(std::wstring const& name);
    bool renameUserConfig(std::wstring const& oldName, std::wstring const& newName);
    bool deleteUserConfig(std::wstring const& name);
    std::vector<UserConfigInfo> listUserConfigs();
    std::wstring getLoadedConfigName() const;

    std::filesystem::path getUserPath();

private:
    std::shared_ptr<Config> masterConfig;
    std::shared_ptr<Config> loadedConfig;

    bool load(std::shared_ptr<Config> cfg);
    bool save(std::shared_ptr<Config> cfg);
    std::wstring sanitizeConfigName(std::wstring name) const;
    std::filesystem::path getConfigPath(std::wstring const& name) const;
    bool isPathInConfigDir(std::filesystem::path const& path) const;
};

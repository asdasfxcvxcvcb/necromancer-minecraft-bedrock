#include "pch.h"
#include "Module.h"

void Module::loadConfig(SettingGroup& resolvedGroup) {
    resolvedGroup.forEach([&](std::shared_ptr<Setting> set) {
        this->settings->forEach([&](std::shared_ptr<Setting> modSet) {
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
    afterLoadConfig();
}

std::shared_ptr<Setting> Module::addSetting(std::string const& internalName, std::wstring const& disp,
                                            std::wstring const& desc, ValueType& val, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, disp, desc, condition);
    set->value = &val;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addSetting(std::string const& internalName, LocalizedString const& disp,
                                            std::wstring const& desc, ValueType& val, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, disp, desc, condition);
    set->value = &val;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addSetting(std::string const& internalName, std::wstring const& disp,
                                            LocalizedString const& desc, ValueType& val, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, disp, desc, condition);
    set->value = &val;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addSetting(std::string const& internalName, LocalizedString const& disp,
                                            LocalizedString const& desc, ValueType& val, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, disp, desc, condition);
    set->value = &val;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addEnumSetting(std::string const& internalName, std::wstring const& displayNameText,
                                                std::wstring const& desc, EnumData& dat, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->enumData = &dat;
    set->value = dat.getValue();
    set->defaultValue = EnumValue(0); // harcoded
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addEnumSetting(std::string const& internalName, LocalizedString const& displayNameText,
                                                std::wstring const& desc, EnumData& dat, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->enumData = &dat;
    set->value = dat.getValue();
    set->defaultValue = EnumValue(0); // harcoded
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addEnumSetting(std::string const& internalName, std::wstring const& displayNameText,
                                                LocalizedString const& desc, EnumData& dat,
                                                Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->enumData = &dat;
    set->value = dat.getValue();
    set->defaultValue = EnumValue(0); // harcoded
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addEnumSetting(std::string const& internalName, LocalizedString const& displayNameText,
                                                LocalizedString const& desc, EnumData& dat,
                                                Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->enumData = &dat;
    set->value = dat.getValue();
    set->defaultValue = EnumValue(0); // harcoded
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addSliderSetting(std::string const& internalName, std::wstring const& displayNameText,
                                                  std::wstring const& desc, ValueType& val, ValueType min,
                                                  ValueType max, ValueType interval, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->value = &val;
    set->min = min;
    set->max = max;
    set->interval = interval;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addSliderSetting(std::string const& internalName, LocalizedString const& displayNameText,
                                                  std::wstring const& desc, ValueType& val, ValueType min,
                                                  ValueType max, ValueType interval, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->value = &val;
    set->min = min;
    set->max = max;
    set->interval = interval;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addSliderSetting(std::string const& internalName, std::wstring const& displayNameText,
                                                  LocalizedString const& desc, ValueType& val, ValueType min,
                                                  ValueType max, ValueType interval, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->value = &val;
    set->min = min;
    set->max = max;
    set->interval = interval;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

std::shared_ptr<Setting> Module::addSliderSetting(std::string const& internalName, LocalizedString const& displayNameText,
                                                  LocalizedString const& desc, ValueType& val, ValueType min,
                                                  ValueType max, ValueType interval, Setting::Condition condition) {
    auto set = std::make_shared<Setting>(internalName, displayNameText, desc, condition);
    set->value = &val;
    set->min = min;
    set->max = max;
    set->interval = interval;
    set->defaultValue = val;
    settings->addSetting(set);
    return set;
}

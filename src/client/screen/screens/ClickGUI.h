#pragma once
#include "../Screen.h"
#include "../../render/asset/Asset.h"
#include "client/screen/TextBox.h"
#include "client/config/ConfigManager.h"
#include "client/localization/LocalizeString.h"
#include <memory>
#include <array>
#include <map>
#include <optional>

class ClickGUI : public Screen {
public:
    ClickGUI();

    void onRender(class Event& ev);
    void onCleanup(Event& ev);
    void onInit(Event& ev);
    void onKey(Event& ev);
    void onClick(Event& ev);
    void close() override;

    std::string getName() override { return "ClickGUI"; }
    float drawSetting(class Setting* set, class SettingGroup* group, struct Vec2 const& pos, class D2DUtil& dc,
                      float size = 150.f, float fTextWidth = 0.21f, bool bypassClickThrough = false);

    bool shouldSelect(d2d::Rect rc, Vec2 const& pt) override;

    void drawColorPicker();

    void openBlockPicker(class BlockESP* mod);
    void openItemSwitcher(class ItemSwitcher* mod);

    void jumpToModule(std::string const& name) {
        jumpModule = name;
        modTab = VISUALS;
    }

    void requestModuleListRebuild() { shouldRebuildModLikes = true; }

    [[nodiscard]] bool hasActiveSetting() const;

    void refreshLocalization() {
        colorPicker.rgbSetting.refreshLocalization();
        colorPicker.forceTagSetting.refreshLocalization();
    }

protected:
    void onEnable(bool ignoreAnims) override;
    void onDisable() override;

private:
    void updateScrollbarDrag(Vec2 const& mouse);
    void renderConfigTab(class D2DUtil& dc, d2d::Rect const& listRect, float modulePad, float padFromSearchBar, bool rtl);
    void renderPlayerListTab(class D2DUtil& dc, d2d::Rect const& area, float modulePad, bool rtl);
    void renderKeybindsTab(class D2DUtil& dc, d2d::Rect const& area, float modulePad, bool rtl);
    void renderKeybindEditorOverlay(class D2DUtil& dc, bool rtl);
    void drawParentPicker(class D2DUtil& dc);
    void drawKindPicker(class D2DUtil& dc);
    void drawCondCanvas(class D2DUtil& dc, bool rtl);
    void drawCondPalette(class D2DUtil& dc, struct Keybind& bind, class ConditionGraph& graph);
    void drawCondItemPicker(class D2DUtil& dc);
    void flushCondIcons(class D2DUtil& dc);
    void openCondCanvas(std::string const& bindName);
    void closeCondCanvas();
    bool drawButton(class D2DUtil& dc, d2d::Rect const& rc, std::wstring const& text, std::wstring const& tip, bool enabled);
    bool isModuleInTab(class Module& mod) const;
    void drawBlockPicker(class D2DUtil& dc);
    void closeBlockPicker();
    void drawItemSwitcher(class D2DUtil& dc);
    void closeItemSwitcher();
    void refreshConfigList();
    bool hasSelectedSettingBox() const;
    void clearSettingBoxFocus();

    struct ColorPicker {
        Setting* setting = nullptr;
        StoredColor* selectedColor = nullptr;
        HSV pickerColor = {};
        float svModX = 1.f;
        float svModY = 0.f;
        float hueMod = 0.f;
        float opacityMod = 1.f;

        bool isEditingHue = false;
        bool isEditingOpacity = false;
        bool isEditingSV = false;

        bool queueClose = false;
        bool dragging = false;
        Vec2 dragOffs = {};
        ValueType rgbSelector = BoolValue(false);
        ValueType forceTagSelector = BoolValue(false);

        Setting rgbSetting;
        Setting forceTagSetting;

        ColorPicker()
            : rgbSetting("colorpickerrgb", LocalizeString::get("client.ui.clickGui.rgb.name"),
                         LocalizeString::get("client.ui.clickGui.rgb.desc")),
              forceTagSetting("colorpickerforcetag", LocalizeString::get("client.ui.clickGui.forceTagColor.name"),
                              LocalizeString::get("client.ui.clickGui.forceTagColor.desc")) {
            rgbSetting.value = &rgbSelector;
            forceTagSetting.value = &forceTagSelector;
        }
    } colorPicker {};

    struct BlockPicker {
        class BlockESP* mod = nullptr;
        bool addView = false;
        int editIndex = -1;
        float scroll = 0.f;
        float lerpScroll = 0.f;
        float scrollMax = 0.f;
        bool dragging = false;
        Vec2 dragOffs = {};
        bool queueClose = false;
    } blockPicker {};

    TextBox blockSearchBox { {}, 48 };
    bool blockSearchRegistered = false;
    d2d::Rect bPickerRect = {};

    struct ItemSwitcherPicker {
        class ItemSwitcher* mod = nullptr;
        float scroll = 0.f;
        float lerpScroll = 0.f;
        float scrollMax = 0.f;
        bool queueClose = false;
    } itemSwitcherPicker {};
    TextBox itemSwitcherSearchBox { {}, 48 };
    bool itemSwitcherSearchRegistered = false;

    TextBox searchTextBox {};
    TextBox configNameTextBox { {}, 64 };
    std::vector<TextBox> pickerTextBoxes {};

    ComPtr<ID2D1Bitmap1> shadowBitmap;
    ComPtr<ID2D1Bitmap1> auxiliaryBitmap;
    ComPtr<ID2D1Bitmap1> modHoverBitmap;
    ComPtr<ID2D1ImageBrush> clipBrush;
    std::optional<d2d::Rect> modClip = {};

    enum Tab {
        MODULES = 0,
        SETTINGS,
    } tab = MODULES;

    enum ModTab {
        COMBAT = 0,
        VISUALS,
        MOVEMENT,
        MISC,
        PLAYERLIST,
        KEYBINDS,
        CONFIG
    } modTab = VISUALS;

    struct ModuleLike {
        std::wstring name;
        std::wstring description;
        std::shared_ptr<Module> mod;
        bool shouldRender = true;
        bool isExtended = false;
        Vec2 previewSize = {};
        float arrowRot = 180.f;
        float lerpArrowRot = 1.f;
        float lerpToggle = 0.f;
        float lerpHover = 0.f;
        Color toggleColorOn = {};
        Color toggleColorOff = d2d::Color::RGB(0x63, 0x63, 0x63);
        std::optional<d2d::Rect> modRect = std::nullopt;

        static bool isLess(ModuleLike const& a, ModuleLike const& b) {
            return a.name < b.name;
        }
    };

    std::map<Setting*, std::shared_ptr<TextBox>> settingBoxes = {};
    std::map<Setting*, float> dropdownAnimations = {};

    d2d::Rect rect = {};
    d2d::Rect cPickerRect = {};
    d2d::Rect scrollbarTrackRect = {};
    d2d::Rect scrollbarThumbRect = {};

    Setting* activeSetting = nullptr;
    Setting* dropdownSetting = nullptr;
    int capturedKey = 0;
    float adaptedScale = 0.f;

    float scrollMax = 0.f;
    float scroll = 0.f;
    float lerpScroll = 0.f;
    float scrollbarDragOffset = 0.f;
    bool shouldRebuildModLikes = false;
    bool draggingScrollbar = false;

    std::optional<std::string> jumpModule;
    std::vector<ConfigManager::UserConfigInfo> configList;
    std::wstring selectedConfigName;
    std::wstring configEditName;
    bool configListDirty = true;
    bool configInputOpen = false;
    bool configRenameMode = false;
    float configStatusTimer = 0.f;
    std::wstring configStatusText;

    std::string plSelectedPlayer;
    bool plTagsView = false;
    std::string plRenamingTag;
    TextBox plRenameBox { {}, 100 };
    bool plRenameBoxRegistered = false;
    std::string plPriorityDrag;
    std::map<std::string, std::shared_ptr<Setting>> tagColorSettings;
    std::map<std::string, ValueType> tagColorValues;
    ComPtr<ID2D1Effect> compositeEffect;

    std::string kbRenamingBind;
    TextBox kbRenameBox { {}, 64 };
    bool kbRenameBoxRegistered = false;
    std::string kbCapturingKey;
    std::string kbEditingBind;
    std::string kbHoverLabel;

    struct ParentPicker {
        std::string bind;
        float scroll = 0.f;
        float lerpScroll = 0.f;
        float scrollMax = 0.f;
        bool queueClose = false;
        bool justOpened = false;
    } kbParentPicker {};

    d2d::Rect kbParentPickerRect = {};

    struct KindPicker {
        bool open = false;
        bool justOpened = false;
        bool queueClose = false;
    } kbKindPicker {};

    d2d::Rect kbKindPickerRect = {};

    struct CondCanvas {
        std::string bind;
        bool open = false;

        // drag-to-snap: either moving an existing node, or spawning a new one from the palette
        int dragNode = 0;
        int dragNewKind = 0;
        bool dragging = false;
        bool dragArmed = false;
        Vec2 dragStart = {};
        Vec2 dragGrabOffset = {};

        // resolved drop slot for the current frame
        int dropParent = 0;
        int dropIndex = -1;
        d2d::Rect dropRect = {};
        bool dropValid = false;

        int selectedNode = 0;
        int paletteHover = -1;
        float paletteScroll = 0.f;
        float paletteScrollMax = 0.f;
        float boardScroll = 0.f;
        float boardScrollMax = 0.f;

        int itemPickerNode = 0;
        bool itemPickerJustOpened = false;
        float itemScroll = 0.f;
        float itemScrollMax = 0.f;
        bool queueClose = false;
    } condCanvas {};

    d2d::Rect condCanvasRect = {};
    d2d::Rect condBoardRect = {};
    d2d::Rect condPaletteRect = {};
    d2d::Rect condItemPickerRect = {};
    TextBox condItemSearchBox { {}, 48 };
    bool condItemSearchRegistered = false;
    TextBox condWaitMsBox { {}, 8, true };
    bool condWaitMsRegistered = false;
    int condWaitMsNode = 0;
    TextBox condThresholdBox { {}, 8, true };
    bool condThresholdRegistered = false;
    int condThresholdNode = 0;
    TextBox condCountBox { {}, 6, true };
    bool condCountRegistered = false;
    int condCountNode = 0;
    TextBox condRangeBox { {}, 8, true };
    bool condRangeRegistered = false;
    int condRangeNode = 0;
    TextBox condDamageWindowBox { {}, 8, true };
    bool condDamageWindowRegistered = false;
    int condDamageWindowNode = 0;
    std::vector<std::pair<d2d::Rect, void*>> condIconDraws;
};

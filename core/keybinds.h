#pragma once
#include "imgui.h"
#include <string>
#include <vector>

enum class KeyAction {
    None = -1,

    AppendTransition,
    DeleteGlbMesh,
    DeleteSection,
    DeleteTransition,
    ExportTrack,
    IncrementalSave,
    MeasurementPointsWindow,
    MoveBackward,
    MoveForward,
    MoveLeft,
    MoveRight,
    OverlayScenery,
    OverlaySceneryDelete,
    OverlaySceneryNext,
    OverlaySceneryPrev,
    OverlaySceneryToggleVis,
    OverlayWarnings,
    PlayheadPlayPause,
    PlayheadStepBackward,
    PlayheadStepForward,
    POVHeightDown,
    POVHeightReset,
    POVHeightUp,
    PrependTransition,
    Redo,
    ResetView,
    SaveProject,
    SaveScreenshot,
    SelectNextSection,
    SelectPrevSection,
    ShaderLateralForce,
    ShaderNormalForce,
    ShaderNothing,
    ShaderRollSpeed,
    ShaderTrackFlexion,
    ShaderVelocity,
    TogglePOVMode,
    ToggleTrainMesh,
    ToggleViewportFullscreen,
    Undo,
    ViewFrontOrtho,
    ViewPerspective,
    ViewSideOrtho,
    ViewTopOrtho,

    Count
};

struct KeybindInfo {
    KeyAction action;
    std::string idStr;
    std::string name;
    std::string category;
    std::string scope;
    ImGuiKeyChord defaultChord;
    ImGuiKeyChord chord;
};

class KeybindManager {
public:
    KeybindManager();

    static KeybindManager& instance();

    void initDefaults();
    void load(const std::string& path = "keybinds.cfg");
    void save(const std::string& path = "keybinds.cfg") const;
    void resetToDefaults();
    void resetActionToDefault(KeyAction action);

    ImGuiKeyChord getChord(KeyAction action) const;
    ImGuiKeyChord& getChordRef(KeyAction action);
    void setChord(KeyAction action, ImGuiKeyChord chord);

    bool isTriggered(KeyAction action, bool repeat = false) const;
    bool isHeld(KeyAction action) const;

    const KeybindInfo& getInfo(KeyAction action) const;
    const std::vector<KeybindInfo>& getAll() const {
        return binds;
    }

    static std::string formatChord(ImGuiKeyChord chord);
    static int getModifierIndex(ImGuiKeyChord chord);
    static int getModifierMaskFromIndex(int index);
    static ImGuiKey getBaseKey(ImGuiKeyChord chord);
    static ImGuiKeyChord makeChord(int modMask, ImGuiKey baseKey);
    static bool isModifierKey(ImGuiKey key);
    static const char* getModifierComboString();

private:
    std::vector<KeybindInfo> binds;
};

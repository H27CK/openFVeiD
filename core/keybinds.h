#pragma once
#include "imgui.h"
#include <string>
#include <vector>

enum class KeyAction {
    None = -1,

    // 1. General & Project Actions
    Undo,
    Redo,
    SaveProject,
    IncrementalSave,
    ExportTrack,
    SaveScreenshot,
    MeasurementPointsWindow,
    ResetView,
    DeleteGlbMesh,

    // 2. Section & Transition Editing
    SelectPrevSection,
    SelectNextSection,
    DeleteSection,
    DeleteTransition,
    PrependTransition,
    AppendTransition,

    // 3. Track Shading Modes
    ShaderNothing,
    ShaderVelocity,
    ShaderRollSpeed,
    ShaderNormalForce,
    ShaderLateralForce,
    ShaderTrackFlexion,

    // 4. Camera Views & Viewport Navigation
    TogglePOVMode,
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    POVHeightUp,
    POVHeightDown,
    POVHeightReset,
    ViewPerspective,
    ViewTopOrtho,
    ViewSideOrtho,
    ViewFrontOrtho,

    // 5. Viewport In-Scene Overlays
    OverlayScenery,
    OverlayWarnings,
    OverlaySceneryPrev,
    OverlaySceneryNext,
    OverlaySceneryToggleVis,
    OverlaySceneryDelete,

    // 6. Graph View & Playhead
    PlayheadPlayPause,
    PlayheadStepBackward,
    PlayheadStepForward,

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

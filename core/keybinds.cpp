#include "core/keybinds.h"
#include <fstream>
#include <sstream>
#include <iostream>

KeybindManager::KeybindManager() {
    initDefaults();
}

KeybindManager& KeybindManager::instance() {
    static KeybindManager inst;
    return inst;
}

void KeybindManager::initDefaults() {
    binds.clear();
    binds.resize((size_t)KeyAction::Count);

    auto reg = [this](KeyAction act, const std::string& idStr, const std::string& name,
                      const std::string& category, const std::string& scope, ImGuiKeyChord defChord) {
        size_t idx = (size_t)act;
        binds[idx] = {act, idStr, name, category, scope, defChord, defChord};
    };

    // 1. General & Project Actions
    reg(KeyAction::Undo, "Undo", "Undo", "General & Project Actions", "Global", ImGuiMod_Ctrl | ImGuiKey_Z);
    reg(KeyAction::Redo, "Redo", "Redo", "General & Project Actions", "Global", ImGuiMod_Ctrl | ImGuiKey_Y);
    reg(KeyAction::SaveProject, "SaveProject", "Save Project", "General & Project Actions", "Global", ImGuiMod_Ctrl | ImGuiKey_S);
    reg(KeyAction::IncrementalSave, "IncrementalSave", "Incremental Save", "General & Project Actions", "Global", ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S);
    reg(KeyAction::ExportTrack, "ExportTrack", "Export Track", "General & Project Actions", "Global", ImGuiMod_Ctrl | ImGuiKey_E);
    reg(KeyAction::SaveScreenshot, "SaveScreenshot", "Save Screenshot", "General & Project Actions", "Global", ImGuiKey_F12);
    reg(KeyAction::MeasurementPointsWindow, "MeasurementPointsWindow", "Measurement Points Window", "General & Project Actions", "Global", ImGuiMod_Ctrl | ImGuiKey_M);
    reg(KeyAction::ResetView, "ResetView", "Reset View", "General & Project Actions", "Viewport", ImGuiKey_Period);
    reg(KeyAction::DeleteGlbMesh, "DeleteGlbMesh", "Delete Scenery Object", "General & Project Actions", "Viewport", ImGuiKey_X);

    // 2. Section & Transition Editing
    reg(KeyAction::SelectPrevSection, "SelectPrevSection", "Select Previous Section", "Section & Transition Editing", "Global", ImGuiMod_Ctrl | ImGuiKey_UpArrow);
    reg(KeyAction::SelectNextSection, "SelectNextSection", "Select Next Section", "Section & Transition Editing", "Global", ImGuiMod_Ctrl | ImGuiKey_DownArrow);
    reg(KeyAction::DeleteSection, "DeleteSection", "Delete Section", "Section & Transition Editing", "Global", ImGuiKey_Delete);
    reg(KeyAction::DeleteTransition, "DeleteTransition", "Delete Transition", "Section & Transition Editing", "Global", ImGuiKey_Backspace);
    reg(KeyAction::PrependTransition, "PrependTransition", "Prepend Transition", "Section & Transition Editing", "Global", ImGuiKey_LeftBracket);
    reg(KeyAction::AppendTransition, "AppendTransition", "Append Transition", "Section & Transition Editing", "Global", ImGuiKey_RightBracket);

    // 3. Track Shading Modes
    reg(KeyAction::ShaderNothing, "ShaderNothing", "Shader: Nothing", "Track Shading Modes", "Global", ImGuiMod_Ctrl | ImGuiKey_1);
    reg(KeyAction::ShaderVelocity, "ShaderVelocity", "Shader: Velocity", "Track Shading Modes", "Global", ImGuiMod_Ctrl | ImGuiKey_2);
    reg(KeyAction::ShaderRollSpeed, "ShaderRollSpeed", "Shader: Roll Speed", "Track Shading Modes", "Global", ImGuiMod_Ctrl | ImGuiKey_3);
    reg(KeyAction::ShaderNormalForce, "ShaderNormalForce", "Shader: Normal Force", "Track Shading Modes", "Global", ImGuiMod_Ctrl | ImGuiKey_4);
    reg(KeyAction::ShaderLateralForce, "ShaderLateralForce", "Shader: Lateral Force", "Track Shading Modes", "Global", ImGuiMod_Ctrl | ImGuiKey_5);
    reg(KeyAction::ShaderTrackFlexion, "ShaderTrackFlexion", "Shader: Track Flexion", "Track Shading Modes", "Global", ImGuiMod_Ctrl | ImGuiKey_6);

    // 4. Camera Views & Viewport Navigation
    reg(KeyAction::TogglePOVMode, "TogglePOVMode", "Toggle POV Mode", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_Space);
    reg(KeyAction::MoveForward, "MoveForward", "Move Forward", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_W);
    reg(KeyAction::MoveBackward, "MoveBackward", "Move Backward", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_S);
    reg(KeyAction::MoveLeft, "MoveLeft", "Move Left", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_A);
    reg(KeyAction::MoveRight, "MoveRight", "Move Right", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_D);
    reg(KeyAction::POVHeightUp, "POVHeightUp", "POV Height Up", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_PageUp);
    reg(KeyAction::POVHeightDown, "POVHeightDown", "POV Height Down", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_PageDown);
    reg(KeyAction::POVHeightReset, "POVHeightReset", "POV Height Reset", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_Home);
    reg(KeyAction::ViewPerspective, "ViewPerspective", "View: Perspective", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_None);
    reg(KeyAction::ViewTopOrtho, "ViewTopOrtho", "View: Top Ortho", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_None);
    reg(KeyAction::ViewSideOrtho, "ViewSideOrtho", "View: Side Ortho", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_None);
    reg(KeyAction::ViewFrontOrtho, "ViewFrontOrtho", "View: Front Ortho", "Camera Views & Viewport Navigation", "Viewport", ImGuiKey_None);

    // 5. Viewport In-Scene Overlays
    reg(KeyAction::OverlayScenery, "OverlayScenery", "Scenery Overlay (Hold)", "Viewport In-Scene Overlays", "Viewport", ImGuiKey_Z);
    reg(KeyAction::OverlayWarnings, "OverlayWarnings", "Warnings Overlay (Hold)", "Viewport In-Scene Overlays", "Viewport", ImGuiKey_X);
    reg(KeyAction::OverlaySceneryPrev, "OverlaySceneryPrev", "Scenery: Previous Mesh", "Viewport In-Scene Overlays", "Viewport Overlay", ImGuiKey_UpArrow);
    reg(KeyAction::OverlaySceneryNext, "OverlaySceneryNext", "Scenery: Next Mesh", "Viewport In-Scene Overlays", "Viewport Overlay", ImGuiKey_DownArrow);
    reg(KeyAction::OverlaySceneryToggleVis, "OverlaySceneryToggleVis", "Scenery: Toggle Visibility", "Viewport In-Scene Overlays", "Viewport Overlay", ImGuiKey_H);
    reg(KeyAction::OverlaySceneryDelete, "OverlaySceneryDelete", "Scenery: Delete Mesh", "Viewport In-Scene Overlays", "Viewport Overlay", ImGuiKey_X);

    // 6. Graph View & Playhead
    reg(KeyAction::PlayheadPlayPause, "PlayheadPlayPause", "Play / Pause Playhead", "Graph View & Playhead", "GraphView", ImGuiKey_P);
    reg(KeyAction::PlayheadStepBackward, "PlayheadStepBackward", "Step Playhead Backward", "Graph View & Playhead", "GraphView", ImGuiKey_LeftArrow);
    reg(KeyAction::PlayheadStepForward, "PlayheadStepForward", "Step Playhead Forward", "Graph View & Playhead", "GraphView", ImGuiKey_RightArrow);
}

void KeybindManager::resetToDefaults() {
    for (auto& b : binds) {
        b.chord = b.defaultChord;
    }
}

void KeybindManager::resetActionToDefault(KeyAction action) {
    size_t idx = (size_t)action;
    if (idx < binds.size()) {
        binds[idx].chord = binds[idx].defaultChord;
    }
}

ImGuiKeyChord KeybindManager::getChord(KeyAction action) const {
    size_t idx = (size_t)action;
    if (idx < binds.size())
        return binds[idx].chord;
    return ImGuiKey_None;
}

ImGuiKeyChord& KeybindManager::getChordRef(KeyAction action) {
    size_t idx = (size_t)action;
    return binds[idx].chord;
}

void KeybindManager::setChord(KeyAction action, ImGuiKeyChord chord) {
    size_t idx = (size_t)action;
    if (idx < binds.size()) {
        binds[idx].chord = chord;
    }
}

const KeybindInfo& KeybindManager::getInfo(KeyAction action) const {
    size_t idx = (size_t)action;
    return binds[idx];
}

bool KeybindManager::isTriggered(KeyAction action, bool repeat) const {
    ImGuiKeyChord chord = getChord(action);
    if (chord == 0 || chord == ImGuiKey_None)
        return false;

    bool wantCtrl = (chord & ImGuiMod_Ctrl) != 0;
    bool wantShift = (chord & ImGuiMod_Shift) != 0;
    bool wantAlt = (chord & ImGuiMod_Alt) != 0;
    bool wantSuper = (chord & ImGuiMod_Super) != 0;

    ImGuiIO& io = ImGui::GetIO();
    if (wantCtrl != io.KeyCtrl || wantShift != io.KeyShift || wantAlt != io.KeyAlt || wantSuper != io.KeySuper)
        return false;

    ImGuiKey key = (ImGuiKey)(chord & ~ImGuiMod_Mask_);
    if (key == ImGuiKey_ReservedForModCtrl)
        key = ImGuiKey_LeftCtrl;
    if (key == ImGuiKey_ReservedForModShift)
        key = ImGuiKey_LeftShift;
    if (key == ImGuiKey_ReservedForModAlt)
        key = ImGuiKey_LeftAlt;
    if (key == ImGuiKey_ReservedForModSuper)
        key = ImGuiKey_LeftSuper;
    if (key == ImGuiKey_None)
        return false;

    return ImGui::IsKeyPressed(key, repeat);
}

bool KeybindManager::isHeld(KeyAction action) const {
    ImGuiKeyChord chord = getChord(action);
    if (chord == 0 || chord == ImGuiKey_None)
        return false;

    bool wantCtrl = (chord & ImGuiMod_Ctrl) != 0;
    bool wantShift = (chord & ImGuiMod_Shift) != 0;
    bool wantAlt = (chord & ImGuiMod_Alt) != 0;
    bool wantSuper = (chord & ImGuiMod_Super) != 0;

    ImGuiIO& io = ImGui::GetIO();
    if (wantCtrl != io.KeyCtrl || wantAlt != io.KeyAlt || wantSuper != io.KeySuper)
        return false;

    // For navigation/movement actions, permit holding Shift (used as sprint boost)
    bool isNavAction = (action == KeyAction::MoveForward || action == KeyAction::MoveBackward ||
                        action == KeyAction::MoveLeft || action == KeyAction::MoveRight ||
                        action == KeyAction::POVHeightUp || action == KeyAction::POVHeightDown ||
                        action == KeyAction::POVHeightReset || action == KeyAction::OverlayScenery ||
                        action == KeyAction::OverlayWarnings);
    if (!isNavAction && wantShift != io.KeyShift)
        return false;

    ImGuiKey key = (ImGuiKey)(chord & ~ImGuiMod_Mask_);
    if (key == ImGuiKey_ReservedForModCtrl)
        key = ImGuiKey_LeftCtrl;
    if (key == ImGuiKey_ReservedForModShift)
        key = ImGuiKey_LeftShift;
    if (key == ImGuiKey_ReservedForModAlt)
        key = ImGuiKey_LeftAlt;
    if (key == ImGuiKey_ReservedForModSuper)
        key = ImGuiKey_LeftSuper;
    if (key == ImGuiKey_None)
        return false;

    return ImGui::IsKeyDown(key);
}

std::string KeybindManager::formatChord(ImGuiKeyChord chord) {
    if (chord == 0 || chord == ImGuiKey_None)
        return "None";

    std::string s;
    if (chord & ImGuiMod_Ctrl)
        s += "Ctrl+";
    if (chord & ImGuiMod_Alt)
        s += "Alt+";
    if (chord & ImGuiMod_Shift)
        s += "Shift+";
    if (chord & ImGuiMod_Super)
        s += "Super+";

    ImGuiKey key = (ImGuiKey)(chord & ~ImGuiMod_Mask_);
    if (key != ImGuiKey_None) {
        if (key == ImGuiKey_ReservedForModCtrl || key == ImGuiKey_ReservedForModShift ||
            key == ImGuiKey_ReservedForModAlt || key == ImGuiKey_ReservedForModSuper) {
            // Handled by modifier flags above
        } else {
            const char* name = ImGui::GetKeyName(key);
            s += (name ? name : "Unknown");
        }
    }
    if (!s.empty() && s.back() == '+') {
        s.pop_back();
    }
    return s.empty() ? "None" : s;
}

int KeybindManager::getModifierIndex(ImGuiKeyChord chord) {
    bool c = (chord & ImGuiMod_Ctrl) != 0;
    bool s = (chord & ImGuiMod_Shift) != 0;
    bool a = (chord & ImGuiMod_Alt) != 0;
    if (c && a && s)
        return 7;
    if (a && s)
        return 6;
    if (c && a)
        return 5;
    if (c && s)
        return 4;
    if (a)
        return 3;
    if (s)
        return 2;
    if (c)
        return 1;
    return 0; // None
}

int KeybindManager::getModifierMaskFromIndex(int index) {
    switch (index) {
    case 1:
        return ImGuiMod_Ctrl;
    case 2:
        return ImGuiMod_Shift;
    case 3:
        return ImGuiMod_Alt;
    case 4:
        return ImGuiMod_Ctrl | ImGuiMod_Shift;
    case 5:
        return ImGuiMod_Ctrl | ImGuiMod_Alt;
    case 6:
        return ImGuiMod_Alt | ImGuiMod_Shift;
    case 7:
        return ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Shift;
    default:
        return 0;
    }
}

bool KeybindManager::isModifierKey(ImGuiKey key) {
    return (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
            key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
            key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
            key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper ||
            key == ImGuiKey_ReservedForModCtrl || key == ImGuiKey_ReservedForModShift ||
            key == ImGuiKey_ReservedForModAlt || key == ImGuiKey_ReservedForModSuper);
}

ImGuiKey KeybindManager::getBaseKey(ImGuiKeyChord chord) {
    ImGuiKey key = (ImGuiKey)(chord & ~ImGuiMod_Mask_);
    if (isModifierKey(key))
        return ImGuiKey_None;
    return key;
}

ImGuiKeyChord KeybindManager::makeChord(int modMask, ImGuiKey baseKey) {
    if (baseKey == ImGuiKey_None || isModifierKey(baseKey))
        return modMask & ImGuiMod_Mask_;
    return (modMask & ImGuiMod_Mask_) | baseKey;
}

const char* KeybindManager::getModifierComboString() {
    return "None\0Ctrl\0Shift\0Alt\0Ctrl+Shift\0Ctrl+Alt\0Alt+Shift\0Ctrl+Alt+Shift\0";
}

void KeybindManager::load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        save(path);
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ss(line);
        std::string idStr;
        int chordVal = 0;
        if (ss >> idStr >> chordVal) {
            for (auto& b : binds) {
                if (b.idStr == idStr) {
                    b.chord = (ImGuiKeyChord)chordVal;
                    break;
                }
            }
        }
    }
}

void KeybindManager::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open())
        return;

    out << "# FVD++ Keybindings Configuration\n";
    for (const auto& b : binds) {
        out << b.idStr << " " << (int)b.chord << " # " << formatChord(b.chord) << "\n";
    }
}

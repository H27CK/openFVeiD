/*
#    FVD++, an advanced coaster design tool
#    Copyright (C) 2026 Veia <h27ck@proton.me>
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DUMMIES_H
#define DUMMIES_H

#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <glm/glm.hpp>
#include <imgui.h>

// Forward declarations
class track;
class section;
class subfunc;

struct DummyOptions {
    bool autoFocusOnSelection = false;
    bool drawGrid = true;
    bool editShadows = true;
    bool enableCurveChasing = false;
    bool enforceMinRadius = false;
    bool glbShadowsEnabled = false;  // Experimental
    bool graphOverlayEnabled = true; // Telemetry overlay
    bool lookAheadPovSmoothing = true;
    bool mistEnabled = false;
    bool relativeExport = false; // Relative NoLimits 2 export coordinate behaviour
    bool shadowsEnabled = true;
    bool showFPS = true;
    bool skyboxEnabled = true;
    bool softShadowsEnabled = true;
    bool strictCustomStyleLock = true;
    bool trackTextureEnabled = true;
    bool transparentGraphs = false;
    bool useLegacyHeartline = false;
    bool vSync = true;

    float ambientLightStrength = 0.72f;
    float fontSize = 15.0f;
    float fov = 90.0f;
    float graphSpacingLimit = 0.1f;
    float gridMajorThickness = 1.5f;
    float gridMinorThickness = 1.0f;
    float minRadius = 5.0f;
    float mistFar = 270.0f;
    float mistNear = 100.0f;
    float mouseSensitivity = 1.0f;
    float scrollCtrlIncrement = 1.0f;
    float scrollIncrement = 0.1f;
    float scrollShiftIncrement = 10.0f;
    float skyboxRotation = 0.0f;
    float sprintMultiplier = 2.0f;
    float stallSpeed = 0.1f;
    float sunLightStrength = 1.0f;
    float sunPitch = -90.0f; // Directly above
    float sunYaw = 0.0f;

    int defaultTransitionLateral = 4; // quartic (Lateral Force default)
    int defaultTransitionNormal = 2;  // cubic (Normal Force default)
    int defaultTransitionPitch = 2;   // cubic (Pitch default)
    int defaultTransitionRoll = 4;    // quartic (Roll Rate default)
    int defaultTransitionYaw = 2;     // cubic (Yaw default)
    int keyAppendTransition = ImGuiKey_RightBracket;
    int keyBackward = 0;
    int keyForward = 0;
    int keyLeft = 0;
    int keyOverlayScenery = 0;
    int keyOverlayWarnings = 0;
    int keyPrependTransition = ImGuiKey_LeftBracket;
    int keyRight = 0;
    int keyViewFront = ImGuiKey_None;
    int keyViewPerspective = ImGuiKey_None;
    int keyViewSide = ImGuiKey_None;
    int keyViewTop = ImGuiKey_None;
    int maxUndoChanges = 64;
    int measures = 0; // 0: Metric (m/s), 1: Metric (kph), 2: English (mph)
    int meshQuality = 1;
    int msaaSamples = 4;
    int screenshotMultiplier = 2;
    int targetFPS = 60;
    int theme = 0; // 0: Dark, 1: Light, 2: Classic

    glm::vec3 ambientLightColor = glm::vec3(1.0f);
    glm::vec3 backgroundColor = glm::vec3(0.5f, 0.5f, 0.5f);
    glm::vec3 floorColor = glm::vec3(213.0f / 255.0f, 213.0f / 255.0f, 213.0f / 255.0f);
    glm::vec3 gridLineColor = glm::vec3(0.6f, 0.6f, 0.6f);
    glm::vec3 mistColor = glm::vec3(0.5f, 0.5f, 0.5f);
    glm::vec3 shadowColor = glm::vec3(0.2f, 0.2f, 0.2f);
    glm::vec3 sunLightColor = glm::vec3(1.0f);

    std::string lastEnvPreset = "";
    std::string skyboxName = "Solid Color";

    glm::vec3 graphColors[18] = {
        glm::vec3(1.0f, 0.2f, 0.2f), // EditRoll (0)
        glm::vec3(0.4f, 0.7f, 1.0f), // EditNormal (1)
        glm::vec3(0.2f, 1.0f, 0.2f), // EditLateral (2)
        glm::vec3(1.0f, 0.5f, 0.0f), // Banking (3) - Orange
        glm::vec3(0.7f, 0.2f, 0.8f), // RollSpeed (4) - Purple
        glm::vec3(0.5f, 0.1f, 0.6f), // TrueRollSpeed (5) - Dark Purple
        glm::vec3(1.0f, 0.3f, 0.6f), // RollAccel (6) - Pink
        glm::vec3(0.0f, 0.7f, 0.7f), // NForce (7) - Teal
        glm::vec3(0.5f, 0.8f, 1.0f), // NForceChange (8) - Sky Blue
        glm::vec3(0.6f, 0.9f, 0.0f), // LForce (9) - Lime
        glm::vec3(0.4f, 0.6f, 0.3f), // LForceChange (10) - Sage
        glm::vec3(0.9f, 0.7f, 0.1f), // PitchChange (11) - Gold
        glm::vec3(0.7f, 0.5f, 1.0f), // YawChange (12) - Lavender
        glm::vec3(0.9f, 0.4f, 0.1f), // WorldPitchChange (13) - Orange-red
        glm::vec3(0.1f, 0.9f, 0.4f), // WorldYawChange (14) - Bright Green
        glm::vec3(0.2f, 0.8f, 0.8f), // Velocity (15) - Cyan
        glm::vec3(0.8f, 0.8f, 0.2f), // WorldPitch (16) - Yellow
        glm::vec3(0.9f, 0.2f, 0.4f)  // Acceleration (17) - Pink-Red
    };

    void save(const std::string& path) const {
        std::ofstream out(path);
        if (out) {
            out << "FVD_OPT_V1 "
                << autoFocusOnSelection << " "
                << drawGrid << " "
                << editShadows << " "
                << enableCurveChasing << " "
                << enforceMinRadius << " "
                << glbShadowsEnabled << " "
                << graphOverlayEnabled << " "
                << lookAheadPovSmoothing << " "
                << mistEnabled << " "
                << relativeExport << " "
                << shadowsEnabled << " "
                << showFPS << " "
                << skyboxEnabled << " "
                << softShadowsEnabled << " "
                << strictCustomStyleLock << " "
                << trackTextureEnabled << " "
                << transparentGraphs << " "
                << useLegacyHeartline << " "
                << vSync << " "
                << ambientLightStrength << " "
                << fontSize << " "
                << fov << " "
                << graphSpacingLimit << " "
                << gridMajorThickness << " "
                << gridMinorThickness << " "
                << minRadius << " "
                << mistFar << " "
                << mistNear << " "
                << mouseSensitivity << " "
                << scrollCtrlIncrement << " "
                << scrollIncrement << " "
                << scrollShiftIncrement << " "
                << skyboxRotation << " "
                << sprintMultiplier << " "
                << stallSpeed << " "
                << sunLightStrength << " "
                << sunPitch << " "
                << sunYaw << " "
                << defaultTransitionLateral << " "
                << defaultTransitionNormal << " "
                << defaultTransitionPitch << " "
                << defaultTransitionRoll << " "
                << defaultTransitionYaw << " "
                << keyAppendTransition << " "
                << keyBackward << " "
                << keyForward << " "
                << keyLeft << " "
                << keyOverlayScenery << " "
                << keyOverlayWarnings << " "
                << keyPrependTransition << " "
                << keyRight << " "
                << keyViewFront << " "
                << keyViewPerspective << " "
                << keyViewSide << " "
                << keyViewTop << " "
                << maxUndoChanges << " "
                << measures << " "
                << meshQuality << " "
                << msaaSamples << " "
                << screenshotMultiplier << " "
                << targetFPS << " "
                << theme << " "
                << ambientLightColor.x << " " << ambientLightColor.y << " " << ambientLightColor.z << " "
                << backgroundColor.x << " " << backgroundColor.y << " " << backgroundColor.z << " "
                << floorColor.x << " " << floorColor.y << " " << floorColor.z << " "
                << gridLineColor.x << " " << gridLineColor.y << " " << gridLineColor.z << " "
                << mistColor.x << " " << mistColor.y << " " << mistColor.z << " "
                << shadowColor.x << " " << shadowColor.y << " " << shadowColor.z << " "
                << sunLightColor.x << " " << sunLightColor.y << " " << sunLightColor.z << " "
                << std::quoted(lastEnvPreset) << " "
                << std::quoted(skyboxName) << "\n";
            for (int i = 0; i < 18; ++i) {
                out << graphColors[i].x << " " << graphColors[i].y << " " << graphColors[i].z << " ";
            }
            out << "\n";
        }
    }

    void load(const std::string& path) {
        std::ifstream in(path);
        if (in) {
            std::string version;
            if (!(in >> version))
                return;

            if (version == "FVD_OPT_V1") {
                in >> autoFocusOnSelection >> drawGrid >> editShadows >> enableCurveChasing >> enforceMinRadius >> glbShadowsEnabled >> graphOverlayEnabled >> lookAheadPovSmoothing >> mistEnabled >> relativeExport >> shadowsEnabled >> showFPS >> skyboxEnabled >> softShadowsEnabled >> strictCustomStyleLock >> trackTextureEnabled >> transparentGraphs >> useLegacyHeartline >> vSync >> ambientLightStrength >> fontSize >> fov >> graphSpacingLimit >> gridMajorThickness >> gridMinorThickness >> minRadius >> mistFar >> mistNear >> mouseSensitivity >> scrollCtrlIncrement >> scrollIncrement >> scrollShiftIncrement >> skyboxRotation >> sprintMultiplier >> stallSpeed >> sunLightStrength >> sunPitch >> sunYaw >> defaultTransitionLateral >> defaultTransitionNormal >> defaultTransitionPitch >> defaultTransitionRoll >> defaultTransitionYaw >> keyAppendTransition >> keyBackward >> keyForward >> keyLeft >> keyOverlayScenery >> keyOverlayWarnings >> keyPrependTransition >> keyRight >> keyViewFront >> keyViewPerspective >> keyViewSide >> keyViewTop >> maxUndoChanges >> measures >> meshQuality >> msaaSamples >> screenshotMultiplier >> targetFPS >> theme >> ambientLightColor.x >> ambientLightColor.y >> ambientLightColor.z >> backgroundColor.x >> backgroundColor.y >> backgroundColor.z >> floorColor.x >> floorColor.y >> floorColor.z >> gridLineColor.x >> gridLineColor.y >> gridLineColor.z >> mistColor.x >> mistColor.y >> mistColor.z >> shadowColor.x >> shadowColor.y >> shadowColor.z >> sunLightColor.x >> sunLightColor.y >> sunLightColor.z >> std::quoted(lastEnvPreset) >> std::quoted(skyboxName);

                for (int i = 0; i < 18; ++i) {
                    if (!(in >> graphColors[i].x >> graphColors[i].y >> graphColors[i].z))
                        break;
                }
            }
        }
    }

    std::string getSpeedString() const {
        if (measures == 0)
            return "m/s";
        if (measures == 1)
            return "km/h";
        return "mph";
    }
    std::string getLengthString() const {
        if (measures == 2)
            return "ft";
        return "m";
    }
    float getSpeedFactor() const {
        if (measures == 0)
            return 1.0f;
        if (measures == 1)
            return 3.6f;
        return 2.2369356f;
    }
    float getLengthFactor() const {
        if (measures == 2)
            return 1.0f / 0.3048f;
        return 1.0f;
    }
};

struct DummyGlobal {
    track* currentTrack = nullptr;
    track* curTrack() {
        return currentTrack;
    }
    subfunc* selectedFunc = nullptr;
    DummyOptions* mOptions = new DummyOptions();

    float projectGrdTexSize = 440.0f;
    std::string projectGroundTex = "";
    float projectGrdHeight = 0.0f;
    bool skyboxAvailable = false;
    struct GlbSettings {
        std::string path;
        bool visible = true;
    };
    std::vector<GlbSettings> projectGlbs;

    void resetEnvironment() {
        projectGrdTexSize = 440.0f;
        projectGroundTex = "";
        projectGrdHeight = 0.0f;
        projectGlbs.clear();
    }

    void updateInfoPanel() {}
    void updateProjectWidget() {}
    void updateProjectWidget(int) {}
    void showMessage(const std::string&, int) {}
    void updateUndoRedo() {}
    void* getDummyParent() {
        return nullptr;
    }
};

extern DummyGlobal* gloParent;

class Viewport;
extern Viewport* gViewport;

struct DummyGLView {
    glm::vec3 cameraPos = glm::vec3(0.f);
};

extern DummyGLView* glView;

#endif // DUMMIES_H

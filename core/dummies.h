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
#include <sstream>
#include <glm/glm.hpp>

// Forward declarations
class track;
class section;
class subfunc;

struct DummyOptions {
    int meshQuality = 1;
    int maxUndoChanges = 64;
    int measures = 0; // 0: Metric (m/s), 1: Metric (kph), 2: English (mph)
    bool drawGrid = true;
    bool enforceMinRadius = false;
    float minRadius = 5.0f;
    glm::vec3 backgroundColor = glm::vec3(80.f / 255.f, 140.f / 255.f, 160.f / 255.f);
    glm::vec3 floorColor = glm::vec3(1.0f, 1.0f, 1.0f);
    int theme = 0; // 0: Dark, 1: Light, 2: Classic
    bool shadowsEnabled = true;
    bool transparentGraphs = false;
    float fov = 90.0f;
    float mouseSensitivity = 1.0f;
    float sprintMultiplier = 2.0f;
    int keyForward = 0;
    int keyBackward = 0;
    int keyLeft = 0;
    int keyRight = 0;
    bool autoFocusOnSelection = false;
    float fontSize = 15.0f;
    int screenshotMultiplier = 2;
    int msaaSamples = 4; // 0: Off, 2, 4, 8

    int targetFPS = 60;
    bool showFPS = true;
    bool vSync = true;

    // Mist Settings
    bool mistEnabled = false;
    float mistNear = 100.0f;
    float mistFar = 270.0f;
    glm::vec3 mistColor = glm::vec3(0.5f, 0.5f, 0.5f);

    bool skyboxEnabled = true;
    bool strictCustomStyleLock = true;
    bool editShadows = true;
    bool stlShadowsEnabled = false; // Experimental
    float sunPitch = -90.0f;        // Directly above
    float sunYaw = 0.0f;
    float stallSpeed = 0.1f;
    float graphSpacingLimit = 0.1f;
    float scrollCtrlIncrement = 1.0f;
    float scrollIncrement = 0.1f;
    float scrollShiftIncrement = 10.0f;

    glm::vec3 graphColors[14] = {
        glm::vec3(1.0f, 0.2f, 0.2f), // EditRoll
        glm::vec3(0.4f, 0.7f, 1.0f), // EditNormal
        glm::vec3(0.2f, 1.0f, 0.2f), // EditLateral
        glm::vec3(1.0f, 0.5f, 0.0f), // Banking (Orange)
        glm::vec3(0.7f, 0.2f, 0.8f), // RollSpeed (Purple)
        glm::vec3(1.0f, 0.3f, 0.6f), // RollAccel (Pink)
        glm::vec3(0.0f, 0.7f, 0.7f), // NForce (Teal)
        glm::vec3(0.5f, 0.8f, 1.0f), // NForceChange (Sky Blue)
        glm::vec3(0.6f, 0.9f, 0.0f), // LForce (Lime)
        glm::vec3(0.4f, 0.6f, 0.3f), // LForceChange (Sage)
        glm::vec3(0.9f, 0.7f, 0.1f), // PitchChange (Gold)
        glm::vec3(0.7f, 0.5f, 1.0f), // YawChange (Lavender)
        glm::vec3(0.9f, 0.4f, 0.1f), // WorldPitchChange (Orange)
        glm::vec3(0.1f, 0.9f, 0.4f)  // WorldYawChange (Bright Green)
    };

    void save(const std::string& path) const {
        std::ofstream out(path);
        if (out) {
            out << "FVD_OPT_V1 "
                << autoFocusOnSelection << " "
                << backgroundColor.x << " " << backgroundColor.y << " " << backgroundColor.z << " "
                << drawGrid << " "
                << editShadows << " "
                << enforceMinRadius << " "
                << floorColor.x << " " << floorColor.y << " " << floorColor.z << " "
                << fontSize << " "
                << fov << " "
                << keyBackward << " "
                << keyForward << " "
                << keyLeft << " "
                << keyRight << " "
                << maxUndoChanges << " "
                << measures << " "
                << meshQuality << " "
                << minRadius << " "
                << mistColor.x << " " << mistColor.y << " " << mistColor.z << " "
                << mistEnabled << " "
                << mistFar << " "
                << mistNear << " "
                << mouseSensitivity << " "
                << msaaSamples << " "
                << screenshotMultiplier << " "
                << scrollCtrlIncrement << " "
                << scrollIncrement << " "
                << scrollShiftIncrement << " "
                << shadowsEnabled << " "
                << showFPS << " "
                << skyboxEnabled << " "
                << sprintMultiplier << " "
                << stallSpeed << " "
                << stlShadowsEnabled << " "
                << strictCustomStyleLock << " "
                << sunPitch << " "
                << sunYaw << " "
                << targetFPS << " "
                << theme << " "
                << transparentGraphs << " "
                << vSync << "\n";
            for (int i = 0; i < 14; ++i) {
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
                in >> autoFocusOnSelection >> backgroundColor.x >> backgroundColor.y >> backgroundColor.z >> drawGrid >> editShadows >> enforceMinRadius >> floorColor.x >> floorColor.y >> floorColor.z >> fontSize >> fov >> keyBackward >> keyForward >> keyLeft >> keyRight >> maxUndoChanges >> measures >> meshQuality >> minRadius >> mistColor.x >> mistColor.y >> mistColor.z >> mistEnabled >> mistFar >> mistNear >> mouseSensitivity >> msaaSamples >> screenshotMultiplier >> scrollCtrlIncrement >> scrollIncrement >> scrollShiftIncrement >> shadowsEnabled >> showFPS >> skyboxEnabled >> sprintMultiplier >> stallSpeed >> stlShadowsEnabled >> strictCustomStyleLock >> sunPitch >> sunYaw >> targetFPS >> theme >> transparentGraphs >> vSync;

                for (int i = 0; i < 14; ++i) {
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
    bool skyboxAvailable = false;
    struct StlSettings {
        std::string path;
        glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
        bool visible = true;
        bool showWireframe = false;
    };
    std::vector<StlSettings> projectStls;

    void resetEnvironment() {
        projectGrdTexSize = 440.0f;
        projectGroundTex = "";
        projectStls.clear();
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

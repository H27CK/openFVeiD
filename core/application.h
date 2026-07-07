/*
#    FVD++, an advanced coaster design tool
#    Copyright (C) 2026 Veia <h27ck@proton.me>
#    Copyright (C) 2026 Ercan Akyürek <ercan.akyuerek@gmail.com>
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

#pragma once

#include "core/dummies.h"
#include "renderer/viewport.h"
#include "ui/graphview.h"
#include "ui/leftpanel.h"
#include "ui/transitionview.h"
#include "core/trackhandler.h"
#include "core/track.h"
#include "core/section.h"
#include "core/mnode.h"
#include "core/globalundohandler.h"
#include "renderer/trackmesh.h"
#include "portable-file-dialogs.h"
#include "core/saver.h"
#include "core/exportfuncs.h"
#include "core/common.h"
#include "core/assets.h"
#include "core/logger.h"
#include "core/crashhandler.h"

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "renderer/vulkan/vulkancontext.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "implot.h"

class Application {
public:
    Application();
    ~Application();

    bool Initialize();
    void Run();
    void Shutdown();

    // Application State
    std::vector<trackHandler*> trackList;
    int activeTrackIdx = -1;
    GlobalUndoHandler* mUndoHandler = nullptr;

    void pushUndo() {
        if (mUndoHandler)
            mUndoHandler->pushSnapshot();
    }

private:
    void Update(float deltaTime);
    void Render(float deltaTime);
    void HandleShortcuts();
    void PerformExport(const std::string& path);
    void PerformIncrementalSave();

    // FPS Limiting / Pacing
    void FramePacing(double frameStartTime);

    GLFWwindow* window = nullptr;
    VulkanContext vulkanContext;

    std::string currentFilePath = "";
    bool firstFrame = true;
    bool showOptions = false;
    bool showAboutDialog = false;
    bool showExitPopup = false;
    bool viewportActive = false;

    // Export State
    bool showExportPopup = false;
    int exportFormat = 1; // NL2 CSV
    float exportDistPerNode = 1.0f;
    float exportRollThresh = 30.0f;
    int exportFromSection = 0;
    int exportToSection = -1;
    bool exportNoHeartline = false;
    std::string lastExportPath = "";

    // UI Components
    Viewport viewport;
    GraphView graphView;
    LeftPanel leftPanel;
    TransitionView transitionView;
    ImVec4 mistColor;

    // FPS Tracking
    double lastFPSUpdate = 0;
    int frameCount = 0;
    float currentFPS = 0.0f;
    double lastTime = 0.0;

    // Mouse State
    double last_mx = 0, last_my = 0;
    double mouseDeltaX = 0, mouseDeltaY = 0;
};

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

#include "core/application.h"
#include "core/workingdirectory.h"
#include "customstyle.h"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <timeapi.h>
#endif

#include <fstream>
#include <thread>
#include <chrono>

extern DummyGlobal* gloParent;
extern DummyGLView* glView;
extern Viewport* gViewport;

Application::Application() {
    mUndoHandler = new GlobalUndoHandler(this, gloParent->mOptions->maxUndoChanges);
}

Application::~Application() {
    delete mUndoHandler;
}

bool Application::Initialize() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    initializeWorkingDirectory();
    common::InitLogger("fvd.log");
    common::InitCrashHandler();

    LOG_INFO("FVD++ starting up...");

    gloParent->mOptions->keyForward = ImGuiKey_W;
    gloParent->mOptions->keyBackward = ImGuiKey_S;
    gloParent->mOptions->keyLeft = ImGuiKey_A;
    gloParent->mOptions->keyRight = ImGuiKey_D;

    if (!std::filesystem::exists("track_styles")) {
        std::filesystem::create_directory("track_styles");
    }

    if (!std::filesystem::exists("skybox")) {
        std::filesystem::create_directory("skybox");
    }

    gloParent->mOptions->load("options.cfg");

    glfwSetErrorCallback([](int error, const char* description) {
        std::cerr << "GLFW Error " << error << ": " << description << std::endl;
    });

    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

#ifndef FVD_VERSION
#define FVD_VERSION "unknown"
#endif

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    window = glfwCreateWindow(mode->width, mode->height, "FVD++", NULL, NULL);
    if (window == NULL)
        return false;

#ifdef _WIN32
    HWND hwnd = glfwGetWin32Window(window);
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
    if (hIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
#endif

    glfwMaximizeWindow(window);

    vulkanContext.setVSync(gloParent->mOptions->vSync);
    if (!vulkanContext.initialize(window)) {
        std::cerr << "Failed to initialize Vulkan!" << std::endl;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDragClickToInputText = true;

    float currentFontSize = gloParent->mOptions->fontSize;
    if (currentFontSize < 8.0f)
        currentFontSize = 8.0f;
    if (currentFontSize > 64.0f)
        currentFontSize = 64.0f;

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;

    const AssetData* fontAsset = getEmbeddedAsset("fonts/Roboto-Medium.ttf");
    if (fontAsset) {
        font_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)fontAsset->data, (int)fontAsset->size, currentFontSize, &font_cfg);
    } else {
        io.Fonts->AddFontDefault(&font_cfg);
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // Apply theme from options
    if (gloParent->mOptions->theme == 0) {
        ImGui::StyleColorsDark();
        ImPlot::StyleColorsDark();
    } else if (gloParent->mOptions->theme == 1) {
        ImGui::StyleColorsLight();
        ImPlot::StyleColorsLight();
    } else if (gloParent->mOptions->theme == 2) {
        ImGui::StyleColorsClassic();
        ImPlot::StyleColorsClassic();
    }

    style.ScaleAllSizes(currentFontSize / 15.0f);
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForVulkan(window, true);

    static VkFormat swapchainColorFormat = vulkanContext.swapchainFormat;
    ImGui_ImplVulkan_InitInfo vulkanInitInfo = {};
    vulkanInitInfo.ApiVersion = VK_API_VERSION_1_3;
    vulkanInitInfo.Instance = vulkanContext.instance;
    vulkanInitInfo.PhysicalDevice = vulkanContext.physicalDevice;
    vulkanInitInfo.Device = vulkanContext.device;
    vulkanInitInfo.QueueFamily = vulkanContext.graphicsQueueFamily;
    vulkanInitInfo.Queue = vulkanContext.graphicsQueue;
    vulkanInitInfo.DescriptorPoolSize = 64;
    vulkanInitInfo.MinImageCount = 2;
    vulkanInitInfo.ImageCount = vulkanContext.swapchainImageCount;
    vulkanInitInfo.UseDynamicRendering = true;
    vulkanInitInfo.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchainColorFormat,
        .depthAttachmentFormat = VulkanContext::depthFormat,
        .stencilAttachmentFormat = VulkanContext::depthFormat,
    };
    if (!ImGui_ImplVulkan_Init(&vulkanInitInfo)) {
        std::cerr << "Failed to initialize the ImGui Vulkan backend!" << std::endl;
        return false;
    }

    gViewport = &viewport;
    viewport.initialize(1280, 720);

    for (const auto& stl : gloParent->projectStls) {
        viewport.addStlMesh(stl.path);
        if (!viewport.stlMeshes.empty()) {
            viewport.stlMeshes.back().color = stl.color;
            viewport.stlMeshes.back().visible = stl.visible;
            viewport.stlMeshes.back().showWireframe = stl.showWireframe;
        }
    }

    if (!gloParent->projectGroundTex.empty()) {
        viewport.loadGroundTexture(gloParent->projectGroundTex);
    }
    viewport.setGroundTextureSize(gloParent->projectGrdTexSize);

    viewport.setMistColor(gloParent->mOptions->mistColor);
    viewport.setShadowMode(gloParent->mOptions->shadowsEnabled ? 1 : 0);
    viewport.setFOV(gloParent->mOptions->fov);
    viewport.setMSAASamples(gloParent->mOptions->msaaSamples);

    mistColor = ImVec4(gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b, 1.0f);
    lastTime = glfwGetTime();
    glfwGetCursorPos(window, &last_mx, &last_my);
    lastFPSUpdate = glfwGetTime();

    return true;
}

void Application::Run() {
    double targetDuration = 1.0 / (double)std::max(1, gloParent->mOptions->targetFPS);
    double nextFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window) || showExitPopup) {
        double frameStartTime = glfwGetTime();

        // 1. Process OS Events
        // If we are navigating the camera, we poll as fast as possible.
        // Otherwise, we poll once and move to rendering.
        if (viewportActive) {
            glfwPollEvents();
        } else {
            glfwPollEvents();
        }

        if (glfwWindowShouldClose(window)) {
            if (viewportActive) {
                viewportActive = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            }
            if (!showExitPopup) {
                showExitPopup = true;
            }
            glfwSetWindowShouldClose(window, false);
        }

        // 2. Update Application State
        float currentTime = (float)glfwGetTime();
        float deltaTime = currentTime - (float)lastTime;
        lastTime = currentTime;

        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        mouseDeltaX = mx - last_mx;
        mouseDeltaY = my - last_my;
        last_mx = mx;
        last_my = my;

        HandleShortcuts();

        // Sync application clear color with mist settings
        mistColor = ImVec4(gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b, 1.0f);

        if (viewportActive) {
            ImGui::GetIO().AddMousePosEvent(-1000000.0f, -1000000.0f);
        }

        if (!vulkanContext.acquireFrame()) {
            continue;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (viewportActive) {
            ImGui::SetWindowFocus("Viewport");
        }

        static bool was_dragging_widget = false;
        bool is_dragging_widget = ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
        if (is_dragging_widget && !was_dragging_widget) {
            if (ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                was_dragging_widget = true;
            }
        } else if (!is_dragging_widget && was_dragging_widget) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            was_dragging_widget = false;
        }

        // 3. Render
        Render(deltaTime);

        ImGui::Render();
        vulkanContext.beginSwapchainRendering(mistColor.x * mistColor.w, mistColor.y * mistColor.w,
                                              mistColor.z * mistColor.w);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vulkanContext.currentCommandBuffer());

        // 4. Present
        vulkanContext.endFrame();

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // 5. Frame Pacing (VSync OFF path)
        if (!gloParent->mOptions->vSync) {
            targetDuration = 1.0 / (double)std::max(1, gloParent->mOptions->targetFPS);
            nextFrameTime += targetDuration;

            double now = glfwGetTime();
            if (now < nextFrameTime) {
                // If the application is idle, use WaitEvents to save CPU.
                // If we are navigating the camera, use Sleep to keep OS responsive but spin for precision.
                if (!viewportActive) {
                    double waitTime = (nextFrameTime - now);
                    // Leave 0.5ms for precision spin-lock
                    if (waitTime > 0.0005) {
                        glfwWaitEventsTimeout(waitTime - 0.0005);
                    }
                } else {
                    double sleepTime = (nextFrameTime - now);
                    // Leave 1.0ms for precision spin-lock during interaction
                    if (sleepTime > 0.001) {
                        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>((sleepTime - 0.001) * 1000000.0)));
                    }
                }

                // Final precision spin-lock to hit the exact microsecond target
                while (glfwGetTime() < nextFrameTime) {
                    // Do nothing, just wait.
                }
            } else if (now > nextFrameTime + 1.0) {
                // If we fall behind by more than 1 second, reset the target
                nextFrameTime = now;
            }
        } else {
            // With VSync ON, we reset nextFrameTime to current to avoid massive catch-up attempts if toggled off
            nextFrameTime = glfwGetTime();
        }
    }
}

void Application::HandleShortcuts() {
    auto exitViewport = [&]() {
        if (viewportActive) {
            viewportActive = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
    };

    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (ImGui::GetIO().KeyShift) {
                if (mUndoHandler)
                    mUndoHandler->doRedo();
            } else {
                if (mUndoHandler)
                    mUndoHandler->doUndo();
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            if (mUndoHandler)
                mUndoHandler->doRedo();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (ImGui::GetIO().KeyAlt) {
                PerformIncrementalSave();
            } else {
                if (currentFilePath.empty()) {
                    exitViewport();
                    auto f = pfd::save_file("Save project", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                    if (!f.empty()) {
                        if (f.find(".fvd") == std::string::npos)
                            f += ".fvd";
                        currentFilePath = f;
                    }
                }
                if (!currentFilePath.empty()) {
                    saver saveObj(currentFilePath, trackList);
                    saveObj.doSave();
                    LOG_INFO("Saved project: %s", currentFilePath.c_str());
                    pfd::notify("Project Saved", "Successfully saved project to:\n" + currentFilePath, pfd::icon::info);
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
            if (hasActiveTrack) {
                if (!lastExportPath.empty()) {
                    PerformExport(lastExportPath);
                } else {
                    exitViewport();
                    showExportPopup = true;
                }
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1, false))
            viewport.setTrackShaderMode(0);
        if (ImGui::IsKeyPressed(ImGuiKey_2, false))
            viewport.setTrackShaderMode(1);
        if (ImGui::IsKeyPressed(ImGuiKey_3, false))
            viewport.setTrackShaderMode(2);
        if (ImGui::IsKeyPressed(ImGuiKey_4, false))
            viewport.setTrackShaderMode(3);
        if (ImGui::IsKeyPressed(ImGuiKey_5, false))
            viewport.setTrackShaderMode(4);
        if (ImGui::IsKeyPressed(ImGuiKey_6, false))
            viewport.setTrackShaderMode(5);
    }

    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            exitViewport();
            auto f = pfd::save_file("Save screenshot", "screenshot.png", {"Image Files", "*.png", "All Files", "*"}).result();
            if (!f.empty()) {
                if (f.find(".png") == std::string::npos)
                    f += ".png";
                viewport.captureScreenshot(gloParent->mOptions->screenshotMultiplier, f, trackList);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Period, false)) {
            viewport.resetView();
        }
    }
}

void Application::Render(float deltaTime) {
    auto exitViewport = [&]() {
        if (viewportActive) {
            viewportActive = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
    };

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project")) {
                for (auto t : trackList)
                    delete t;
                trackList.clear();
                activeTrackIdx = -1;
                gloParent->selectedFunc = nullptr;
                currentFilePath = "";
                gloParent->resetEnvironment();
                viewport.setGroundTextureSize(gloParent->projectGrdTexSize);
                viewport.loadGroundTexture(gloParent->projectGroundTex);
                while (!viewport.stlMeshes.empty())
                    viewport.removeStlMesh(0);
                if (mUndoHandler) {
                    mUndoHandler->clearActions();
                    mUndoHandler->pushSnapshot();
                }
            }
            if (ImGui::MenuItem("Open...")) {
                exitViewport();
                auto f = pfd::open_file("Choose project file", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                if (!f.empty()) {
                    saver loadObj(f[0], trackList);
                    loadObj.doLoad();
                    activeTrackIdx = trackList.empty() ? -1 : 0;
                    gloParent->selectedFunc = nullptr;
                    currentFilePath = f[0];
                    LOG_INFO("Loaded project: %s", currentFilePath.c_str());

                    viewport.setGroundTextureSize(gloParent->projectGrdTexSize);
                    viewport.loadGroundTexture(gloParent->projectGroundTex);
                    while (!viewport.stlMeshes.empty())
                        viewport.removeStlMesh(0);
                    std::vector<DummyGlobal::StlSettings> validStls;
                    for (const auto& stl : gloParent->projectStls) {
                        if (viewport.addStlMesh(stl.path)) {
                            viewport.stlMeshes.back().color = stl.color;
                            viewport.stlMeshes.back().visible = stl.visible;
                            viewport.stlMeshes.back().showWireframe = stl.showWireframe;
                            validStls.push_back(stl);
                        }
                    }
                    gloParent->projectStls = validStls;
                    if (mUndoHandler) {
                        mUndoHandler->clearActions();
                        mUndoHandler->pushSnapshot();
                    }
                }
            }
            ImGui::Separator();
            bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
            if (ImGui::BeginMenu("Import")) {
                if (ImGui::MenuItem("Track(s)...")) {
                    exitViewport();
                    auto f = pfd::open_file("Choose project to import from", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                    if (!f.empty()) {
                        saver loadObj(f[0], trackList);
                        loadObj.doLoad(true);
                        activeTrackIdx = trackList.empty() ? -1 : (int)trackList.size() - 1;
                        gloParent->selectedFunc = nullptr;
                        LOG_INFO("Imported tracks from: %s", f[0].c_str());
                        if (mUndoHandler)
                            mUndoHandler->pushSnapshot();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Measurement Points...", nullptr, false, hasActiveTrack)) {
                    exitViewport();
                    auto f = pfd::open_file("Import Measurement Points", ".", {"FVD Measurement Files", "*.fvdmeasure"}).result();
                    if (!f.empty())
                        trackList[activeTrackIdx]->trackData->importMeasurementPoints(f[0]);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Export")) {
                if (ImGui::MenuItem("Track...", "Ctrl+E", false, hasActiveTrack)) {
                    exitViewport();
                    showExportPopup = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Parametric Track Style...", nullptr, false, hasActiveTrack)) {
                    exitViewport();
                    auto f = pfd::save_file("Export Parametric Style", "custom.fvdstyle", {"FVD Style Files", "*.fvdstyle"}).result();
                    if (!f.empty())
                        trackList[activeTrackIdx]->trackData->exportParametricStyle(f);
                }
                if (ImGui::MenuItem("Measurement Points...", nullptr, false, hasActiveTrack)) {
                    exitViewport();
                    auto f = pfd::save_file("Export Measurement Points", "train.fvdmeasure", {"FVD Measurement Files", "*.fvdmeasure"}).result();
                    if (!f.empty())
                        trackList[activeTrackIdx]->trackData->exportMeasurementPoints(f);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (currentFilePath.empty()) {
                    exitViewport();
                    auto f = pfd::save_file("Save project", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                    if (!f.empty()) {
                        if (f.find(".fvd") == std::string::npos)
                            f += ".fvd";
                        currentFilePath = f;
                    }
                }
                if (!currentFilePath.empty()) {
                    saver saveObj(currentFilePath, trackList);
                    saveObj.doSave();
                    LOG_INFO("Saved project: %s", currentFilePath.c_str());
                    pfd::notify("Project Saved", "Successfully saved project to:\n" + currentFilePath, pfd::icon::info);
                }
            }
            if (ImGui::MenuItem("Save As...")) {
                exitViewport();
                auto f = pfd::save_file("Save project", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                if (!f.empty()) {
                    if (f.find(".fvd") == std::string::npos)
                        f += ".fvd";
                    currentFilePath = f;
                    saver saveObj(currentFilePath, trackList);
                    saveObj.doSave();
                    LOG_INFO("Saved project: %s", currentFilePath.c_str());
                    pfd::notify("Project Saved", "Successfully saved project to:\n" + currentFilePath, pfd::icon::info);
                }
            }
            if (ImGui::MenuItem("Incremental Save", "Ctrl+Alt+S")) {
                PerformIncrementalSave();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Options...")) {
                exitViewport();
                showOptions = true;
            }
            if (ImGui::MenuItem("Reload Assets")) {
                for (auto th : trackList) {
                    if (th->mMesh) {
                        th->mMesh->clearParametricStyles();
                    }
                    track* t = th->trackData;
                    if (t) {
                        if (!t->customStyleFile.empty()) {
                            t->importParametricStyle(t->customStyleFile);
                        } else {
                            for (auto& asset : t->customAssets) {
                                if (asset.loadedModel) {
                                    delete asset.loadedModel;
                                    asset.loadedModel = nullptr;
                                }
                            }
                            t->requestUpdateTrack(0, 0);
                        }
                    }
                }
                viewport.markSceneDirty();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) {
                exitViewport();
                showExitPopup = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, mUndoHandler && mUndoHandler->canUndo())) {
                if (mUndoHandler)
                    mUndoHandler->doUndo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, mUndoHandler && mUndoHandler->canRedo())) {
                if (mUndoHandler)
                    mUndoHandler->doRedo();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout"))
                firstFrame = true;
            ImGui::Separator();
            if (ImGui::BeginMenu("Track Rendering")) {
                int shaderMode = viewport.getTrackShaderMode();
                if (ImGui::MenuItem("Nothing", "Ctrl+1", shaderMode == 0))
                    viewport.setTrackShaderMode(0);
                if (ImGui::MenuItem("Velocity", "Ctrl+2", shaderMode == 1))
                    viewport.setTrackShaderMode(1);
                if (ImGui::MenuItem("Roll Speed", "Ctrl+3", shaderMode == 2))
                    viewport.setTrackShaderMode(2);
                if (ImGui::MenuItem("Normal Force", "Ctrl+4", shaderMode == 3))
                    viewport.setTrackShaderMode(3);
                if (ImGui::MenuItem("Lateral Force", "Ctrl+5", shaderMode == 4))
                    viewport.setTrackShaderMode(4);
                if (ImGui::MenuItem("Track Flexion", "Ctrl+6", shaderMode == 5))
                    viewport.setTrackShaderMode(5);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Measurements")) {
                if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size())
                    graphView.renderMeasurementsMenu(trackList[activeTrackIdx]);
                else
                    ImGui::MenuItem("No active track", nullptr, false, false);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Viewport")) {
                ImGui::SeparatorText("Camera");
                if (ImGui::MenuItem("Reset View", "."))
                    viewport.resetView();
                ImGui::SeparatorText("Projection");
                Viewport::ViewMode mode = viewport.getViewMode();
                if (ImGui::MenuItem("Perspective", nullptr, mode == Viewport::ViewMode::Perspective))
                    viewport.setViewMode(Viewport::ViewMode::Perspective);
                if (ImGui::MenuItem("Top Ortho", nullptr, mode == Viewport::ViewMode::Top))
                    viewport.setViewMode(Viewport::ViewMode::Top);
                if (ImGui::MenuItem("Side Ortho", nullptr, mode == Viewport::ViewMode::Side))
                    viewport.setViewMode(Viewport::ViewMode::Side);
                if (ImGui::MenuItem("Front Ortho", nullptr, mode == Viewport::ViewMode::Front))
                    viewport.setViewMode(Viewport::ViewMode::Front);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Developer", false)) {
            if (ImGui::MenuItem("Test Crash (Null Pointer)")) {
                LOG_INFO("Triggering intentional crash...");
                int* p = nullptr;
                *p = 123;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("About")) {
            if (ImGui::MenuItem("Version")) {
                exitViewport();
                showAboutDialog = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    float statusBarHeight = ImGui::GetFrameHeight();
    main_viewport->WorkSize.y -= statusBarHeight;

    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport);

    main_viewport->WorkSize.y += statusBarHeight;

    if (firstFrame) {
        firstFrame = false;
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
        if (node && node->ChildNodes[0] == 0) {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_id_top_half, dock_id_bottom_half;
            ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.60f, &dock_id_top_half, &dock_id_bottom_half);

            ImGuiID dock_id_metrics, dock_id_top_content;
            ImGui::DockBuilderSplitNode(dock_id_top_half, ImGuiDir_Down, 0.10f, &dock_id_metrics, &dock_id_top_content);

            ImGuiDockNode* metrics_node = ImGui::DockBuilderGetNode(dock_id_metrics);
            if (metrics_node)
                metrics_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

            ImGuiID dock_id_left_top, dock_id_viewport;
            ImGui::DockBuilderSplitNode(dock_id_top_content, ImGuiDir_Left, 0.20f, &dock_id_left_top, &dock_id_viewport);

            ImGuiDockNode* viewport_node = ImGui::DockBuilderGetNode(dock_id_viewport);
            if (viewport_node)
                viewport_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

            ImGuiID dock_id_left_bottom, dock_id_bottom_graphs;
            ImGui::DockBuilderSplitNode(dock_id_bottom_half, ImGuiDir_Left, 0.20f, &dock_id_left_bottom, &dock_id_bottom_graphs);

            ImGui::DockBuilderDockWindow("Project Panel", dock_id_left_top);
            ImGui::DockBuilderDockWindow("Environment", dock_id_left_top);
            ImGui::DockBuilderDockWindow("Transition Editor", dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Graph List", dock_id_left_bottom);
            ImGui::DockBuilderDockWindow("Viewport", dock_id_viewport);
            ImGui::DockBuilderDockWindow("Metrics", dock_id_metrics);
            ImGui::DockBuilderDockWindow("Graphs", dock_id_bottom_graphs);
            ImGui::DockBuilderDockWindow("Resulting Graphs", dock_id_bottom_graphs);
            ImGui::DockBuilderDockWindow("Measurement Graphs", dock_id_bottom_graphs);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    if (showAboutDialog) {
        ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 150.0f, ImGui::GetMainViewport()->Size.y / 2.0f - 75.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("About FVD++", &showAboutDialog, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("FVD++ (Force Vector Design)");
        ImGui::Separator();

        ImGui::Text("Version: %s", FVD_VERSION);
        ImGui::Text("Commit: %s", GIT_COMMIT_HASH);

        ImGui::Separator();
        if (ImGui::Button("Close")) {
            showAboutDialog = false;
        }

        ImGui::End();
    }

    if (showOptions) {
        ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 200.0f, ImGui::GetMainViewport()->Size.y / 2.0f - 225.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Options", &showOptions, ImGuiWindowFlags_NoDocking);

        if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("OptionsGeneralTable", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Theme");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                static int themeIdx = gloParent->mOptions->theme;
                const char* themes[] = {"Dark", "Light", "Classic"};
                if (ImGui::Combo("##Theme", &themeIdx, themes, IM_ARRAYSIZE(themes))) {
                    gloParent->mOptions->theme = themeIdx;
                    if (themeIdx == 0) {
                        ImGui::StyleColorsDark();
                        ImPlot::StyleColorsDark();
                    } else if (themeIdx == 1) {
                        ImGui::StyleColorsLight();
                        ImPlot::StyleColorsLight();
                    } else if (themeIdx == 2) {
                        ImGui::StyleColorsClassic();
                        ImPlot::StyleColorsClassic();
                    }
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Measure");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::Combo("##Measure", &gloParent->mOptions->measures, "Metric (m, m/s)\0Metric (m, km/h)\0English (ft, mph)\0");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Max Undo Steps");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderInt("##MaxUndo", &gloParent->mOptions->maxUndoChanges, 5, 200)) {
                    gloParent->mOptions->maxUndoChanges = std::clamp(gloParent->mOptions->maxUndoChanges, 5, 200);
                    if (mUndoHandler)
                        mUndoHandler->setMaxStackSize(gloParent->mOptions->maxUndoChanges);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Transparent Graphs");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Make graph backgrounds transparent so they blend into the panel background.");
                }
                ImGui::TableNextColumn();
                ImGui::Checkbox("##TransparentGraphs", &gloParent->mOptions->transparentGraphs);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                static float initialFontSize = gloParent->mOptions->fontSize;
                bool fontSizeChanged = (gloParent->mOptions->fontSize != initialFontSize);
                if (fontSizeChanged) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                }
                ImGui::Text("Font Size");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Requires application restart for settings to take effect.");
                }
                if (fontSizeChanged) {
                    ImGui::PopStyleColor();
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (fontSizeChanged) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                }
                if (ImGui::SliderFloat("##FontSize", &gloParent->mOptions->fontSize, 10.0f, 32.0f, "%.1f px")) {
                    gloParent->mOptions->fontSize = std::clamp(gloParent->mOptions->fontSize, 10.0f, 32.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Requires application restart for settings to take effect.");
                }
                if (fontSizeChanged) {
                    ImGui::PopStyleColor();
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("OptionsGraphicsTable", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Anti-Aliasing");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                static int msaaIdx = 0;
                if (gloParent->mOptions->msaaSamples == 0)
                    msaaIdx = 0;
                else if (gloParent->mOptions->msaaSamples == 2)
                    msaaIdx = 1;
                else if (gloParent->mOptions->msaaSamples == 4)
                    msaaIdx = 2;
                else if (gloParent->mOptions->msaaSamples == 8)
                    msaaIdx = 3;
                if (ImGui::Combo("##MSAA", &msaaIdx, "Off\0 2x\0 4x\0 8x\0")) {
                    if (msaaIdx == 0)
                        gloParent->mOptions->msaaSamples = 0;
                    else if (msaaIdx == 1)
                        gloParent->mOptions->msaaSamples = 2;
                    else if (msaaIdx == 2)
                        gloParent->mOptions->msaaSamples = 4;
                    else if (msaaIdx == 3)
                        gloParent->mOptions->msaaSamples = 8;
                    viewport.setMSAASamples(gloParent->mOptions->msaaSamples);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Auto Focus");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Automatically focus/zoom the graph viewports onto the selected track section.");
                }
                ImGui::TableNextColumn();
                ImGui::Checkbox("##AutoFocus", &gloParent->mOptions->autoFocusOnSelection);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Automatically focus/zoom the graph viewports onto the selected track section.");
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Field of View");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##FOV", &gloParent->mOptions->fov, 60.0f, 175.0f, "%.1f")) {
                    gloParent->mOptions->fov = std::clamp(gloParent->mOptions->fov, 60.0f, 175.0f);
                    viewport.setFOV(gloParent->mOptions->fov);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Floor Grid");
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##FloorGrid", &gloParent->mOptions->drawGrid)) {
                    viewport.markSceneDirty();
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Floor Color");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::ColorEdit3("##FloorColor", &gloParent->mOptions->floorColor.x)) {
                    viewport.markSceneDirty();
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("FPS Limit");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##FPSLimit", &gloParent->mOptions->targetFPS, 30, 400, "%d FPS");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Limits the maximum frame rate to save CPU/GPU power.\nOnly active when VSync is OFF.");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Mesh Quality");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##MeshQuality", &gloParent->mOptions->meshQuality, "Low\0Medium\0High\0Extreme\0Insane\0")) {
                    for (auto track : trackList)
                        if (track->mMesh)
                            track->mMesh->buildMeshes(0);
                    viewport.markSceneDirty();
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Screenshot Res.");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Multiplies the viewport resolution (up to 8x) to capture high-definition, anti-aliased screenshots. Press F12 to capture.");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SliderInt("##ScreenshotMult", &gloParent->mOptions->screenshotMultiplier, 1, 8, "%dx");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Multiplies the viewport resolution (up to 8x) to capture high-definition, anti-aliased screenshots. Press F12 to capture.");
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Show FPS");
                ImGui::TableNextColumn();
                ImGui::Checkbox("##ShowFPS", &gloParent->mOptions->showFPS);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("STL Shadows (Exp)");
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##StlShadows", &gloParent->mOptions->stlShadowsEnabled)) {
                    viewport.markSceneDirty();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("EXPERIMENTAL: Enables planar shadows for imported STL geometry.\nMay impact performance on large models.");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("VSync");
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##VSync", &gloParent->mOptions->vSync))
                    vulkanContext.setVSync(gloParent->mOptions->vSync);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Synchronizes the frame rate with your monitor's refresh rate.\nEliminates screen tearing but may add minor input lag.");
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Track Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("OptionsEditorTable", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Radius Limiter");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enforces a minimum physical radius limit along the track to ensure design manufacturability.");
                }
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##RadiusLimiter", &gloParent->mOptions->enforceMinRadius)) {
                    for (auto track : trackList)
                        if (track->trackData)
                            track->trackData->requestUpdateTrack(0, 0);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enforces a minimum physical radius limit along the track to ensure design manufacturability.");
                }
                if (gloParent->mOptions->enforceMinRadius) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputFloat("##minRad", &gloParent->mOptions->minRadius, 0.0f, 0.0f, "%.1f m")) {
                        gloParent->mOptions->minRadius = std::max(0.1f, gloParent->mOptions->minRadius);
                        for (auto track : trackList)
                            if (track->trackData)
                                track->trackData->requestUpdateTrack(0, 0);
                    }
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Stall Speed");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Sets the threshold speed below which the train simulator will consider the train stalled.");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputFloat("##StallSpeed", &gloParent->mOptions->stallSpeed, 0.01f, 0.1f, "%.2f m/s")) {
                    gloParent->mOptions->stallSpeed = std::max(0.01f, std::min(gloParent->mOptions->stallSpeed, 10.0f));
                    for (auto track : trackList)
                        if (track->trackData)
                            track->trackData->requestUpdateTrack(0, 0);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Graph Spacing");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Sets the minimum spatial distance (meters) between plotted points on the Resulting Graphs. Prevents rendering lag in long tracks.");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##GraphSpacingLimit", &gloParent->mOptions->graphSpacingLimit, 0.001f, 1.0f, "%.3f m")) {
                    gloParent->mOptions->graphSpacingLimit = std::max(0.001f, std::min(gloParent->mOptions->graphSpacingLimit, 1.0f));
                    for (auto track : trackList) {
                        if (track->trackData) {
                            track->trackData->graphChanged = true;
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Sets the threshold speed below which the train simulator will consider the train stalled.");
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("OptionsControlsTable", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Mouse Sens.");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##MouseSens", &gloParent->mOptions->mouseSensitivity, 0.1f, 5.0f, "%.2f")) {
                    gloParent->mOptions->mouseSensitivity = std::clamp(gloParent->mOptions->mouseSensitivity, 0.1f, 5.0f);
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Sprint Mult.");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Camera sprint speed multiplier when holding down the Shift key.");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##SprintMult", &gloParent->mOptions->sprintMultiplier, 1.0f, 10.0f, "%.1fx")) {
                    gloParent->mOptions->sprintMultiplier = std::clamp(gloParent->mOptions->sprintMultiplier, 1.0f, 10.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Camera sprint speed multiplier when holding down the Shift key.");
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Incr. Scroll");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Sets the absolute value increment when using Scroll Wheel over input fields.");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputFloat("##ScrollInc", &gloParent->mOptions->scrollIncrement, 0.01f, 1.0f, "%.3f")) {
                    gloParent->mOptions->scrollIncrement = std::max(0.0001f, gloParent->mOptions->scrollIncrement);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Incr. Ctrl");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Sets the absolute value increment when using Ctrl + Scroll Wheel over input fields.");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputFloat("##CtrlScrollInc", &gloParent->mOptions->scrollCtrlIncrement, 0.001f, 0.1f, "%.3f")) {
                    gloParent->mOptions->scrollCtrlIncrement = std::max(0.0001f, gloParent->mOptions->scrollCtrlIncrement);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Incr. Shift");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Sets the absolute value increment when using Shift + Scroll Wheel over input fields.");
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputFloat("##ShiftScrollInc", &gloParent->mOptions->scrollShiftIncrement, 0.1f, 10.0f, "%.3f")) {
                    gloParent->mOptions->scrollShiftIncrement = std::max(0.0001f, gloParent->mOptions->scrollShiftIncrement);
                }

                auto KeyBindButton = [](const char* label, int& key) {
                    ImGui::PushID(label);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s", label);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const char* keyName = ImGui::GetKeyName((ImGuiKey)key);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%s##btn", keyName ? keyName : "Unknown");
                    static int* awaitingKey = nullptr;
                    if (awaitingKey == &key) {
                        ImGui::Button("Press any key...##btn", ImVec2(-FLT_MIN, 0));
                        for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i++) {
                            if (ImGui::IsKeyPressed((ImGuiKey)i)) {
                                if (i != ImGuiKey_Escape)
                                    key = i;
                                awaitingKey = nullptr;
                                break;
                            }
                        }
                        if (ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())
                            awaitingKey = nullptr;
                    } else if (ImGui::Button(buf, ImVec2(-FLT_MIN, 0)))
                        awaitingKey = &key;
                    ImGui::PopID();
                };
                KeyBindButton("Move Forward", gloParent->mOptions->keyForward);
                KeyBindButton("Move Backward", gloParent->mOptions->keyBackward);
                KeyBindButton("Move Left", gloParent->mOptions->keyLeft);
                KeyBindButton("Move Right", gloParent->mOptions->keyRight);
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    if (showExportPopup) {
        ImGui::OpenPopup("Export Track Settings");
        showExportPopup = false;
    }
    if (ImGui::BeginPopupModal("Export Track Settings", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        trackHandler* activeTrack = trackList[activeTrackIdx];
        track* curTrack = activeTrack->trackData;
        int numSections = curTrack->lSections.size();
        if (exportToSection == -1 || exportToSection >= numSections)
            exportToSection = numSections - 1;
        if (exportFromSection < 0)
            exportFromSection = 0;
        if (exportFromSection > exportToSection)
            exportFromSection = exportToSection;

        ImGui::Combo("Format", &exportFormat, "NoLimits 2 Element (*.nl2elem)\0NoLimits 2 CSV (*.csv)\0NoLimits 1 Element (Bezier) (*.nlelem)\0NoLimits 1 Element (Hermite) (*.nlelem)\0");
        ImGui::InputFloat("Dist. per Node (m)", &exportDistPerNode, 0.1f, 1.0f, "%.2f");
        if (exportDistPerNode < 0.1f)
            exportDistPerNode = 0.1f;
        ImGui::Checkbox("No Heartline", &exportNoHeartline);
        if (exportFormat == 2 || exportFormat == 3)
            ImGui::InputFloat("Roll Threshold (deg)", &exportRollThresh, 1.0f, 5.0f, "%.1f");
        ImGui::SliderInt("From Section", &exportFromSection, 0, numSections - 1);
        ImGui::SliderInt("To Section", &exportToSection, exportFromSection, numSections - 1);
        ImGui::Separator();
        if (ImGui::Button("Export", ImVec2(120, 0))) {
            std::string filter, ext;
            if (exportFormat == 0) {
                filter = "NL2 Element (*.nl2elem)";
                ext = ".nl2elem";
            } else if (exportFormat == 1) {
                filter = "NL2 CSV (*.csv)";
                ext = ".csv";
            } else {
                filter = "NL Element (*.nlelem)";
                ext = ".nlelem";
            }
            auto f = pfd::save_file("Export Track", ".", {filter, "*" + ext, "All Files", "*"}).result();
            if (!f.empty()) {
                if (f.find(ext) == std::string::npos)
                    f += ext;
                PerformExport(f);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (showExitPopup)
        ImGui::OpenPopup("Exit Confirmation");
    if (ImGui::BeginPopupModal("Exit Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Do you want to save your changes before exiting?");
        ImGui::Separator();
        if (ImGui::Button("Exit", ImVec2(120, 0))) {
            glfwSetWindowShouldClose(window, true);
            showExitPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Exit and Save", ImVec2(120, 0))) {
            if (currentFilePath.empty()) {
                auto f = pfd::save_file("Save project", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                if (!f.empty()) {
                    if (f.find(".fvd") == std::string::npos)
                        f += ".fvd";
                    currentFilePath = f;
                }
            }
            if (!currentFilePath.empty()) {
                saver saveObj(currentFilePath, trackList);
                saveObj.doSave();
                LOG_INFO("Saved project: %s", currentFilePath.c_str());
                pfd::notify("Project Saved", "Successfully saved project to:\n" + currentFilePath, pfd::icon::info);
                glfwSetWindowShouldClose(window, true);
                showExitPopup = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            showExitPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    {
        ImGuiWindowFlags commonFlags = 0;
        ImGui::Begin("Project Panel", nullptr, commonFlags);
        leftPanel.render(this);
        ImGui::End();
        ImGui::Begin("Environment", nullptr, commonFlags);
        leftPanel.renderEnvironmentTab();
        ImGui::End();
        ImGui::Begin("Transition Editor", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            auto track = trackList[activeTrackIdx];
            bool valid = false;
            if (gloParent->selectedFunc) {
                for (section* sec : track->trackData->lSections) {
                    if (sec->rollFunc)
                        for (subfunc* sf : sec->rollFunc->funcList)
                            if (sf == gloParent->selectedFunc) {
                                valid = true;
                                break;
                            }
                    if (valid)
                        break;
                    if (sec->normForce)
                        for (subfunc* sf : sec->normForce->funcList)
                            if (sf == gloParent->selectedFunc) {
                                valid = true;
                                break;
                            }
                    if (valid)
                        break;
                    if (sec->latForce)
                        for (subfunc* sf : sec->latForce->funcList)
                            if (sf == gloParent->selectedFunc) {
                                valid = true;
                                break;
                            }
                    if (valid)
                        break;
                }
                if (!valid)
                    gloParent->selectedFunc = nullptr;
            }
            transitionView.render(track, gloParent->selectedFunc, this);
        } else
            ImGui::Text("No active track.");
        ImGui::End();

        ImGui::Begin("Viewport", nullptr, commonFlags);
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
            static int lastW = 0, lastH = 0;
            if ((int)viewportPanelSize.x != lastW || (int)viewportPanelSize.y != lastH) {
                viewport.resize((int)viewportPanelSize.x, (int)viewportPanelSize.y);
                lastW = (int)viewportPanelSize.x;
                lastH = (int)viewportPanelSize.y;
            }
            if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
                viewport.setActiveTrack(trackList[activeTrackIdx]);
                gloParent->currentTrack = trackList[activeTrackIdx]->trackData;
            } else {
                viewport.setActiveTrack(nullptr);
                gloParent->currentTrack = nullptr;
            }

            bool toggleRequest = ImGui::IsMouseClicked(ImGuiMouseButton_Right) && (ImGui::IsWindowHovered() || viewportActive);
            bool escapePressed = viewportActive && ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (ImGui::IsWindowHovered() && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || ImGui::IsMouseClicked(2)) && !viewportActive) {
                ImGui::SetWindowFocus();
                ImGui::ClearActiveID();
            }
            if (toggleRequest || escapePressed) {
                viewportActive = !viewportActive;
                if (escapePressed)
                    viewportActive = false;
                viewport.setCaptured(viewportActive);
                viewport.markSceneDirty();
                if (viewportActive) {
                    ImGui::ClearActiveID();
                    ImGui::SetWindowFocus();
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                } else {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }

            if (viewportActive) {
                if (ImGui::IsKeyPressed(ImGuiKey_Space) && !viewport.getOrthoMode())
                    viewport.setPOVMode(!viewport.getPOVMode());

                if (viewport.getOrthoMode()) {
                    viewport.panCamera((float)mouseDeltaX, (float)mouseDeltaY);
                    if (ImGui::GetIO().MouseWheel != 0.0f)
                        viewport.zoomCamera(ImGui::GetIO().MouseWheel * 2.0f);
                } else
                    viewport.rotateCamera((float)-mouseDeltaX * 0.1f * gloParent->mOptions->mouseSensitivity, (float)-mouseDeltaY * 0.1f * gloParent->mOptions->mouseSensitivity);

                glm::vec3 moveDelta(0.0f);

                if (!viewport.getOrthoMode() && !viewport.getPOVMode()) {
                    float moveSpeed = 30.0f * deltaTime;
                    if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
                        moveSpeed *= gloParent->mOptions->sprintMultiplier;
                    if (ImGui::IsKeyDown((ImGuiKey)gloParent->mOptions->keyForward))
                        moveDelta.z += moveSpeed;
                    if (ImGui::IsKeyDown((ImGuiKey)gloParent->mOptions->keyBackward))
                        moveDelta.z -= moveSpeed;
                    if (ImGui::IsKeyDown((ImGuiKey)gloParent->mOptions->keyLeft))
                        moveDelta.x -= moveSpeed;
                    if (ImGui::IsKeyDown((ImGuiKey)gloParent->mOptions->keyRight))
                        moveDelta.x += moveSpeed;
                    if (ImGui::GetIO().MouseWheel != 0.0f) {
                        float boost = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? gloParent->mOptions->sprintMultiplier : 1.0f;
                        viewport.moveCamera(glm::vec3(0.0f, ImGui::GetIO().MouseWheel * 2.0f * boost, 0.0f));
                    }
                }
                if (viewport.getPOVMode()) {
                    float boost = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? gloParent->mOptions->sprintMultiplier : 1.0f;
                    float zMovement = 0.0f;
                    if (ImGui::IsKeyDown((ImGuiKey)gloParent->mOptions->keyForward))
                        zMovement += 1.0f;
                    if (ImGui::IsKeyDown((ImGuiKey)gloParent->mOptions->keyBackward))
                        zMovement -= 1.0f;
                    viewport.movePOVCamera(zMovement * boost, deltaTime);
                    float heightSpeed = 5.0f * deltaTime * boost;
                    if (ImGui::IsKeyDown(ImGuiKey_PageUp))
                        viewport.adjustPOVHeight(heightSpeed);
                    if (ImGui::IsKeyDown(ImGuiKey_PageDown))
                        viewport.adjustPOVHeight(-heightSpeed);
                    if (ImGui::GetIO().MouseWheel != 0.0f)
                        viewport.adjustPOVHeight(ImGui::GetIO().MouseWheel * 0.5f * boost);
                } else
                    viewport.moveCamera(moveDelta);
            } else if (ImGui::IsWindowHovered())
                viewport.zoomCamera(ImGui::GetIO().MouseWheel * 1.0f);

            viewport.setShowPOVMarker3D(graphView.getShowPOVMarker());
            viewport.render(trackList);
            ImGui::Image((ImTextureID)viewport.getOutputTexture(), viewportPanelSize);
        }
        ImGui::End();

        ImGui::Begin("Graph List", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size())
            graphView.renderList(trackList[activeTrackIdx]);
        else
            ImGui::Text("No active track.");
        ImGui::End();

        bool focusResulting = graphView.getAndClearSwitchToResultingTab();

        ImGui::Begin("Graphs", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size() && trackList[activeTrackIdx]->trackData->activeSection != nullptr)
            graphView.renderPlot(trackList[activeTrackIdx]);
        else
            ImGui::Text("No data to plot.");
        ImGui::End();

        if (focusResulting)
            ImGui::SetNextWindowFocus();
        ImGui::Begin("Resulting Graphs", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size() && trackList[activeTrackIdx]->trackData->activeSection != nullptr)
            graphView.renderResultingPlot(trackList[activeTrackIdx]);
        else
            ImGui::Text("No data to plot.");
        ImGui::End();

        ImGui::Begin("Measurement Graphs", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size() && trackList[activeTrackIdx]->trackData->activeSection != nullptr) {
            if (graphView.hasMeasurementGraphsVisible(trackList[activeTrackIdx]))
                graphView.renderMeasurementPlot(trackList[activeTrackIdx]);
            else
                ImGui::TextDisabled("Add Measurement Points in the Graph List panel to view them here.");
        } else
            ImGui::Text("No data to plot.");
        ImGui::End();

        ImGui::Begin("Metrics", nullptr, commonFlags | ImGuiWindowFlags_NoTitleBar);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            trackHandler* hTrack = trackList[activeTrackIdx];
            mnode* lastNode = nullptr;
            if (graphView.getShowPOVMarker() && viewport.getPOVNode())
                lastNode = viewport.getPOVNode();
            else if (!hTrack->trackData->lSections.empty() && hTrack->trackData->activeSection) {
                if (!hTrack->trackData->activeSection->lNodes.empty())
                    lastNode = &hTrack->trackData->activeSection->lNodes.back();
            } else
                lastNode = hTrack->trackData->anchorNode;

            if (lastNode) {
                glm::mat4 anchorBase = glm::translate(glm::mat4(1.0f), (glm::vec3)hTrack->trackData->startPos) *
                                       glm::rotate(glm::mat4(1.0f), glm::radians((float)hTrack->trackData->startYaw - 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 worldPos = glm::vec3(anchorBase * glm::vec4(lastNode->vPosHeart(hTrack->trackData->fHeart), 1.0f));

                float lenFact = gloParent->mOptions->getLengthFactor();
                std::string lenStr = gloParent->mOptions->getLengthString();
                float spdFact = gloParent->mOptions->getSpeedFactor();
                std::string spdStr = gloParent->mOptions->getSpeedString();

                std::vector<std::string> groups;
                char buf[256];
                snprintf(buf, sizeof(buf), "X: %+.3f %s    Y: %+.3f %s    Z: %+.3f %s", worldPos.x * lenFact, lenStr.c_str(), worldPos.y * lenFact, lenStr.c_str(), worldPos.z * lenFact, lenStr.c_str());
                groups.push_back(buf);
                snprintf(buf, sizeof(buf), "Roll: %+.3f deg (%+.3f deg/s)    Pitch: %+.3f deg (%+.3f deg/s)    Yaw: %+.3f deg (%+.3f deg/s)", lastNode->fRoll, lastNode->fRollSpeed, lastNode->getPitch(), lastNode->getPitchChange(), lastNode->getDirection(), lastNode->getYawChange());
                groups.push_back(buf);
                snprintf(buf, sizeof(buf), "Y-Accel: %+.3f g    X-Accel: %+.3f g", lastNode->forceNormal, lastNode->forceLateral);
                groups.push_back(buf);

                int lastNodeIndex = 0;
                if (graphView.getShowPOVMarker() && viewport.getPOVNode()) {
                    lastNodeIndex = viewport.getPOVPos();
                } else if (!hTrack->trackData->lSections.empty()) {
                    section* activeSec = hTrack->trackData->activeSection;
                    if (!activeSec) {
                        activeSec = hTrack->trackData->lSections.back();
                    }
                    lastNodeIndex = hTrack->trackData->getNumPoints(activeSec);
                    if (!activeSec->lNodes.empty()) {
                        lastNodeIndex += activeSec->lNodes.size() - 1;
                    }
                }
                double timeVal = (double)lastNodeIndex / 1000.0;
                double distVal = lastNode->fTotalLength * lenFact;

                snprintf(buf, sizeof(buf), "Distance: %+.3f %s    Time: %+.3f s", distVal, lenStr.c_str(), timeVal);
                groups.push_back(buf);

                char speedBuf[128];
                snprintf(speedBuf, sizeof(speedBuf), "Speed: %+.3f %s", lastNode->fVel * spdFact, spdStr.c_str());
                std::string speedStr(speedBuf);

                float totalTextWidth = 0.0f;
                std::vector<float> textWidths;
                for (size_t i = 0; i < groups.size(); ++i) {
                    float w = ImGui::CalcTextSize(groups[i].c_str()).x;
                    if (i == groups.size() - 1) {
                        w += ImGui::CalcTextSize("    ").x + ImGui::CalcTextSize(speedStr.c_str()).x;
                    }
                    textWidths.push_back(w);
                    totalTextWidth += w;
                }

                float availWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
                float startX = ImGui::GetWindowContentRegionMin().x;
                float padding = 15.0f;
                if (groups.size() > 1 && availWidth > totalTextWidth)
                    padding = (availWidth - totalTextWidth - 1.0f) / (groups.size() - 1);

                float currentX = startX;
                for (size_t i = 0; i < groups.size(); ++i) {
                    if (i > 0)
                        ImGui::SameLine();
                    if (i > 0 && currentX + textWidths[i] > startX + availWidth + 1.0f) {
                        ImGui::NewLine();
                        currentX = startX;
                    }
                    ImGui::SetCursorPosX(currentX);
                    if (i == groups.size() - 1) {
                        ImGui::TextUnformatted(groups[i].c_str());
                        ImGui::SameLine(0, 0);
                        ImGui::TextUnformatted("    ");
                        ImGui::SameLine(0, 0);
                        ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", speedStr.c_str());
                    } else {
                        ImGui::TextUnformatted(groups[i].c_str());
                    }
                    currentX += textWidths[i] + padding;
                }
            }
        } else
            ImGui::Text("No active track.");
        ImGui::End();
    }

    if (gloParent->mOptions->showFPS) {
        frameCount++;
        double now = glfwGetTime();
        if (now - lastFPSUpdate >= 1.0) {
            currentFPS = (float)frameCount / (float)(now - lastFPSUpdate);
            frameCount = 0;
            lastFPSUpdate = now;
        }
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("FPS Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("%.1f FPS", currentFPS);
            ImGui::End();
        }
    }

    // Render Status Bar at the bottom of the window
    {
        ImGuiViewport* main_vp = ImGui::GetMainViewport();
        float sbHeight = ImGui::GetFrameHeight();

        ImGui::SetNextWindowPos(ImVec2(main_vp->WorkPos.x, main_vp->WorkPos.y + main_vp->WorkSize.y - sbHeight));
        ImGui::SetNextWindowSize(ImVec2(main_vp->WorkSize.x, sbHeight));
        ImGui::SetNextWindowViewport(main_vp->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 2.0f));

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

        ImGuiWindowFlags sbFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##MainStatusBar", NULL, sbFlags)) {
            if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
                track* curTrack = trackList[activeTrackIdx]->trackData;

                // 1. Check if any section is stalled
                bool anySectionStalled = false;
                for (section* sec : curTrack->lSections) {
                    if (sec->isStalled) {
                        anySectionStalled = true;
                        break;
                    }
                }

                if (anySectionStalled) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: Train stalled! Minimum speed clamped.");
                } else if (curTrack->isAnyNodeNearGimbalLock) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "Warning: Pitch is near 90 degrees; gimbal lock may cause instability.");
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "System Status: Ready");
                }

                // Align other elements to the right side of the status bar
                ImGui::SameLine();

                int totalPoints = curTrack->getNumPoints();
                int currentSectionNodes = curTrack->activeSection ? (int)curTrack->activeSection->lNodes.size() : 0;
                double updateTime = curTrack->lastUpdateTimeMs;
                int totalPlotted = graphView.getTotalPlottedPoints();
                double spacingLimit = (double)gloParent->mOptions->graphSpacingLimit;

                char statBuf[256];
                snprintf(statBuf, sizeof(statBuf), "Total Nodes: %d   |   Section Nodes: %d   |   Down-sampling Spacing: %.3f m (Plotted: %d)   |   Layout Update: %.2f ms",
                         totalPoints, currentSectionNodes, spacingLimit, totalPlotted, updateTime);

                float textWidth = ImGui::CalcTextSize(statBuf).x;
                float availWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;

                ImGui::SetCursorPosX(availWidth - textWidth);
                ImGui::TextUnformatted(statBuf);
            } else {
                ImGui::Text("No active track.");
            }
            ImGui::End();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
}

void Application::PerformExport(const std::string& path) {
    LOG_INFO("Exporting track to: %s (Format: %d)", path.c_str(), exportFormat);
    if (activeTrackIdx < 0 || activeTrackIdx >= (int)trackList.size())
        return;
    track* curTrack = trackList[activeTrackIdx]->trackData;
    int numSections = curTrack->lSections.size();

    int toSec = (exportToSection == -1 || exportToSection >= numSections) ? numSections - 1 : exportToSection;
    int fromSec = std::clamp(exportFromSection, 0, toSec);

    float oldHeartLine = curTrack->fHeart;
    if (exportNoHeartline)
        curTrack->fHeart = 0.0f;

    if (exportFormat == 0) {
        FILE* fout = fopen(path.c_str(), "w");
        if (fout) {
            fprintf(fout, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n\t<element>\n\t\t<description>FVD++ Export Data</description>\n");
            curTrack->exportNL2Track(fout, exportDistPerNode, fromSec, toSec);
            fprintf(fout, "\t</element>\n</root>\n");
            fclose(fout);
        }
    } else if (exportFormat == 1) {
        FILE* fout = fopen(path.c_str(), "w");
        if (fout) {
            fprintf(fout, "\"No.\"\t\"PosX\"\t\"PosY\"\t\"PosZ\"\t\"FrontX\"\t\"FrontY\"\t\"FrontZ\"\t\"LeftX\"\t\"LeftY\"\t\"LeftZ\"\t\"UpX\"\t\"UpY\"\t\"UpZ\"\n");
            curTrack->exportNL2TrackCSV(fout, exportDistPerNode, fromSec, toSec);
            fclose(fout);
        }
    } else if (exportFormat == 2 || exportFormat == 3) {
        std::fstream fout(path.c_str(), std::ios::out | std::ios::binary);
        if (fout.is_open()) {
            writeBytes(&fout, (const char*)"MELE", 4);
            writeNulls(&fout, 4);
            writeNulls(&fout, 64);
            writeNulls(&fout, 4);

            float rollThreshRad = sin(exportRollThresh * F_PI / 180.f);
            int iNodes = (exportFormat == 2) ? curTrack->exportTrack4(&fout, exportDistPerNode, fromSec, toSec, rollThreshRad) : curTrack->exportTrack3(&fout, exportDistPerNode, fromSec, toSec, rollThreshRad);

            int iDataLength = iNodes * 50 + 132;
            writeNulls(&fout, 69);
            fout.seekp(4);
            writeBytes(&fout, (const char*)&iDataLength, 4);
            fout.seekp(72);
            writeBytes(&fout, (const char*)&iNodes, 4);
            fout.close();
        }
    }

    curTrack->fHeart = oldHeartLine;
    lastExportPath = path;
}

void Application::Shutdown() {
    gloParent->mOptions->save("options.cfg");
    LOG_INFO("FVD++ shutting down...");

    vulkanContext.waitIdle();
    for (auto track : trackList)
        delete track;
    trackList.clear();

    viewport.shutdown();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    vulkanContext.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

#ifdef _WIN32
    timeEndPeriod(1);
#endif
}

static std::string incrementFilename(const std::string& path) {
    std::filesystem::path p(path);
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();
    std::filesystem::path parent = p.parent_path();

    // Find the trailing digits in stem
    int idx = (int)stem.size() - 1;
    while (idx >= 0 && std::isdigit((unsigned char)stem[idx])) {
        idx--;
    }

    std::string base = stem.substr(0, idx + 1);
    std::string digits = stem.substr(idx + 1);

    if (digits.empty()) {
        // If there are no trailing digits, check if it ends with '_'. If not, append '_'
        if (base.empty() || base.back() != '_') {
            base += "_";
        }
        stem = base + "001";
    } else {
        // Parse the digits, increment them, and format with the same width
        long long num = std::stoll(digits);
        num++;
        std::string newDigits = std::to_string(num);
        if (newDigits.size() < digits.size()) {
            newDigits = std::string(digits.size() - newDigits.size(), '0') + newDigits;
        }
        stem = base + newDigits;
    }

    return (parent / (stem + ext)).string();
}

void Application::PerformIncrementalSave() {
    auto exitViewport = [&]() {
        if (viewportActive) {
            viewportActive = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
    };

    if (currentFilePath.empty()) {
        exitViewport();
        auto f = pfd::save_file("Save project", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
        if (!f.empty()) {
            if (f.find(".fvd") == std::string::npos)
                f += ".fvd";
            currentFilePath = f;
            saver saveObj(currentFilePath, trackList);
            saveObj.doSave();
            LOG_INFO("Saved project: %s", currentFilePath.c_str());
            pfd::notify("Project Saved", "Successfully saved project to:\n" + currentFilePath, pfd::icon::info);
        }
    } else {
        std::string newPath = incrementFilename(currentFilePath);
        currentFilePath = newPath;
        saver saveObj(currentFilePath, trackList);
        saveObj.doSave();
        LOG_INFO("Incrementally saved project: %s", currentFilePath.c_str());
        pfd::notify("Project Saved (Incremental)", "Successfully saved project incrementally to:\n" + currentFilePath, pfd::icon::info);
    }
}

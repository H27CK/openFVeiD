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
#include "core/keybinds.h"
#include "imgui_internal.h"
#include "core/workingdirectory.h"
#include "core/dockstyle.h"
#include "smoothhandler.h"
#include "vfs.h"
#include "core/secnlcsv.h"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <shellapi.h>
#include <GLFW/glfw3native.h>
#include <timeapi.h>
#endif

#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "ext/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "ext/nanosvgrast.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cctype>

extern DummyGlobal* gloParent;
extern DummyGLView* glView;
extern Viewport* gViewport;

static std::string formatWithCommas(size_t val) {
    std::string s = std::to_string(val);
    int n = s.length() - 3;
    while (n > 0) {
        s.insert(n, ",");
        n -= 3;
    }
    return s;
}

static bool hasCaseInsensitiveExtension(const std::string& path, const std::string& expected) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == expected;
}

static int pendingDeleteSceneryIdx = -1;

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

std::string Application::loadSkippedVersion() {
    std::ifstream in("skip_version.txt");
    if (!in)
        return "";
    std::string ver;
    in >> ver;
    return ver;
}

void Application::saveSkippedVersion(const std::string& ver) {
    std::ofstream out("skip_version.txt");
    if (out) {
        out << ver;
    }
}

std::vector<int> Application::parseVersion(std::string versionStr) {
    if (!versionStr.empty() && (versionStr[0] == 'v' || versionStr[0] == 'V')) {
        versionStr = versionStr.substr(1);
    }
    size_t dashPos = versionStr.find('-');
    if (dashPos != std::string::npos) {
        versionStr = versionStr.substr(0, dashPos);
    }
    std::vector<int> parts;
    std::stringstream ss(versionStr);
    std::string part;
    while (std::getline(ss, part, '.')) {
        try {
            parts.push_back(std::stoi(part));
        } catch (...) {
            parts.push_back(0);
        }
    }
    while (parts.size() < 3) {
        parts.push_back(0);
    }
    return parts;
}

bool Application::isNewerVersion(const std::string& remote, const std::string& local) {
    std::vector<int> rParts = parseVersion(remote);
    std::vector<int> lParts = parseVersion(local);
    for (size_t i = 0; i < 3; ++i) {
        if (rParts[i] > lParts[i])
            return true;
        if (rParts[i] < lParts[i])
            return false;
    }
    return false;
}

void Application::triggerUpdateCheck() {
    std::thread([this]() {
        // Check if our local version is an unreleased development or Git hash build.
        // Standard releases always have at least 2 dot separators (e.g. vX.Y.Z) and no dev/dirty suffixes.
        std::string localVer = FVD_VERSION;
        int dotCount = 0;
        for (char c : localVer) {
            if (c == '.')
                dotCount++;
        }
        bool isDevBuild = (dotCount < 2) || (localVer.find("-dev") != std::string::npos) || (localVer.find("-dirty") != std::string::npos);

        if (isDevBuild) {
            updateCheckFinished = true;
            return;
        }

        // 1. Check if curl is available
        std::string checkCmd = "curl --version 2>&1";
        FILE* pipe = popen(checkCmd.c_str(), "r");
        if (!pipe) {
            curlAvailable = false;
            updateCheckFinished = true;
            return;
        }
        char buffer[128];
        std::string checkResult = "";
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            checkResult = buffer;
        }
        pclose(pipe);

        if (checkResult.rfind("curl", 0) != 0) {
            curlAvailable = false;
            updateCheckFinished = true;
            return;
        }

        // 2. Fetch the latest release JSON from the GitHub API
        std::string fetchCmd = "curl -H \"User-Agent: FVDPlusPlus\" -s -m 5 https://api.github.com/repos/H27CK/openFVeiD/releases/latest 2>&1";
        pipe = popen(fetchCmd.c_str(), "r");
        if (!pipe) {
            updateCheckFinished = true;
            return;
        }
        std::string response = "";
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            response += buffer;
        }
        pclose(pipe);

        // Find and parse "tag_name" key from the JSON response
        size_t pos = response.find("\"tag_name\"");
        if (pos != std::string::npos) {
            size_t colon = response.find(':', pos);
            if (colon != std::string::npos) {
                size_t firstQuote = response.find('\"', colon);
                if (firstQuote != std::string::npos) {
                    size_t secondQuote = response.find('\"', firstQuote + 1);
                    if (secondQuote != std::string::npos) {
                        std::string remoteVer = response.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                        remoteVer.erase(remoteVer.find_last_not_of(" \n\r\t") + 1);

                        if (!remoteVer.empty()) {
                            remoteVersion = remoteVer;
                            std::string skipped = loadSkippedVersion();
                            if (remoteVer != skipped && isNewerVersion(remoteVer, FVD_VERSION)) {
                                updateAvailable = true;
                            }
                        }
                    }
                }
            }
        }
        updateCheckFinished = true;
    }).detach();
}

Application::Application() {
    mUndoHandler = new GlobalUndoHandler(this, gloParent->mOptions->maxUndoChanges);
}

Application::~Application() {
    delete mUndoHandler;
}

static void ApplyThemeSoftDark() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImGui::StyleColorsDark();

    // Base background colors (soft slate charcoal, low fatigue)
    colors[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.88f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.54f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.14f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame inputs (sliders, checkboxes, textfields)
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.26f, 0.28f, 1.00f);

    // Titlebar
    colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.15f, 0.16f, 0.75f);

    // Menu and Menubar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.29f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.36f, 0.38f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.43f, 0.45f, 1.00f);

    // Checkmark & Sliders
    colors[ImGuiCol_CheckMark] = ImVec4(0.46f, 0.65f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.38f, 0.50f, 0.65f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.46f, 0.65f, 0.85f, 1.00f);

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.24f, 0.25f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.32f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.40f, 0.45f, 1.00f);

    // Headers (CollapsingHeader, Selectable, TreeNode)
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.36f, 0.40f, 1.00f);

    // Separators
    colors[ImGuiCol_Separator] = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.42f, 0.50f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.45f, 0.55f, 0.68f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.27f, 0.30f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.18f, 0.19f, 0.20f, 1.00f);

    // Tables
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.26f, 0.27f, 0.29f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.22f, 0.23f, 0.24f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);

    ImPlot::StyleColorsDark();
}

static void ApplyThemeSoftLight() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImGui::StyleColorsLight();

    // Base background colors (soft muted off-white/silver, low glare)
    colors[ImGuiCol_Text] = ImVec4(0.18f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.54f, 0.56f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.91f, 0.92f, 0.93f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.94f, 0.95f, 0.96f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.15f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame inputs (sliders, checkboxes, textfields)
    colors[ImGuiCol_FrameBg] = ImVec4(0.98f, 0.98f, 0.99f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.88f, 0.90f, 0.92f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.82f, 0.85f, 0.88f, 1.00f);

    // Titlebar
    colors[ImGuiCol_TitleBg] = ImVec4(0.85f, 0.86f, 0.88f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.80f, 0.82f, 0.84f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.85f, 0.86f, 0.88f, 0.75f);

    // Menu and Menubar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.86f, 0.87f, 0.89f, 1.00f);

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.72f, 0.74f, 0.76f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.62f, 0.65f, 0.68f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.53f, 0.56f, 1.00f);

    // Checkmark & Sliders
    colors[ImGuiCol_CheckMark] = ImVec4(0.24f, 0.44f, 0.68f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.65f, 0.78f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.28f, 0.48f, 0.72f, 1.00f);

    // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.82f, 0.84f, 0.86f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.78f, 0.81f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.70f, 0.76f, 1.00f);

    // Headers (CollapsingHeader, Selectable, TreeNode)
    colors[ImGuiCol_Header] = ImVec4(0.82f, 0.84f, 0.86f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.76f, 0.80f, 0.84f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.70f, 0.74f, 0.80f, 1.00f);

    // Separators
    colors[ImGuiCol_Separator] = ImVec4(0.74f, 0.76f, 0.78f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.52f, 0.62f, 0.74f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.35f, 0.48f, 0.64f, 1.00f);

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.84f, 0.85f, 0.87f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.76f, 0.79f, 0.83f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.91f, 0.92f, 0.93f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.86f, 0.87f, 0.89f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.89f, 0.90f, 0.91f, 1.00f);

    // Tables
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.84f, 0.85f, 0.87f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.72f, 0.74f, 0.76f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.80f, 0.82f, 0.84f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);

    ImPlot::StyleColorsLight();
}

static glm::vec3 GetDefaultTabHighlightColor(int theme) {
    switch (theme) {
    case 0:                                    // Dark
        return glm::vec3(0.26f, 0.59f, 0.98f); // ImGui classic electric blue
    case 1:                                    // Light
        return glm::vec3(0.60f, 0.78f, 0.96f); // Bright sky blue (high contrast for dark/black text)
    case 2:                                    // Classic
        return glm::vec3(0.50f, 0.32f, 0.75f); // Classic purple / amethyst
    case 3:                                    // Soft Dark
        return glm::vec3(0.28f, 0.52f, 0.76f); // Soft slate cyan / blue
    case 4:                                    // Soft Light
        return glm::vec3(0.65f, 0.78f, 0.92f); // Pastel steel/cerulean (high contrast for dark/black text)
    default:
        return glm::vec3(0.26f, 0.59f, 0.98f);
    }
}

static void ApplyTheme(int theme) {
    if (theme == 0) {
        ImGui::StyleColorsDark();
        ImPlot::StyleColorsDark();
    } else if (theme == 1) {
        ImGui::StyleColorsLight();
        ImPlot::StyleColorsLight();
    } else if (theme == 2) {
        ImGui::StyleColorsClassic();
        ImPlot::StyleColorsClassic();
    } else if (theme == 3) {
        ApplyThemeSoftDark();
    } else if (theme == 4) {
        ApplyThemeSoftLight();
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    if (theme < 3) {
        style.Colors[ImGuiCol_Border].w = 0.35f;
    }

    glm::vec3 thCol = (gloParent && gloParent->mOptions) ? gloParent->mOptions->tabHighlightColor : GetDefaultTabHighlightColor(theme);
    ImVec4 tabColor(thCol.x, thCol.y, thCol.z, 1.0f);
    ImVec4 tabDimmedColor(thCol.x * 0.85f, thCol.y * 0.85f, thCol.z * 0.85f, 0.90f);
    style.Colors[ImGuiCol_TabSelected] = tabColor;
    style.Colors[ImGuiCol_TabDimmedSelected] = tabDimmedColor;
    style.Colors[ImGuiCol_TabSelectedOverline] = tabColor;
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = tabDimmedColor;
    style.TabBarOverlineSize = 2.0f;

    style.FrameBorderSize = 1.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarSize = 9.0f;
    style.ScrollbarRounding = 4.5f;
    style.TabRounding = 5.0f;
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
}

bool Application::Initialize(int argc, char** argv) {
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
    gloParent->mOptions->keyOverlayWarnings = ImGuiKey_X;
    gloParent->mOptions->keyOverlayScenery = ImGuiKey_Z;

    if (!std::filesystem::exists("tracks")) {
        std::filesystem::create_directory("tracks");
    }

    if (!std::filesystem::exists("skybox")) {
        std::filesystem::create_directory("skybox");
    }

    if (!std::filesystem::exists("templates")) {
        std::filesystem::create_directory("templates");
    }

    gloParent->mOptions->load("options.cfg");
    KeybindManager::instance().load("keybinds.cfg");
    gloParent->resetEnvironment();
    loadRecentFiles();

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
    HINSTANCE hInstance = GetModuleHandle(NULL);
    HICON hIconBig = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(101), IMAGE_ICON,
                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(101), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!hIconBig) {
        hIconBig = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    }
    if (hIconBig) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    }
    if (hIconSmall) {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    } else if (hIconBig) {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconBig);
    }
#endif

    glfwMaximizeWindow(window);

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

    const AssetData* fontAsset = getEmbeddedAsset("resources/fonts/Roboto-Medium.ttf");
    if (fontAsset) {
        font_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)fontAsset->data, (int)fontAsset->size, currentFontSize, &font_cfg);
    } else {
        io.Fonts->AddFontDefault(&font_cfg);
    }

    ApplyTheme(gloParent->mOptions->theme);
    ImGuiStyle& style = ImGui::GetStyle();

    style.ScaleAllSizes(currentFontSize / 15.0f);
    style.ScrollbarSize = 9.0f * (currentFontSize / 15.0f);
    style.ScrollbarRounding = style.ScrollbarSize * 0.5f;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
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
    initWelcomeSvgTexture();

    for (const auto& glb : gloParent->projectGlbs) {
        viewport.addGlbMesh(glb.path);
        if (!viewport.glbMeshes.empty()) {
            viewport.glbMeshes.back().visible = glb.visible;
        }
    }

    if (!gloParent->projectGroundTex.empty()) {
        viewport.loadGroundTexture(gloParent->projectGroundTex);
    }
    viewport.setGroundTextureSize(gloParent->projectGrdTexSize);
    viewport.setGroundHeight(gloParent->projectGrdHeight);

    if (!gloParent->mOptions->lastEnvPreset.empty()) {
        viewport.loadEnvironmentPreset(gloParent->mOptions->lastEnvPreset);
    }

    viewport.setMistColor(gloParent->mOptions->mistColor);
    viewport.setShadowMode(gloParent->mOptions->shadowsEnabled ? 1 : 0);
    viewport.setFOV(gloParent->mOptions->fov);
    viewport.setMSAASamples(gloParent->mOptions->msaaSamples);

    mistColor = ImVec4(gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b, 1.0f);
    lastTime = glfwGetTime();
    glfwGetCursorPos(window, &last_mx, &last_my);
    lastFPSUpdate = glfwGetTime();

    triggerUpdateCheck();

    if (argc > 1 && argv && argv[1]) {
        std::string arg1 = argv[1];
        if (std::filesystem::exists(arg1) && !std::filesystem::is_directory(arg1)) {
            loadProjectFile(arg1);
            showWelcomeScreen = false;
        } else {
            showWelcomeScreen = true;
        }
    } else {
        showWelcomeScreen = true;
    }

    return true;
}

void Application::Run() {
    double targetDuration = 1.0 / (double)std::max(1, gloParent->mOptions->targetFPS);
    double nextFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(window) || showExitPopup) {
        // 1. Process OS Events
        // If we are navigating the camera, we poll as fast as possible.
        // Otherwise, we poll once and move to rendering.
        if (viewportActive) {
            glfwPollEvents();
        } else {
            glfwPollEvents();
        }

        if (glfwWindowShouldClose(window)) {
            if (showWelcomeScreen) {
                break;
            }
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
        viewport.update(deltaTime);

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
        static ImVec2 pre_drag_mouse_pos = ImVec2(0.0f, 0.0f);

        bool is_dragging_widget = ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && (GImGui->MovingWindow == nullptr);
        if (is_dragging_widget) {
            ImGuiIO& io = ImGui::GetIO();

            // Record original click position on the start of the drag
            if (!was_dragging_widget) {
                pre_drag_mouse_pos = io.MousePos;
                was_dragging_widget = true;
            }

            ImGui::SetMouseCursor(ImGuiMouseCursor_None); // Hide the cursor visually

            ImGuiViewport* vp = ImGui::GetMainViewport();

            // Define a small margin near the screen edge to trigger the warp
            float margin = 2.0f;
            bool warped = false;

            // Horizontal Warp
            if (io.MousePos.x <= vp->Pos.x + margin) {
                io.MousePos.x = vp->Pos.x + vp->Size.x - margin - 1.0f;
                warped = true;
            } else if (io.MousePos.x >= vp->Pos.x + vp->Size.x - margin) {
                io.MousePos.x = vp->Pos.x + margin + 1.0f;
                warped = true;
            }

            // Vertical Warp
            if (io.MousePos.y <= vp->Pos.y + margin) {
                io.MousePos.y = vp->Pos.y + vp->Size.y - margin - 1.0f;
                warped = true;
            } else if (io.MousePos.y >= vp->Pos.y + vp->Size.y - margin) {
                io.MousePos.y = vp->Pos.y + margin + 1.0f;
                warped = true;
            }

            // Request the backend to apply the new OS cursor position
            if (warped) {
                io.WantSetMousePos = true;
            }
        } else if (was_dragging_widget) {
            // Restore original click position when dragging ends
            ImGuiIO& io = ImGui::GetIO();
            io.MousePos = pre_drag_mouse_pos;
            io.WantSetMousePos = true;
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

        // 5. Frame Pacing
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
    }
}

void Application::HandleShortcuts() {
    if (pendingDeleteSceneryIdx != -1 && pendingDeleteSceneryIdx < (int)viewport.glbMeshes.size()) {
        std::string removedPath = viewport.glbMeshes[pendingDeleteSceneryIdx].path;
        for (auto it = gloParent->projectGlbs.begin(); it != gloParent->projectGlbs.end(); ++it) {
            if (it->path == removedPath) {
                gloParent->projectGlbs.erase(it);
                break;
            }
        }
        viewport.removeGlbMesh(pendingDeleteSceneryIdx);
        pushUndo();
        pendingDeleteSceneryIdx = -1;
    } else {
        pendingDeleteSceneryIdx = -1;
    }

    auto exitViewport = [&]() {
        if (viewportActive) {
            viewportActive = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        }
    };

    if (ImGui::GetIO().WantTextInput)
        return;

    auto& kb = KeybindManager::instance();

    if (kb.isTriggered(KeyAction::Undo)) {
        if (mUndoHandler)
            mUndoHandler->doUndo();
    }
    if (kb.isTriggered(KeyAction::Redo)) {
        if (mUndoHandler)
            mUndoHandler->doRedo();
    }
    if (kb.isTriggered(KeyAction::IncrementalSave)) {
        PerformIncrementalSave();
    } else if (kb.isTriggered(KeyAction::SaveProject)) {
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
            std::string filename = currentFilePath;
            size_t lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                filename = filename.substr(lastSlash + 1);
            }
            showInAppNotification("Project Saved: " + filename);
        }
    }
    if (kb.isTriggered(KeyAction::ExportTrack)) {
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
    if (kb.isTriggered(KeyAction::MeasurementPointsWindow)) {
        bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
        if (hasActiveTrack) {
            showMeasurementPoints = !showMeasurementPoints;
        }
    }
    if (kb.isTriggered(KeyAction::ShaderNothing))
        viewport.setTrackShaderMode(0);
    if (kb.isTriggered(KeyAction::ShaderVelocity))
        viewport.setTrackShaderMode(1);
    if (kb.isTriggered(KeyAction::ShaderRollSpeed))
        viewport.setTrackShaderMode(2);
    if (kb.isTriggered(KeyAction::ShaderNormalForce))
        viewport.setTrackShaderMode(3);
    if (kb.isTriggered(KeyAction::ShaderLateralForce))
        viewport.setTrackShaderMode(4);
    if (kb.isTriggered(KeyAction::ShaderTrackFlexion))
        viewport.setTrackShaderMode(5);

    if (kb.isTriggered(KeyAction::SelectPrevSection) && !viewportActive) {
        bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
        if (hasActiveTrack) {
            track* curTrack = trackList[activeTrackIdx]->trackData;
            int currentSec = leftPanel.selectedSectionIdx;
            if (currentSec > -1) {
                leftPanel.selectedSectionIdx--;
                curTrack->activeSection = (leftPanel.selectedSectionIdx == -1) ? nullptr : curTrack->lSections[leftPanel.selectedSectionIdx];
                if (viewport.getPOVMode()) {
                    viewport.setPOVMode(false);
                }
                viewport.markSceneDirty();
                if (gloParent->mOptions->autoFocusOnSelection)
                    viewport.focusOnSection(leftPanel.selectedSectionIdx);
            }
        }
    }
    if (kb.isTriggered(KeyAction::SelectNextSection) && !viewportActive) {
        bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
        if (hasActiveTrack) {
            track* curTrack = trackList[activeTrackIdx]->trackData;
            int maxSec = (int)curTrack->lSections.size();
            int currentSec = leftPanel.selectedSectionIdx;
            if (currentSec < maxSec - 1) {
                leftPanel.selectedSectionIdx++;
                curTrack->activeSection = curTrack->lSections[leftPanel.selectedSectionIdx];
                if (viewport.getPOVMode()) {
                    viewport.setPOVMode(false);
                }
                viewport.markSceneDirty();
                if (gloParent->mOptions->autoFocusOnSelection)
                    viewport.focusOnSection(leftPanel.selectedSectionIdx);
            }
        }
    }

    if (kb.isTriggered(KeyAction::ViewPerspective)) {
        viewport.setViewMode(Viewport::ViewMode::Perspective);
    }
    if (kb.isTriggered(KeyAction::ViewTopOrtho)) {
        viewport.setViewMode(Viewport::ViewMode::Top);
    }
    if (kb.isTriggered(KeyAction::ViewSideOrtho)) {
        viewport.setViewMode(Viewport::ViewMode::Side);
    }
    if (kb.isTriggered(KeyAction::ViewFrontOrtho)) {
        viewport.setViewMode(Viewport::ViewMode::Front);
    }
    if (kb.isTriggered(KeyAction::SaveScreenshot)) {
        exitViewport();
        auto f = pfd::save_file("Save screenshot", "screenshot.png", {"Image Files", "*.png", "All Files", "*"}).result();
        if (!f.empty()) {
            if (f.find(".png") == std::string::npos)
                f += ".png";
            viewport.captureScreenshot(gloParent->mOptions->screenshotMultiplier, f, trackList);
        }
    }
    if (kb.isTriggered(KeyAction::ResetView)) {
        viewport.resetView();
    }
    if (kb.isTriggered(KeyAction::DeleteGlbMesh)) {
        if (viewport.hasSelectedGlb()) {
            std::string removedPath = "";
            for (const auto& gm : viewport.glbMeshes) {
                if (gm.selected) {
                    removedPath = gm.path;
                    break;
                }
            }
            if (!removedPath.empty()) {
                for (auto it = gloParent->projectGlbs.begin(); it != gloParent->projectGlbs.end(); ++it) {
                    if (it->path == removedPath) {
                        gloParent->projectGlbs.erase(it);
                        break;
                    }
                }
            }
            viewport.deleteSelectedGlb();
            pushUndo();
        }
    }
    if (kb.isTriggered(KeyAction::DeleteSection) && !viewportActive) {
        bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
        if (hasActiveTrack) {
            track* curTrack = trackList[activeTrackIdx]->trackData;
            bool canDelete = (leftPanel.selectedSectionIdx >= 0);
            if (canDelete) {
                curTrack->removeSection(leftPanel.selectedSectionIdx);
                leftPanel.selectedSectionIdx--;
                if (leftPanel.selectedSectionIdx < -1)
                    leftPanel.selectedSectionIdx = -1;
                if (leftPanel.selectedSectionIdx >= 0)
                    curTrack->activeSection = curTrack->lSections.at(leftPanel.selectedSectionIdx);
                else
                    curTrack->activeSection = nullptr;
                viewport.markSceneDirty();
                if (gloParent->mOptions->autoFocusOnSelection)
                    viewport.focusOnSection(leftPanel.selectedSectionIdx);
                pushUndo();
            }
        }
    }
    if (kb.isTriggered(KeyAction::DeleteTransition) && !viewportActive) {
        bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
        if (hasActiveTrack && gloParent->selectedFunc) {
            subfunc* sf = gloParent->selectedFunc;
            func* fParent = sf->parent;
            if (fParent && fParent->funcList.size() > 1) {
                int idx = fParent->getSubfuncNumber(sf);
                fParent->removeSubFunction(idx);
                if (!fParent->funcList.empty()) {
                    int newIdx = (idx > 0) ? idx - 1 : 0;
                    if (newIdx < (int)fParent->funcList.size()) {
                        gloParent->selectedFunc = fParent->funcList[newIdx];
                    } else {
                        gloParent->selectedFunc = nullptr;
                    }
                } else {
                    gloParent->selectedFunc = nullptr;
                }
                track* curTrack = trackList[activeTrackIdx]->trackData;
                curTrack->requestUpdateTrack(fParent->secParent, 0);
                viewport.markSceneDirty();
                pushUndo();
            }
        }
    }
    if (gloParent->selectedFunc && !viewportActive) {
        bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
        if (hasActiveTrack) {
            if (kb.isTriggered(KeyAction::PrependTransition)) {
                subfunc* sf = gloParent->selectedFunc;
                func* fParent = sf->parent;
                if (fParent) {
                    int idx = fParent->getSubfuncNumber(sf);
                    fParent->appendSubFunction(1.0f, idx - 1);
                    gloParent->selectedFunc = fParent->funcList[idx];
                    track* curTrack = trackList[activeTrackIdx]->trackData;
                    curTrack->requestUpdateTrack(fParent->secParent, 0);
                    viewport.markSceneDirty();
                    pushUndo();
                }
            }
            if (kb.isTriggered(KeyAction::AppendTransition)) {
                subfunc* sf = gloParent->selectedFunc;
                func* fParent = sf->parent;
                if (fParent) {
                    int idx = fParent->getSubfuncNumber(sf);
                    fParent->appendSubFunction(1.0f, idx);
                    gloParent->selectedFunc = fParent->funcList[idx + 1];
                    track* curTrack = trackList[activeTrackIdx]->trackData;
                    curTrack->requestUpdateTrack(fParent->secParent, 0);
                    viewport.markSceneDirty();
                    pushUndo();
                }
            }
        }
    }
}

void Application::Render(float deltaTime) {
    if (showWelcomeScreen) {
        RenderWelcomeScreen();
        if (showOptions) {
            RenderOptionsWindow();
        }
        return;
    }

    bool viewportOverlayZTrigger = false;
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
                CreateNewProject();
            }
            if (ImGui::MenuItem("Open...")) {
                exitViewport();
                auto f = pfd::open_file("Choose project file", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                if (!f.empty()) {
                    loadProjectFile(f[0]);
                }
            }
            if (ImGui::BeginMenu("Open Recent")) {
                if (recentFiles.empty()) {
                    ImGui::MenuItem("No Recent Files", nullptr, false, false);
                } else {
                    for (const auto& path : recentFiles) {
                        std::string displayLabel = path;
                        size_t lastSlash = displayLabel.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            displayLabel = displayLabel.substr(lastSlash + 1);
                        }
                        if (ImGui::MenuItem((displayLabel + "##recent_" + path).c_str())) {
                            exitViewport();
                            loadProjectFile(path);
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent Files")) {
                        clearRecentFiles();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
            if (ImGui::BeginMenu("Import")) {
                ImGui::SeparatorText("Geometry");
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
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Import one or more track designs from another FVD++ project file (.fvd) into the current workspace.");
                }
                if (ImGui::MenuItem("Reference Track (NoLimits 2 CSV)...")) {
                    exitViewport();
                    auto f = pfd::open_file("Import NoLimits 2 CSV as Reference Track", ".", {"NoLimits 2 CSV Files", "*.csv *.txt", "All Files", "*"}).result();
                    if (!f.empty()) {
                        importReferenceTrack(f[0]);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Import a NoLimits 2 CSV spline file as a read-only reference track with customizable track style.");
                }
                if (ImGui::MenuItem("Scenery...")) {
                    exitViewport();
                    auto f = pfd::open_file("Open .glb file", ".", {".glb Files", "*.glb", "All Files", "*"}).result();
                    if (!f.empty() && viewport.addGlbMesh(f[0])) {
                        DummyGlobal::GlbSettings s;
                        s.path = f[0];
                        s.visible = true;
                        gloParent->projectGlbs.push_back(s);
                        pushUndo();
                    }
                }
                ImGui::SeparatorText("Reference");
                if (ImGui::MenuItem("Measurement Points...", nullptr, false, hasActiveTrack)) {
                    exitViewport();
                    auto f = pfd::open_file("Import Measurement Points", ".", {"FVD Measurement Files", "*.fvdmeasure"}).result();
                    if (!f.empty())
                        trackList[activeTrackIdx]->trackData->importMeasurementPoints(f[0]);
                }
                ImGui::SeparatorText("Environment");
                if (ImGui::MenuItem("Environment Preset...")) {
                    exitViewport();
                    auto file = pfd::open_file("Import Environment", "skybox",
                                               {"FVD Environment", "*.fvdenv", "All Files", "*"})
                                    .result();
                    if (!file.empty())
                        viewport.loadEnvironmentPreset(file[0]);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Export")) {
                ImGui::SeparatorText("Geometry");
                if (ImGui::MenuItem("Track...", "Ctrl+E", false, hasActiveTrack)) {
                    exitViewport();
                    showExportPopup = true;
                }
                ImGui::SeparatorText("Reference & Styles");
                if (ImGui::MenuItem("Custom Track Model...", nullptr, false, hasActiveTrack)) {
                    exitViewport();
                    auto f = pfd::save_file("Export Custom Track", "custom.fvdtrack", {"FVD Track Files", "*.fvdtrack"}).result();
                    if (!f.empty())
                        trackList[activeTrackIdx]->trackData->exportCustomTrack(f);
                }
                if (ImGui::MenuItem("Measurement Points...", nullptr, false, hasActiveTrack)) {
                    exitViewport();
                    auto f = pfd::save_file("Export Measurement Points", "train.fvdmeasure", {"FVD Measurement Files", "*.fvdmeasure"}).result();
                    if (!f.empty())
                        trackList[activeTrackIdx]->trackData->exportMeasurementPoints(f);
                }
                ImGui::SeparatorText("Environment");
                if (ImGui::MenuItem("Environment Preset...")) {
                    exitViewport();
                    std::string file = pfd::save_file(
                                           "Export Environment",
                                           viewport.suggestedEnvironmentPresetPath(),
                                           {"FVD Environment", "*.fvdenv", "All Files", "*"})
                                           .result();
                    if (!file.empty()) {
                        if (!hasCaseInsensitiveExtension(file, ".fvdenv"))
                            file += ".fvdenv";
                        viewport.saveEnvironmentPreset(file);
                    }
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
                    std::string filename = currentFilePath;
                    size_t lastSlash = filename.find_last_of("/\\");
                    if (lastSlash != std::string::npos) {
                        filename = filename.substr(lastSlash + 1);
                    }
                    showInAppNotification("Project Saved: " + filename);
                    addRecentFile(currentFilePath);
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
                    std::string filename = currentFilePath;
                    size_t lastSlash = filename.find_last_of("/\\");
                    if (lastSlash != std::string::npos) {
                        filename = filename.substr(lastSlash + 1);
                    }
                    showInAppNotification("Project Saved: " + filename);
                    addRecentFile(currentFilePath);
                }
            }
            if (ImGui::MenuItem("Incremental Save", "Ctrl+Alt+S")) {
                PerformIncrementalSave();
            }
            ImGui::Separator();
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
            if (ImGui::MenuItem("Options...")) {
                exitViewport();
                showOptions = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) {
                exitViewport();
                showExitPopup = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::SeparatorText("History");
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, mUndoHandler && mUndoHandler->canUndo())) {
                if (mUndoHandler)
                    mUndoHandler->doUndo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, mUndoHandler && mUndoHandler->canRedo())) {
                if (mUndoHandler)
                    mUndoHandler->doRedo();
            }
            ImGui::SeparatorText("Workspace Editors");
            bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size();
            if (ImGui::MenuItem("Environment...", nullptr, &showEnvironmentWindow)) {
                exitViewport();
            }
            std::string scMeasure = KeybindManager::formatChord(KeybindManager::instance().getChord(KeyAction::MeasurementPointsWindow));
            if (ImGui::MenuItem("Measurement Points...", scMeasure == "None" ? nullptr : scMeasure.c_str(), &showMeasurementPoints, hasActiveTrack)) {
                exitViewport();
            }
            if (ImGui::MenuItem("Parametric Track Editor...", nullptr, &showParametricTrackEditor, hasActiveTrack)) {
                exitViewport();
            }
            ImGui::SeparatorText("Track Operations");
            extern Viewport* gViewport;
            bool canFork = (hasActiveTrack && gViewport && gViewport->getPOVNode() != nullptr);
            if (ImGui::MenuItem("Fork Track at Playhead", nullptr, false, canFork)) {
                forkTrack(trackList[activeTrackIdx], gViewport->getPOVPos());
                leftPanel.selectedSectionIdx = -1;
                gloParent->selectedFunc = nullptr;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (!hasActiveTrack) {
                    ImGui::SetTooltip("Fork Track (at Playhead)\n\nBranches off a new, empty track starting from the playhead's current 3D position, heading, and speed, leaving the original track untouched.\n\n(Disabled: No active track selected)");
                } else if (!gViewport || gViewport->getPOVNode() == nullptr) {
                    ImGui::SetTooltip("Fork Track (at Playhead)\n\nBranches off a new, empty track starting from the playhead's current 3D position, heading, and speed, leaving the original track untouched.\n\n(Disabled: Position the timeline/POV playhead on the track to choose fork point)");
                } else {
                    ImGui::SetTooltip("Fork Track (at Playhead)\n\nBranches off a new, empty track starting from the playhead's current 3D position, heading, and speed, leaving the original track untouched.");
                }
            }

            bool canSplit = (hasActiveTrack && leftPanel.selectedSectionIdx > 0 &&
                             !trackList[activeTrackIdx]->trackData->isReferenceTrack());
            if (ImGui::MenuItem("Split Track at Selected Section", nullptr, false, canSplit)) {
                splitTrack(trackList[activeTrackIdx], leftPanel.selectedSectionIdx);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (!hasActiveTrack) {
                    ImGui::SetTooltip("Split Track (at Section)\n\nCuts this track into two independent tracks: the selected section and all following sections become a new track anchored in place.\n\n(Disabled: No active track selected)");
                } else if (leftPanel.selectedSectionIdx <= 0) {
                    ImGui::SetTooltip("Split Track (at Section)\n\nCuts this track into two independent tracks: the selected section and all following sections become a new track anchored in place.\n\n(Disabled: Select section 1 or later to choose split point)");
                } else {
                    ImGui::SetTooltip("Split Track (at Section)\n\nCuts the active track at section %d into two independent tracks. The selected section and all following sections move into Part 2, anchored in world space.", leftPanel.selectedSectionIdx);
                }
            }

            bool canMerge = (hasActiveTrack && trackList.size() > 1 &&
                             !trackList[activeTrackIdx]->trackData->isReferenceTrack());
            if (ImGui::BeginMenu("Merge Track Into Active...", canMerge)) {
                for (int i = 0; i < static_cast<int>(trackList.size()); i++) {
                    if (i == activeTrackIdx)
                        continue;
                    if (trackList[i]->trackData->isReferenceTrack())
                        continue;
                    if (ImGui::MenuItem(trackList[i]->trackData->name.c_str())) {
                        mergeTracks(trackList[activeTrackIdx], trackList[i]);
                        break;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (!hasActiveTrack) {
                    ImGui::SetTooltip("Merge Track Into Active\n\nAppends another track's sections onto the end of the active track, combining them into a single continuous track.\n\n(Disabled: No active track selected)");
                } else if (trackList.size() <= 1) {
                    ImGui::SetTooltip("Merge Track Into Active\n\nAppends another track's sections onto the end of the active track, combining them into a single continuous track.\n\n(Disabled: Requires at least 2 tracks in the project)");
                } else if (trackList[activeTrackIdx]->trackData->isReferenceTrack()) {
                    ImGui::SetTooltip("Merge Track Into Active\n\nAppends another track's sections onto the end of the active track, combining them into a single continuous track.\n\n(Disabled: Cannot merge into a reference track)");
                } else {
                    ImGui::SetTooltip("Merge Track Into Active\n\nAppends another track's sections onto the end of '%s', combining them into a single continuous track.", trackList[activeTrackIdx]->trackData->name.c_str());
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::SeparatorText("Window Layout");
            if (ImGui::MenuItem("Reset Layout"))
                forceResetLayout = true;
            ImGui::SeparatorText("Display & Analysis");
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
#if 0 // Disabled for public release
        if (ImGui::BeginMenu("Developer")) {
            if (ImGui::MenuItem("Use Legacy Heartline Math", nullptr, &gloParent->mOptions->useLegacyHeartline)) {
                if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
                    track* activeTrack = trackList[activeTrackIdx]->trackData;
                    if (activeTrack) {
                        activeTrack->requestUpdateTrack(0, 0);
                        activeTrack->processPendingUpdates();
                    }
                }
            }
            if (ImGui::MenuItem("Test Crash (Null Pointer)")) {
                LOG_INFO("Triggering intentional crash...");
                int* p = nullptr;
                *p = 123;
            }
            ImGui::EndMenu();
        }
#endif
        if (ImGui::BeginMenu("About")) {
            if (ImGui::MenuItem("Version")) {
                exitViewport();
                showAboutDialog = true;
            }
            if (ImGui::MenuItem("Check for Updates...")) {
                if (!curlAvailable) {
                    showInAppNotification("Update check failed: curl is not available");
                } else if (!updateCheckFinished) {
                    showInAppNotification("Update check in progress, please wait...");
                } else if (updateAvailable) {
                    exitViewport();
                    showUpdatePopup = true;
                } else {
                    showInAppNotification("FVD++ is up to date!");
                }
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

    if (firstFrame || forceResetLayout) {
        bool forced = forceResetLayout;
        firstFrame = false;
        forceResetLayout = false;
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
        if (forced || (node && node->ChildNodes[0] == 0)) {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_id_top_half, dock_id_bottom_half;
            ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.60f, &dock_id_top_half, &dock_id_bottom_half);

            float metricsLineHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
            float topHalfHeight = ImGui::GetMainViewport()->Size.y * 0.60f;
            float metricsRatio = (topHalfHeight > 0.0f) ? std::clamp(metricsLineHeight / topHalfHeight, 0.01f, 0.15f) : 0.05f;

            ImGuiID dock_id_metrics, dock_id_top_content;
            ImGui::DockBuilderSplitNode(dock_id_top_half, ImGuiDir_Down, metricsRatio, &dock_id_metrics, &dock_id_top_content);

            ImGuiDockNode* metrics_node = ImGui::DockBuilderGetNode(dock_id_metrics);
            if (metrics_node) {
                metrics_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResizeY;
                metrics_node->SizeRef.y = metricsLineHeight;
            }

            ImGuiID dock_id_left_top, dock_id_viewport;
            ImGui::DockBuilderSplitNode(dock_id_top_content, ImGuiDir_Left, 0.17f, &dock_id_left_top, &dock_id_viewport);

            ImGuiDockNode* viewport_node = ImGui::DockBuilderGetNode(dock_id_viewport);
            if (viewport_node)
                viewport_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

            ImGuiID dock_id_left_bottom, dock_id_bottom_graphs;
            ImGui::DockBuilderSplitNode(dock_id_bottom_half, ImGuiDir_Left, 0.17f, &dock_id_left_bottom, &dock_id_bottom_graphs);

            ImGui::DockBuilderDockWindow("Tracks", dock_id_left_top);
            ImGui::DockBuilderDockWindow("Sections", dock_id_left_top);
            // ImGui::DockBuilderDockWindow("Smoothing", dock_id_left_top);
            ImGui::DockBuilderDockWindow("Colors", dock_id_left_top);
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

    // Check if background thread found an update and we haven't prompted the user yet
    if (updateAvailable && !hasTriggeredUpdatePopup) {
        exitViewport();
        showUpdatePopup = true;
        hasTriggeredUpdatePopup = true;
    }

    if (showUpdatePopup) {
        ImGui::SetNextWindowSize(ImVec2(350, 150), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 175.0f, ImGui::GetMainViewport()->Size.y / 2.0f - 75.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Update Available", &showUpdatePopup, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::Text("A new version of FVD++ is available!");
        ImGui::Separator();
        ImGui::Text("Installed Version: %s", FVD_VERSION);
        ImGui::Text("Latest Version:    %s", remoteVersion.c_str());
        ImGui::Separator();

        if (ImGui::Button("Visit Download Page")) {
#if defined(_WIN32)
            ShellExecuteA(NULL, "open", "https://github.com/H27CK/openFVeiD/releases/latest", NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
            [[maybe_unused]] int result = std::system("open https://github.com/H27CK/openFVeiD/releases/latest");
#else
            [[maybe_unused]] int result = std::system("xdg-open https://github.com/H27CK/openFVeiD/releases/latest");
#endif
            showUpdatePopup = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Skip This Version")) {
            saveSkippedVersion(remoteVersion);
            showUpdatePopup = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remind Me Later")) {
            showUpdatePopup = false;
        }

        ImGui::End();
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
        RenderOptionsWindow();
    }

    if (showTrainGenerator) {
        RenderTrainGeneratorWindow();
    }

    if (showMeasurementPoints) {
        RenderMeasurementPointsWindow();
    }

    if (showParametricTrackEditor) {
        RenderParametricTrackEditorWindow();
    }

    if (showEnvironmentWindow) {
        RenderEnvironmentWindow();
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

        ImGui::Combo("Format", &exportFormat, "NoLimits 2 Element (*.nl2elem)\0NoLimits 2 CSV (*.csv)\0");
        ImGui::InputFloat("Dist. per Node (m)", &exportDistPerNode, 0.1f, 1.0f, "%.2f");
        if (exportDistPerNode < 0.1f)
            exportDistPerNode = 0.1f;
        ImGui::SliderInt("From Section", &exportFromSection, 0, numSections - 1);
        ImGui::SliderInt("To Section", &exportToSection, exportFromSection, numSections - 1);

        // Custom Number Format Input (below To Section)
        if (exportFormat == 0) {
            ImGui::BeginDisabled();
        }
        static char numFormatBuf[32] = "";
        static int lastExportFormat = -1;
        if (lastExportFormat != exportFormat) {
            lastExportFormat = exportFormat;
            strncpy(numFormatBuf, exportNumFormat.c_str(), sizeof(numFormatBuf) - 1);
        }
        if (ImGui::InputText("Number Format", numFormatBuf, sizeof(numFormatBuf))) {
            exportNumFormat = numFormatBuf;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", "Specify custom C format specifier for CSV numbers (e.g. %%.6f or %%e).");
        }
        if (exportFormat == 0) {
            ImGui::EndDisabled();
        }

        // Heartline Checkbox and Local Space on the same line
        if (exportFormat == 0) {
            exportHeartline = false;
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Heartline", &exportHeartline);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export coordinates relative to the rider's heartline instead of the rail center.");
        }
        if (exportFormat == 0) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        ImGui::Checkbox("Local Space", &gloParent->mOptions->relativeExport);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Export coordinates in local space starting at (0,0,0) instead of absolute world coordinates.");
        }
        ImGui::Separator();
        if (ImGui::Button("Export", ImVec2(120, 0))) {
            std::string filter, ext;
            if (exportFormat == 0) {
                filter = "NL2 Element (*.nl2elem)";
                ext = ".nl2elem";
            } else {
                filter = "NL2 CSV (*.csv)";
                ext = ".csv";
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
                std::string filename = currentFilePath;
                size_t lastSlash = filename.find_last_of("/\\");
                if (lastSlash != std::string::npos) {
                    filename = filename.substr(lastSlash + 1);
                }
                showInAppNotification("Project Saved: " + filename);
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
        leftPanel.render(this);
        ImGuiWindowFlags commonFlags = 0;
        DockStyle::Begin("Transition Editor", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            auto track = trackList[activeTrackIdx];
            if (track->trackData->isReferenceTrack()) {
                ImGui::Text("Reference Track (Transitions Disabled)");
            } else {
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
            }
        } else
            ImGui::Text("No active track.");
        ImGui::End();

        DockStyle::Begin("Viewport", nullptr, commonFlags);
        ImVec2 imagePos(0.0f, 0.0f);
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

                // Commented out to disable selecting meshes via clicking
                /*
                if (ImGui::IsMouseClicked(0)) {
                    ImVec2 mousePos = ImGui::GetMousePos();
                    ImVec2 imagePos = ImGui::GetCursorScreenPos();
                    float localX = mousePos.x - imagePos.x;
                    float localY = mousePos.y - imagePos.y;
                    if (localX >= 0.0f && localX <= viewportPanelSize.x &&
                        localY >= 0.0f && localY <= viewportPanelSize.y) {
                        float ndcX = (localX / viewportPanelSize.x) * 2.0f - 1.0f;
                        float ndcY = (localY / viewportPanelSize.y) * 2.0f - 1.0f;
                        viewport.selectGlbAtRay(ndcX, ndcY);
                    }
                }
                */
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
                if (KeybindManager::instance().isTriggered(KeyAction::TogglePOVMode) && !viewport.getOrthoMode())
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
                    if (KeybindManager::instance().isHeld(KeyAction::MoveForward))
                        moveDelta.z += moveSpeed;
                    if (KeybindManager::instance().isHeld(KeyAction::MoveBackward))
                        moveDelta.z -= moveSpeed;
                    if (KeybindManager::instance().isHeld(KeyAction::MoveLeft))
                        moveDelta.x -= moveSpeed;
                    if (KeybindManager::instance().isHeld(KeyAction::MoveRight))
                        moveDelta.x += moveSpeed;
                    if (ImGui::GetIO().MouseWheel != 0.0f) {
                        float boost = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? gloParent->mOptions->sprintMultiplier : 1.0f;
                        viewport.moveCamera(glm::vec3(0.0f, ImGui::GetIO().MouseWheel * 2.0f * boost, 0.0f));
                    }
                }
                if (viewport.getPOVMode()) {
                    float boost = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? gloParent->mOptions->sprintMultiplier : 1.0f;
                    float zMovement = 0.0f;
                    if (KeybindManager::instance().isHeld(KeyAction::MoveForward))
                        zMovement += 1.0f;
                    if (KeybindManager::instance().isHeld(KeyAction::MoveBackward))
                        zMovement -= 1.0f;
                    viewport.movePOVCamera(zMovement * boost, deltaTime);
                    float heightSpeed = 5.0f * deltaTime * boost;
                    if (KeybindManager::instance().isHeld(KeyAction::POVHeightUp))
                        viewport.adjustPOVHeight(heightSpeed);
                    if (KeybindManager::instance().isHeld(KeyAction::POVHeightDown))
                        viewport.adjustPOVHeight(-heightSpeed);
                    if (KeybindManager::instance().isTriggered(KeyAction::POVHeightReset))
                        viewport.resetPOVHeight();
                    if (ImGui::GetIO().MouseWheel != 0.0f)
                        viewport.adjustPOVHeight(ImGui::GetIO().MouseWheel * 0.5f * boost);
                } else
                    viewport.moveCamera(moveDelta);
            } else if (ImGui::IsWindowHovered())
                viewport.zoomCamera(ImGui::GetIO().MouseWheel * 1.0f);

            viewport.setShowPOVMarker3D(true);
            viewport.render(trackList);
            ImGui::Image((ImTextureID)viewport.getOutputTexture(), viewportPanelSize);
            imagePos = ImGui::GetItemRectMin();

            // Track whether the viewport window itself is hovered or focused (including its overlay widgets)
            bool isViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            bool isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            viewportOverlayZTrigger = (isViewportHovered || isViewportFocused);
        }
        ImGui::End();

        if (viewportOverlayZTrigger && KeybindManager::instance().isHeld(KeyAction::OverlayScenery)) {
            int numMeshes = (int)viewport.glbMeshes.size();
            static int highlightIdx = 0;
            if (numMeshes > 0) {
                if (highlightIdx >= numMeshes)
                    highlightIdx = numMeshes - 1;
                if (highlightIdx < 0)
                    highlightIdx = 0;

                if (KeybindManager::instance().isTriggered(KeyAction::OverlaySceneryPrev)) {
                    highlightIdx = (highlightIdx - 1 + numMeshes) % numMeshes;
                }
                if (KeybindManager::instance().isTriggered(KeyAction::OverlaySceneryNext)) {
                    highlightIdx = (highlightIdx + 1) % numMeshes;
                }
                if (KeybindManager::instance().isTriggered(KeyAction::OverlaySceneryToggleVis)) {
                    viewport.glbMeshes[highlightIdx].visible = !viewport.glbMeshes[highlightIdx].visible;
                    viewport.markSceneDirty();
                }
                if (KeybindManager::instance().isTriggered(KeyAction::OverlaySceneryDelete)) {
                    pendingDeleteSceneryIdx = highlightIdx;
                    numMeshes--;
                    if (highlightIdx >= numMeshes)
                        highlightIdx = numMeshes - 1;
                    if (highlightIdx < 0)
                        highlightIdx = 0;
                }
            }

            ImGui::SetNextWindowPos(ImVec2(imagePos.x + 10.0f, imagePos.y + 10.0f));
            ImGui::SetNextWindowBgAlpha(0.65f);

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav;

            if (ImGui::Begin("SceneryOverlay", nullptr, flags)) {
                std::string sceneryKeyName = KeybindManager::formatChord(KeybindManager::instance().getChord(KeyAction::OverlayScenery));
                ImGui::Text("Scenery Meshes (%s Held)", sceneryKeyName.c_str());
                ImGui::TextDisabled("Up/Down: select; H: toggle; X: delete");
                ImGui::Separator();

                if (viewport.glbMeshes.empty()) {
                    ImGui::TextDisabled("No scenery meshes loaded.");
                } else {
                    for (size_t i = 0; i < viewport.glbMeshes.size(); ++i) {
                        auto& gm = viewport.glbMeshes[i];

                        std::string filename = gm.path;
                        size_t lastSlash = filename.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            filename = filename.substr(lastSlash + 1);
                        }

                        bool isHidden = !gm.visible;
                        if (isHidden) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        }

                        bool isHighlighted = (i == (size_t)highlightIdx);
                        std::string label = filename + "##overlay_" + std::to_string(i);
                        if (ImGui::Selectable(label.c_str(), isHighlighted)) {
                            highlightIdx = (int)i;
                        }

                        if (isHidden) {
                            ImGui::PopStyleColor();
                        }
                    }
                }
                ImGui::End();
            }
        } else if (viewportOverlayZTrigger && KeybindManager::instance().isHeld(KeyAction::OverlayWarnings)) {
            // Render Track Safety Warnings overlay in the exact same top-left spot and with the same style as SceneryOverlay
            if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
                track* curTrack = trackList[activeTrackIdx]->trackData;

                struct WarningItem {
                    std::string text;
                    ImVec4 color;
                };
                std::vector<WarningItem> safetyWarnings;

                for (size_t s = 0; s < curTrack->lSections.size(); ++s) {
                    section* sec = curTrack->lSections[s];
                    std::string secTypeStr = "";
                    switch (sec->type) {
                    case straight:
                        secTypeStr = "Straight";
                        break;
                    case curved:
                        secTypeStr = "Curved";
                        break;
                    case forced:
                        secTypeStr = "Forced";
                        break;
                    case geometric:
                        secTypeStr = "Geometric";
                        break;
                    case geometricriderlocal:
                        secTypeStr = "Geometric Rider-Local";
                        break;
                    default:
                        secTypeStr = "Unknown";
                        break;
                    }
                    std::string secLabel = "Section " + std::to_string(s + 1) + " (" + secTypeStr + ")";

                    if (sec->isStalled) {
                        safetyWarnings.push_back({secLabel + ": Train stalled! Speed clamped.", ImVec4(1.0f, 0.4f, 0.4f, 1.0f)}); // Red
                    }

                    bool forceViolated = false;
                    if (curTrack->enableForceLimits) {
                        for (const auto& node : sec->lNodes) {
                            if (node.forceNormal > curTrack->fMaxPosNormal ||
                                node.forceNormal < curTrack->fMaxNegNormal ||
                                node.forceLateral > curTrack->fMaxLateral ||
                                node.forceLateral < curTrack->fMinLateral) {
                                forceViolated = true;
                                break;
                            }
                        }
                    }
                    if (forceViolated) {
                        safetyWarnings.push_back({secLabel + ": Forces exceed safety limits!", ImVec4(1.0f, 0.7f, 0.4f, 1.0f)}); // Orange
                    }

                    bool radiusViolated = false;
                    if (curTrack->enforceMinRadius && curTrack->minRadius > 0.0f) {
                        for (size_t ni = 1; ni < sec->lNodes.size(); ++ni) {
                            const mnode& currNode = sec->lNodes[ni];
                            const mnode& prevNode = sec->lNodes[ni - 1];
                            double dotProd = glm::dot(currNode.vDir, prevNode.vDir);
                            double deltaThetaRad = std::acos(std::clamp(dotProd, -1.0, 1.0));
                            double ds = currNode.fDistFromLast;
                            if (deltaThetaRad > 1e-7) {
                                double physicalRadius = ds / deltaThetaRad;
                                if (physicalRadius < curTrack->minRadius) {
                                    radiusViolated = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (radiusViolated) {
                        safetyWarnings.push_back({secLabel + ": Exceeds minimum radius limit!", ImVec4(1.0f, 0.7f, 0.4f, 1.0f)}); // Orange
                    }
                }

                if (curTrack->isAnyNodeNearGimbalLock) {
                    safetyWarnings.push_back({"Track: Pitch near 90 deg; gimbal lock risk.", ImVec4(1.0f, 0.7f, 0.4f, 1.0f)}); // Orange (matching radius/force limits)
                }

                ImGui::SetNextWindowPos(ImVec2(imagePos.x + 10.0f, imagePos.y + 10.0f));
                ImGui::SetNextWindowBgAlpha(0.65f);

                ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav;

                if (ImGui::Begin("WarningOverlay", nullptr, flags)) {
                    std::string warnKeyName = KeybindManager::formatChord(KeybindManager::instance().getChord(KeyAction::OverlayWarnings));
                    ImGui::Text("Track State (%s Held)", warnKeyName.c_str());
                    ImGui::TextDisabled("Displays active safety warnings and constraints");
                    ImGui::Separator();

                    if (safetyWarnings.empty()) {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "No active safety warnings or issues detected.");
                    } else {
                        for (const auto& item : safetyWarnings) {
                            ImGui::PushStyleColor(ImGuiCol_Text, item.color);
                            ImGui::Text("- %s", item.text.c_str());
                            ImGui::PopStyleColor();
                        }
                    }
                    ImGui::End();
                }
            }
        }

        DockStyle::Begin("Graph List", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            if (trackList[activeTrackIdx]->trackData->isReferenceTrack())
                ImGui::Text("Reference Track (Graphs Disabled)");
            else
                graphView.renderList(trackList[activeTrackIdx]);
        } else
            ImGui::Text("No active track.");
        ImGui::End();

        bool focusResulting = graphView.getAndClearSwitchToResultingTab();

        DockStyle::Begin("Graphs", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            if (trackList[activeTrackIdx]->trackData->isReferenceTrack())
                ImGui::Text("Reference Track (Graphs Disabled)");
            else if (trackList[activeTrackIdx]->trackData->activeSection != nullptr)
                graphView.renderPlot(trackList[activeTrackIdx]);
            else
                ImGui::Text("No data to plot.");
        } else
            ImGui::Text("No active track.");
        ImGui::End();

        if (focusResulting)
            ImGui::SetNextWindowFocus();
        DockStyle::Begin("Resulting Graphs", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            if (trackList[activeTrackIdx]->trackData->isReferenceTrack())
                ImGui::Text("Reference Track (Graphs Disabled)");
            else if (trackList[activeTrackIdx]->trackData->activeSection != nullptr)
                graphView.renderResultingPlot(trackList[activeTrackIdx]);
            else
                ImGui::Text("No data to plot.");
        } else
            ImGui::Text("No active track.");
        ImGui::End();

        DockStyle::Begin("Measurement Graphs", nullptr, commonFlags);
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            if (trackList[activeTrackIdx]->trackData->isReferenceTrack())
                ImGui::Text("Reference Track (Graphs Disabled)");
            else if (trackList[activeTrackIdx]->trackData->activeSection != nullptr) {
                if (graphView.hasMeasurementGraphsVisible(trackList[activeTrackIdx]))
                    graphView.renderMeasurementPlot(trackList[activeTrackIdx]);
                else
                    ImGui::TextDisabled("Add Measurement Points in the Graph List panel to view them here.");
            } else
                ImGui::Text("No data to plot.");
        } else
            ImGui::Text("No active track.");
        ImGui::End();

        DockStyle::Begin("Metrics", nullptr, commonFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        ImGuiDockNode* metricsDockNode = ImGui::GetWindowDockNode();
        if (metricsDockNode) {
            float targetHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
            if (metricsDockNode->SizeRef.y != targetHeight) {
                metricsDockNode->SizeRef.y = targetHeight;
                metricsDockNode->WantLockSizeOnce = true;
            }
            metricsDockNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResizeY;
        }
        if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
            trackHandler* hTrack = trackList[activeTrackIdx];
            if (hTrack->trackData->isReferenceTrack()) {
                ImGui::Text("Reference Track (Metrics Disabled)");
            } else {
                mnode* lastNode = nullptr;
                if (viewport.getPOVNode())
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
                    if (viewport.getPOVNode()) {
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

                // 1. Check if there are any active safety warnings or issues on the track
                bool hasWarnings = curTrack->isAnyNodeNearGimbalLock;
                if (!hasWarnings) {
                    for (section* sec : curTrack->lSections) {
                        if (sec->isStalled) {
                            hasWarnings = true;
                            break;
                        }
                        if (curTrack->enableForceLimits) {
                            for (const auto& node : sec->lNodes) {
                                if (node.forceNormal > curTrack->fMaxPosNormal ||
                                    node.forceNormal < curTrack->fMaxNegNormal ||
                                    node.forceLateral > curTrack->fMaxLateral ||
                                    node.forceLateral < curTrack->fMinLateral) {
                                    hasWarnings = true;
                                    break;
                                }
                            }
                            if (hasWarnings)
                                break;
                        }
                        if (curTrack->enforceMinRadius && curTrack->minRadius > 0.0f) {
                            for (size_t ni = 1; ni < sec->lNodes.size(); ++ni) {
                                const mnode& currNode = sec->lNodes[ni];
                                const mnode& prevNode = sec->lNodes[ni - 1];
                                double dotProd = glm::dot(currNode.vDir, prevNode.vDir);
                                double deltaThetaRad = std::acos(std::clamp(dotProd, -1.0, 1.0));
                                double ds = currNode.fDistFromLast;
                                if (deltaThetaRad > 1e-7) {
                                    double physicalRadius = ds / deltaThetaRad;
                                    if (physicalRadius < curTrack->minRadius) {
                                        hasWarnings = true;
                                        break;
                                    }
                                }
                            }
                            if (hasWarnings)
                                break;
                        }
                    }
                }

                if (hasWarnings) {
                    std::string warnKeyName = KeybindManager::formatChord(KeybindManager::instance().getChord(KeyAction::OverlayWarnings));
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "Warning: Safety warnings or issues detected (press %s in viewport)", warnKeyName.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "System Status: Ready");
                }

                // Align other elements to the right side of the status bar
                ImGui::SameLine();

                trackHandler* activeHandler = trackList[activeTrackIdx];
                size_t trackVertices = activeHandler->mMesh ? activeHandler->mMesh->getTotalRenderedVertices() : 0;
                size_t glbVertices = 0;
                for (const auto& sm : viewport.glbMeshes) {
                    if (sm.visible) {
                        for (const auto& prim : sm.primitives) {
                            glbVertices += (size_t)prim.vertexCount;
                        }
                    }
                }

                int totalPoints = curTrack->getNumPoints();
                int currentSectionNodes = curTrack->activeSection ? (int)curTrack->activeSection->lNodes.size() : 0;
                double updateTime = curTrack->lastUpdateTimeMs;
                int totalPlotted = graphView.getTotalPlottedPoints();
                double spacingLimit = (double)gloParent->mOptions->graphSpacingLimit;

                char statBuf[384];
                snprintf(statBuf, sizeof(statBuf), "Track Verts: %s   |   Scenery Verts: %s   |   Total Nodes: %d   |   Section Nodes: %d   |   Down-sampling Spacing: %.3f m (Plotted: %d)   |   Layout Update: %.2f ms",
                         formatWithCommas(trackVertices).c_str(), formatWithCommas(glbVertices).c_str(),
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

    if (notificationTimer > 0.0f) {
        notificationTimer -= deltaTime;

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 work_pos = vp->WorkPos;
        ImVec2 work_size = vp->WorkSize;

        ImVec2 window_pos = ImVec2(work_pos.x + work_size.x - 15.0f, work_pos.y + work_size.y - 15.0f);
        ImVec2 window_pos_pivot = ImVec2(1.0f, 1.0f);

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);

        float alpha = std::min(1.0f, notificationTimer);
        ImGui::SetNextWindowBgAlpha(0.85f * alpha);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, alpha));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.5f * alpha));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f, 10.0f));

        if (ImGui::Begin("##InAppNotification", nullptr, window_flags)) {
            ImGui::Text("%s", notificationMessage.c_str());
            ImGui::End();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
    }
}

void Application::RenderOptionsWindow() {
    if (!showOptions)
        return;

    ImGui::SetNextWindowSize(ImVec2(540, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 270.0f, ImGui::GetMainViewport()->Size.y / 2.0f - 290.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Options", &showOptions, ImGuiWindowFlags_NoDocking);

    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("OptionsGeneralTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

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
            ImGui::Text("Theme");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int themeIdx = gloParent->mOptions->theme;
            const char* themes[] = {"Dark", "Light", "Classic", "Soft Dark", "Soft Light"};
            if (ImGui::Combo("##Theme", &themeIdx, themes, IM_ARRAYSIZE(themes))) {
                gloParent->mOptions->theme = themeIdx;
                gloParent->mOptions->tabHighlightColor = GetDefaultTabHighlightColor(themeIdx);
                ApplyTheme(themeIdx);
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Tab Highlight Color");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::ColorEdit3("##TabHighlightColor", &gloParent->mOptions->tabHighlightColor.x, ImGuiColorEditFlags_NoInputs)) {
                ApplyTheme(gloParent->mOptions->theme);
            }
            if (ImGui::BeginPopupContextItem("##TabHighlightContext")) {
                if (ImGui::MenuItem("Reset to Theme Default")) {
                    gloParent->mOptions->tabHighlightColor = GetDefaultTabHighlightColor(gloParent->mOptions->theme);
                    ApplyTheme(gloParent->mOptions->theme);
                }
                ImGui::EndPopup();
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
            ImGui::Text("Units");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("##Measure", &gloParent->mOptions->measures, "Metric (m, m/s)\0Metric (m, km/h)\0English (ft, mph)\0");

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Default Transitions", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("OptionsTransitionsTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            const char* typesRoll[] = {"Linear", "Quadratic", "Cubic", "Quartic", "Quintic", "Sinusoidal", "Plateau", "ToZero", "Custom"};
            const char* typesForce[] = {"Linear", "Quadratic", "Cubic", "Quartic", "Quintic", "Sinusoidal", "Plateau", "Custom"};

            // 1. Roll Rate
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Roll Rate");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int rIdx = gloParent->mOptions->defaultTransitionRoll;
            if (rIdx == 9)
                rIdx = 8;
            if (ImGui::Combo("##DefaultTransitionRoll", &rIdx, typesRoll, IM_ARRAYSIZE(typesRoll))) {
                if (rIdx == 8)
                    rIdx = 9;
                gloParent->mOptions->defaultTransitionRoll = rIdx;
            }

            // 2. Normal Force
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Normal Force");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int nIdx = gloParent->mOptions->defaultTransitionNormal;
            if (nIdx == 9)
                nIdx = 7;
            if (ImGui::Combo("##DefaultTransitionNormal", &nIdx, typesForce, IM_ARRAYSIZE(typesForce))) {
                if (nIdx == 7)
                    nIdx = 9;
                gloParent->mOptions->defaultTransitionNormal = nIdx;
            }

            // 3. Lateral Force
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Lateral Force");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int lIdx = gloParent->mOptions->defaultTransitionLateral;
            if (lIdx == 9)
                lIdx = 7;
            if (ImGui::Combo("##DefaultTransitionLateral", &lIdx, typesForce, IM_ARRAYSIZE(typesForce))) {
                if (lIdx == 7)
                    lIdx = 9;
                gloParent->mOptions->defaultTransitionLateral = lIdx;
            }

            // 4. Pitch
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Pitch");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int pIdx = gloParent->mOptions->defaultTransitionPitch;
            if (pIdx == 9)
                pIdx = 7;
            if (ImGui::Combo("##DefaultTransitionPitch", &pIdx, typesForce, IM_ARRAYSIZE(typesForce))) {
                if (pIdx == 7)
                    pIdx = 9;
                gloParent->mOptions->defaultTransitionPitch = pIdx;
            }

            // 5. Yaw
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Yaw");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int yIdx = gloParent->mOptions->defaultTransitionYaw;
            if (yIdx == 9)
                yIdx = 7;
            if (ImGui::Combo("##DefaultTransitionYaw", &yIdx, typesForce, IM_ARRAYSIZE(typesForce))) {
                if (yIdx == 7)
                    yIdx = 9;
                gloParent->mOptions->defaultTransitionYaw = yIdx;
            }

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("OptionsGraphicsTable", 2, ImGuiTableFlags_SizingFixedFit)) {
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
                ImGui::SetTooltip("Limits the maximum frame rate to save CPU/GPU power.");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Look-ahead Smoothing");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Smooths out camera when in POV mode. Turn off to match the behaviour of old FVD.");
            }
            ImGui::TableNextColumn();
            ImGui::Checkbox("##LookAheadSmoothing", &gloParent->mOptions->lookAheadPovSmoothing);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Smooths out camera when in POV mode. Turn off to match the behaviour of old FVD.");
            }

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
            ImGui::Text("Scenery Shadows (Exp.)");
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##GlbShadows", &gloParent->mOptions->glbShadowsEnabled)) {
                viewport.markSceneDirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("EXPERIMENTAL: Enables planar shadows for imported scenery geometry.\nMay impact performance on large models.");

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
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Track Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("OptionsEditorTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

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
                if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
                    graphView.updateData(trackList[activeTrackIdx]);
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

            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("OptionsControlsTable", 2, ImGuiTableFlags_SizingFixedFit)) {
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
                ImGui::SetTooltip("Sets the multiplier for the scroll wheel step size over input fields (default: 1.0).");
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
                ImGui::SetTooltip("Sets the multiplier for the Ctrl + Scroll Wheel step size over input fields (default: 1.0).");
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
                ImGui::SetTooltip("Sets the multiplier for the Shift + Scroll Wheel step size over input fields (default: 10.0).");
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputFloat("##ShiftScrollInc", &gloParent->mOptions->scrollShiftIncrement, 0.1f, 10.0f, "%.3f")) {
                gloParent->mOptions->scrollShiftIncrement = std::max(0.0001f, gloParent->mOptions->scrollShiftIncrement);
            }
            ImGui::EndTable();
        }
    }

    bool keybindsOpen = ImGui::CollapsingHeader("Keybinds", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("KeybindsResetPopup")) {
        if (ImGui::MenuItem("Reset All Keybinds to Defaults")) {
            KeybindManager::instance().resetToDefaults();
            KeybindManager::instance().save();
        }
        ImGui::EndPopup();
    }

    if (keybindsOpen) {
        ImGui::TextDisabled("Tip: Right-click category headers (or Keybinds) to reset to defaults.");
        ImGui::Spacing();

        static KeyAction awaitingAction = KeyAction::None;

        auto KeyBindRow = [&](KeyAction action) {
            const auto& info = KeybindManager::instance().getInfo(action);
            ImGui::PushID((int)action);
            ImGui::TableNextRow();

            // 1. Action name
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", info.name.c_str());

            // 2. Scope
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", info.scope.c_str());

            // 3. Modifier dropdown
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int modIndex = KeybindManager::getModifierIndex(info.chord);
            if (ImGui::Combo("##mod", &modIndex, KeybindManager::getModifierComboString())) {
                int newModMask = KeybindManager::getModifierMaskFromIndex(modIndex);
                ImGuiKey baseKey = KeybindManager::getBaseKey(info.chord);
                ImGuiKeyChord newChord = KeybindManager::makeChord(newModMask, baseKey);
                KeybindManager::instance().setChord(action, newChord);
                KeybindManager::instance().save();
            }

            // 4. Single-key property box
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);

            ImGuiKey baseKey = KeybindManager::getBaseKey(info.chord);
            const char* keyName = (baseKey != ImGuiKey_None) ? ImGui::GetKeyName(baseKey) : "None";
            char buf[64];
            snprintf(buf, sizeof(buf), "%s##keybtn", keyName ? keyName : "None");

            if (awaitingAction == action) {
                ImGui::Button("Press a key...##keybtn", ImVec2(-FLT_MIN, 0));
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    int curModMask = KeybindManager::getModifierMaskFromIndex(KeybindManager::getModifierIndex(info.chord));
                    KeybindManager::instance().setChord(action, KeybindManager::makeChord(curModMask, ImGuiKey_None));
                    KeybindManager::instance().save();
                    awaitingAction = KeyAction::None;
                } else {
                    for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i++) {
                        if (KeybindManager::isModifierKey((ImGuiKey)i))
                            continue;

                        if (ImGui::IsKeyPressed((ImGuiKey)i)) {
                            int curModMask = KeybindManager::getModifierMaskFromIndex(KeybindManager::getModifierIndex(info.chord));
                            ImGuiKeyChord newChord = KeybindManager::makeChord(curModMask, (ImGuiKey)i);
                            KeybindManager::instance().setChord(action, newChord);
                            KeybindManager::instance().save();
                            awaitingAction = KeyAction::None;
                            break;
                        }
                    }
                }
                if (ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())
                    awaitingAction = KeyAction::None;
            } else if (ImGui::Button(buf, ImVec2(-FLT_MIN, 0))) {
                awaitingAction = action;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to set single key. Press Escape to clear (set to None).");
            }
            ImGui::PopID();
        };

        auto RenderCategoryTable = [&](const char* title, const std::vector<KeyAction>& actions) {
            bool nodeOpen = ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_DefaultOpen);
            std::string popupId = std::string("ResetPopup_") + title;
            if (ImGui::BeginPopupContextItem(popupId.c_str())) {
                std::string cleanName = title;
                size_t dotPos = cleanName.find(". ");
                if (dotPos != std::string::npos)
                    cleanName = cleanName.substr(dotPos + 2);
                std::string menuLabel = std::string("Reset ") + cleanName + " to Defaults";
                if (ImGui::MenuItem(menuLabel.c_str())) {
                    for (KeyAction act : actions) {
                        KeybindManager::instance().resetActionToDefault(act);
                    }
                    KeybindManager::instance().save();
                }
                ImGui::EndPopup();
            }

            if (nodeOpen) {
                std::string tableId = std::string("Table_") + title;
                if (ImGui::BeginTable(tableId.c_str(), 4, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Modifier", ImGuiTableColumnFlags_WidthFixed, 115.0f);
                    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (KeyAction act : actions) {
                        KeyBindRow(act);
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        };

        RenderCategoryTable("1. General & Project Actions", {KeyAction::Undo,
                                                             KeyAction::Redo,
                                                             KeyAction::SaveProject,
                                                             KeyAction::IncrementalSave,
                                                             KeyAction::ExportTrack,
                                                             KeyAction::SaveScreenshot,
                                                             KeyAction::MeasurementPointsWindow,
                                                             KeyAction::ResetView,
                                                             KeyAction::DeleteGlbMesh});

        RenderCategoryTable("2. Section & Transition Editing", {KeyAction::SelectPrevSection,
                                                                KeyAction::SelectNextSection,
                                                                KeyAction::DeleteSection,
                                                                KeyAction::DeleteTransition,
                                                                KeyAction::PrependTransition,
                                                                KeyAction::AppendTransition});

        RenderCategoryTable("3. Track Shading Modes", {KeyAction::ShaderNothing,
                                                       KeyAction::ShaderVelocity,
                                                       KeyAction::ShaderRollSpeed,
                                                       KeyAction::ShaderNormalForce,
                                                       KeyAction::ShaderLateralForce,
                                                       KeyAction::ShaderTrackFlexion});

        RenderCategoryTable("4. Camera Views & Viewport Navigation", {KeyAction::TogglePOVMode,
                                                                      KeyAction::MoveForward,
                                                                      KeyAction::MoveBackward,
                                                                      KeyAction::MoveLeft,
                                                                      KeyAction::MoveRight,
                                                                      KeyAction::POVHeightUp,
                                                                      KeyAction::POVHeightDown,
                                                                      KeyAction::POVHeightReset,
                                                                      KeyAction::ViewPerspective,
                                                                      KeyAction::ViewTopOrtho,
                                                                      KeyAction::ViewSideOrtho,
                                                                      KeyAction::ViewFrontOrtho});

        RenderCategoryTable("5. Viewport In-Scene Overlays", {KeyAction::OverlayScenery,
                                                              KeyAction::OverlayWarnings,
                                                              KeyAction::OverlaySceneryPrev,
                                                              KeyAction::OverlaySceneryNext,
                                                              KeyAction::OverlaySceneryToggleVis,
                                                              KeyAction::OverlaySceneryDelete});

        RenderCategoryTable("6. Graph View & Playhead", {KeyAction::PlayheadPlayPause,
                                                         KeyAction::PlayheadStepBackward,
                                                         KeyAction::PlayheadStepForward});
    }
    ImGui::End();
}

void Application::RenderTrainGeneratorWindow() {
    ImGui::SetNextWindowSize(ImVec2(350, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 175.0f, ImGui::GetMainViewport()->Size.y / 2.0f - 180.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Train Generator", &showTrainGenerator, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    if (activeTrackIdx < 0 || activeTrackIdx >= static_cast<int>(trackList.size())) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please select an active track first.");
        ImGui::End();
        return;
    }

    trackHandler* hTrack = trackList[activeTrackIdx];
    track* myTrack = hTrack->trackData;

    if (ImGui::CollapsingHeader("Train Model", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Scan trains for *.fvdtrain files recursively with cache (max once every 2 seconds)
        static std::vector<std::string> cachedTrainStyles;
        static double lastTrainScanTime = -10.0;
        double currentTime = ImGui::GetTime();
        if (cachedTrainStyles.empty() || currentTime - lastTrainScanTime > 2.0) {
            cachedTrainStyles = VFS::get().listFiles("trains", {".fvdtrain"});
            lastTrainScanTime = currentTime;
        }

        auto getTrainDisplayName = [](const std::string& fullPath) -> std::string {
            std::filesystem::path p(fullPath);
            std::string pathStr = p.lexically_normal().string();
            size_t pos = pathStr.find("trains");
            if (pos != std::string::npos) {
                std::string sub = pathStr.substr(pos + 6);
                if (!sub.empty() && (sub[0] == '/' || sub[0] == '\\')) {
                    sub = sub.substr(1);
                }
                return sub;
            }
            return p.filename().string();
        };

        std::string currentTrainLabel = "None";
        if (myTrack->train.isLoaded()) {
            currentTrainLabel = getTrainDisplayName(myTrack->train.getFilePath());
        }

        if (ImGui::BeginTable("TrainModelTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Model");
            ImGui::TableNextColumn();

            float buttonWidth = 70.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);

            if (ImGui::BeginCombo("##TrainModelDropdown", currentTrainLabel.c_str())) {
                if (ImGui::Selectable("None", !myTrack->train.isLoaded())) {
                    myTrack->train.clear();
                    myTrack->requestUpdateTrack(0, 0);
                }
                for (const auto& trainPath : cachedTrainStyles) {
                    std::string trainName = getTrainDisplayName(trainPath);
                    bool isSelected = (myTrack->train.getFilePath() == trainPath);
                    if (ImGui::Selectable(trainName.c_str(), isSelected)) {
                        myTrack->importTrainStyle(trainPath);
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("Browse...", ImVec2(buttonWidth, 0))) {
                auto selection = pfd::open_file("Select Train Configuration", ".", {"FVD Train Files", "*.fvdtrain"}).result();
                if (!selection.empty()) {
                    myTrack->importTrainStyle(selection[0]);
                }
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Middle Cars");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::SliderInt("##MiddleCars", &myTrack->train.middleCarCount, 0, 20)) {
                myTrack->train.setMiddleCarCount(myTrack->train.middleCarCount, myTrack);
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("POV Camera");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string povCamLabel = "Heartline (Default)";
            if (myTrack->train.boundCameraIndex >= 0 && myTrack->train.boundCameraIndex < (int)myTrack->train.getCameras().size()) {
                povCamLabel = myTrack->train.getCameras()[myTrack->train.boundCameraIndex].name;
            }
            if (ImGui::BeginCombo("##POVCameraDropdown", povCamLabel.c_str())) {
                if (ImGui::Selectable("Heartline (Default)", myTrack->train.boundCameraIndex == -1)) {
                    myTrack->train.boundCameraIndex = -1;
                }
                for (size_t i = 0; i < myTrack->train.getCameras().size(); ++i) {
                    ImGui::PushID((int)i);
                    bool isSelected = (myTrack->train.boundCameraIndex == (int)i);
                    if (ImGui::Selectable(myTrack->train.getCameras()[i].name, isSelected)) {
                        myTrack->train.boundCameraIndex = (int)i;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            ImGui::EndTable();
        }
    }

    static int arrCars = 5, arrRows = 2, arrSeats = 2;
    static glm::vec3 arrSpacing(0.9f, 0.0f, -0.9f);
    static float arrCarSpacing = 2.8f;

    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("TrainGenTable", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            auto propRow = [](const char* label, auto contentFunc) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", label);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                contentFunc();
            };

            propRow("Cars", [&]() { ImGui::DragInt("##Cars", &arrCars, 1, 1, 20); });
            propRow("Car Distance", [&]() {
                if (ImGui::DragFloat("##CarDist", &arrCarSpacing, 0.1f, 0.0f, 20.0f)) {
                    if (arrCarSpacing < 0.0f)
                        arrCarSpacing = 0.0f;
                }
            });
            propRow("Rows/Car", [&]() { ImGui::DragInt("##Rows", &arrRows, 1, 1, 10); });
            propRow("Seats/Row", [&]() { ImGui::DragInt("##Seats", &arrSeats, 1, 1, 10); });
            propRow("Spacing", [&]() { ImGui::DragFloat3("##Spacing", &arrSpacing.x, 0.1f); });

            ImGui::EndTable();
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Generate", ImVec2(-FLT_MIN, 0))) {
        float centerOffsetZ = ((arrCars - 1) * -arrCarSpacing + (arrRows - 1) * arrSpacing.z) / 2.0f;
        for (int c = 0; c < arrCars; ++c) {
            for (int r = 0; r < arrRows; ++r) {
                for (int s = 0; s < arrSeats; ++s) {
                    float hue = (float)(c * arrRows + r) / (float)(arrCars * arrRows);
                    ImVec4 rgb;
                    ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, rgb.x, rgb.y, rgb.z);
                    track::TrainOffset o;
                    snprintf(o.name, 64, "C%d R%d S%d", c + 1, r + 1, s + 1);
                    o.offset = glm::vec3((s - (arrSeats - 1) / 2.0f) * arrSpacing.x, r * arrSpacing.y, (c * -arrCarSpacing + r * arrSpacing.z) - centerOffsetZ);
                    o.color = glm::vec3(rgb.x, rgb.y, rgb.z);
                    myTrack->trainOffsets.push_back(o);
                }
            }
        }
        myTrack->hasChanged = true;
        myTrack->graphChanged = true;
        pushUndo();
        showTrainGenerator = false;
    }

    ImGui::End();
}

void Application::RenderMeasurementPointsWindow() {
    ImGui::SetNextWindowSize(ImVec2(550, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 275.0f, ImGui::GetMainViewport()->Size.y / 2.0f - 150.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Measurement Points", &showMeasurementPoints, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    if (activeTrackIdx < 0 || activeTrackIdx >= static_cast<int>(trackList.size())) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please select an active track first.");
        ImGui::End();
        return;
    }

    trackHandler* hTrack = trackList[activeTrackIdx];
    track* myTrack = hTrack->trackData;

    if (offsetSelections.size() != myTrack->trainOffsets.size()) {
        offsetSelections.resize(myTrack->trainOffsets.size(), false);
    }

    if (ImGui::BeginTable("MeasurementPointTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Sel.", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel, 40.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Norm.", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel, 45.0f);
        ImGui::TableSetupColumn("Lat.", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel, 45.0f);
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel, 45.0f);
        ImGui::TableHeadersRow();

        auto centerHeader = [&](int idx, const char* name) {
            ImGui::TableSetColumnIndex(idx);
            float tw = ImGui::CalcTextSize(name).x;
            float cw = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cw - tw) * 0.5f);
            ImGui::TableHeader(name);
        };
        centerHeader(0, "Sel.");
        centerHeader(3, "Norm.");
        centerHeader(4, "Lat.");
        centerHeader(5, "Color");

        for (size_t i = 0; i < myTrack->trainOffsets.size(); ++i) {
            ImGui::PushID((int)i);
            auto& o = myTrack->trainOffsets[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - 20.0f) * 0.5f);
            bool sel = offsetSelections[i];
            if (ImGui::Checkbox("##selPoint", &sel)) {
                offsetSelections[i] = sel;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##N", o.name, 64);

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##P", &o.offset.x, 0.1f)) {
                myTrack->hasChanged = true;
                myTrack->graphChanged = true;
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - 20.0f) * 0.5f);
            ImGui::Checkbox("##showN", &o.showNormal);

            ImGui::TableSetColumnIndex(4);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - 20.0f) * 0.5f);
            ImGui::Checkbox("##showL", &o.showLateral);

            ImGui::TableSetColumnIndex(5);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - 20.0f) * 0.5f);
            ImGui::ColorEdit3("##C", &o.color.x, ImGuiColorEditFlags_NoInputs);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    if (ImGui::Button("Add Point")) {
        track::TrainOffset o;
        myTrack->trainOffsets.push_back(o);
        myTrack->hasChanged = true;
        myTrack->graphChanged = true;
        pushUndo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Train Generator...")) {
        showTrainGenerator = !showTrainGenerator;
    }

    bool hasPoints = !myTrack->trainOffsets.empty();
    if (hasPoints) {
        ImGui::SameLine();
        if (ImGui::Button("Delete Selected")) {
            for (int i = (int)myTrack->trainOffsets.size() - 1; i >= 0; --i) {
                if (i < (int)offsetSelections.size() && offsetSelections[i]) {
                    myTrack->trainOffsets.erase(myTrack->trainOffsets.begin() + i);
                }
            }
            offsetSelections.clear();
            myTrack->hasChanged = true;
            myTrack->graphChanged = true;
            pushUndo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete All")) {
            myTrack->trainOffsets.clear();
            offsetSelections.clear();
            myTrack->hasChanged = true;
            myTrack->graphChanged = true;
            pushUndo();
        }
    }

    ImGui::End();
}

void Application::RenderParametricTrackEditorWindow() {
    ImGui::SetNextWindowSize(ImVec2(450, 450), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 225.0f, ImGui::GetMainViewport()->Size.y / 2.0f - 225.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Parametric Track Editor", &showParametricTrackEditor, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    if (activeTrackIdx < 0 || activeTrackIdx >= static_cast<int>(trackList.size())) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please select an active track first.");
        ImGui::End();
        return;
    }

    trackHandler* hTrack = trackList[activeTrackIdx];
    track* myTrack = hTrack->trackData;

    auto beginPropTable = [](const char* name) {
        return ImGui::BeginTable(name, 2, ImGuiTableFlags_SizingFixedFit);
    };
    auto propRow = [](const char* label, auto contentFunc) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        contentFunc();
    };

    if (ImGui::CollapsingHeader("Track Metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (beginPropTable("MetadataTable")) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            propRow("Author", [&]() {
                char buf[128];
                snprintf(buf, sizeof(buf), "%s", myTrack->trackAuthor.c_str());
                if (ImGui::InputText("##Author", buf, sizeof(buf))) {
                    myTrack->trackAuthor = buf;
                }
            });

            propRow("Manufacturer", [&]() {
                const char* curMfg = myTrack->trackManufacturer.empty() ? "Unset" : myTrack->trackManufacturer.c_str();
                if (ImGui::BeginCombo("##Manufacturer", curMfg)) {
                    for (const char* mfg : kTrackManufacturers) {
                        bool isSel = (myTrack->trackManufacturer == mfg);
                        if (ImGui::Selectable(mfg, isSel)) {
                            myTrack->trackManufacturer = mfg;
                        }
                    }
                    ImGui::EndCombo();
                }
            });

            propRow("Type", [&]() {
                char buf[128];
                snprintf(buf, sizeof(buf), "%s", myTrack->trackCoasterType.c_str());
                if (ImGui::InputText("##Type", buf, sizeof(buf))) {
                    myTrack->trackCoasterType = buf;
                }
            });

            ImGui::EndTable();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Parametric Extrusions", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Add Extrusion##AddExt")) {
            myTrack->customExtrusions.push_back({});
            myTrack->requestUpdateTrack(0, 0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All##ClearExt")) {
            myTrack->customExtrusions.clear();
            myTrack->requestUpdateTrack(0, 0);
        }

        for (int i = 0; i < (int)myTrack->customExtrusions.size(); ++i) {
            ImGui::PushID(i);
            auto& ext = myTrack->customExtrusions[i];
            if (beginPropTable("ExtrusionTable")) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                int shapeIdx = (int)ext.shape;
                const char* shapes[] = {"Cylindrical", "Box"};
                propRow("Shape", [&]() {
                    if (ImGui::Combo("##Shape", &shapeIdx, shapes, 2)) {
                        ext.shape = (track::ExtrusionShape)shapeIdx;
                        myTrack->requestUpdateTrack(0, 0);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The geometric cross-section of the extrusion.");
                });

                propRow("Size (L1/L2)", [&]() {
                    if (ImGui::DragFloat2("##Size", &ext.size.x, 0.005f, 0.01f, 5.0f))
                        myTrack->requestUpdateTrack(0, 0);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The dimensions of the extrusion cross-section.");
                });

                propRow("Offset (X/Y)", [&]() {
                    if (ImGui::DragFloat2("##Offset", &ext.offset.x, 0.005f, -10.0f, 10.0f))
                        myTrack->requestUpdateTrack(0, 0);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The lateral (X) and vertical (Y) displacement. Note: Offset is relative to the centre of the rails. The entire track assembly automatically inverts if heartline is negative.");
                });

                ImGui::EndTable();
            }

            bool removed = false;
            if (ImGui::Button("Remove Extrusion")) {
                myTrack->customExtrusions.erase(myTrack->customExtrusions.begin() + i);
                myTrack->requestUpdateTrack(0, 0);
                removed = true;
            }
            if (!removed && i < (int)myTrack->customExtrusions.size() - 1) {
                ImGui::Separator();
            }
            ImGui::PopID();
            if (removed)
                break;
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Custom Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Add Asset##AddAsset")) {
            myTrack->customAssets.push_back({});
        }

        for (int i = 0; i < (int)myTrack->customAssets.size(); ++i) {
            ImGui::PushID(i);
            auto& asset = myTrack->customAssets[i];
            if (beginPropTable("AssetTable")) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                propRow("File", [&]() {
                    if (ImGui::Button(asset.filepath.empty() ? "Browse..." : asset.filepath.c_str())) {
                        auto selection = pfd::open_file("Select Asset", ".", {"glTF Files", "*.gltf *.glb"}).result();
                        if (!selection.empty()) {
                            asset.filepath = selection[0];
                            if (asset.loadedModel) {
                                delete asset.loadedModel;
                                asset.loadedModel = nullptr;
                            }
                            myTrack->requestUpdateTrack(0, 0);
                        }
                    }
                });

                float totalLength = myTrack->getNumPoints() > 0 ? myTrack->getPoint(myTrack->getNumPoints())->fTotalLength : 1000.0f;
                float uiEndDist = asset.endDist < 0.0f ? totalLength : asset.endDist;

                propRow("Full Layout", [&]() {
                    if (ImGui::Checkbox("##FullLayout", &asset.fullLayout)) {
                        if (asset.fullLayout) {
                            asset.startDist = 0.0f;
                            asset.endDist = -1.0f;
                            asset.toEnd = true;
                        } else {
                            asset.endDist = totalLength;
                        }
                        myTrack->requestUpdateTrack(0, 0);
                    }
                });

                if (!asset.fullLayout) {
                    propRow("To End", [&]() {
                        if (ImGui::Checkbox("##ToEnd", &asset.toEnd)) {
                            asset.endDist = asset.toEnd ? -1.0f : totalLength;
                            myTrack->requestUpdateTrack(0, 0);
                        }
                    });

                    propRow("Start Dist", [&]() {
                        if (ImGui::DragFloat("##Start", &asset.startDist, 0.1f, 0.0f, totalLength))
                            myTrack->requestUpdateTrack(0, 0);
                    });

                    if (!asset.toEnd) {
                        propRow("End Dist", [&]() {
                            if (ImGui::DragFloat("##End", &uiEndDist, 0.1f, asset.startDist, totalLength)) {
                                asset.endDist = uiEndDist;
                                myTrack->requestUpdateTrack(0, 0);
                            }
                        });
                    }
                }

                propRow("Interval", [&]() {
                    if (ImGui::DragFloat("##Interval", &asset.interval, 0.1f, 0.01f, 100.0f))
                        myTrack->requestUpdateTrack(0, 0);
                });

                propRow("Color", [&]() {
                    if (ImGui::ColorEdit3("##Color", &asset.color.x))
                        myTrack->requestUpdateTrack(0, 0);
                });

                propRow("Shade Smooth", [&]() {
                    if (ImGui::Checkbox("##ShadeSmooth", &asset.smoothAlongSpline)) {
                        myTrack->requestUpdateTrack(0, 0);
                        myTrack->processPendingUpdates();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("When enabled, shading follows the track's spline smoothly.");
                    }
                });

                propRow("Visible", [&]() {
                    if (ImGui::Checkbox("##Visible", &asset.visible))
                        myTrack->requestUpdateTrack(0, 0);
                });

                ImGui::EndTable();
            }

            bool removed = false;
            if (ImGui::Button("Remove Asset")) {
                if (asset.loadedModel) {
                    delete asset.loadedModel;
                    asset.loadedModel = nullptr;
                }
                myTrack->customAssets.erase(myTrack->customAssets.begin() + i);
                myTrack->requestUpdateTrack(0, 0);
                removed = true;
            }
            if (!removed && i < (int)myTrack->customAssets.size() - 1) {
                ImGui::Separator();
            }
            ImGui::PopID();
            if (removed)
                break;
        }
    }

    ImGui::End();
}

void Application::RenderEnvironmentWindow() {
    ImGui::SetNextWindowSize(ImVec2(350, 650), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x / 2.0f - 175.0f,
                                   ImGui::GetMainViewport()->Size.y / 2.0f - 325.0f),
                            ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Environment", &showEnvironmentWindow, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    leftPanel.renderEnvironmentTab();
    ImGui::End();
}

static bool isValidDoubleFormatSpecifier(const std::string& fmt) {
    if (fmt.empty() || fmt[0] != '%')
        return false;
    for (size_t i = 1; i < fmt.length(); ++i) {
        char c = fmt[i];
        if (i == fmt.length() - 1) {
            return c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G';
        }
        if (c != '.' && c != '+' && c != '-' && (c < '0' || c > '9'))
            return false;
    }
    return false;
}

void Application::PerformExport(const std::string& path) {
    LOG_INFO("Exporting track to: %s (Format: %d)", path.c_str(), exportFormat);
    if (activeTrackIdx < 0 || activeTrackIdx >= (int)trackList.size())
        return;
    track* curTrack = trackList[activeTrackIdx]->trackData;
    int numSections = curTrack->lSections.size();

    int toSec = (exportToSection == -1 || exportToSection >= numSections) ? numSections - 1 : exportToSection;
    int fromSec = std::clamp(exportFromSection, 0, toSec);

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

            std::string fmt = exportNumFormat;
            if (!isValidDoubleFormatSpecifier(fmt)) {
                fmt = "%.6f";
            }
            double fHeartVal = exportHeartline ? 0.0 : curTrack->fHeart;
            curTrack->exportNL2TrackCSV(fout, exportDistPerNode, fromSec, toSec, fHeartVal, fmt.c_str());
            fclose(fout);
        }
    }

    lastExportPath = path;
}

void Application::Shutdown() {
    gloParent->mOptions->save("options.cfg");
    KeybindManager::instance().save("keybinds.cfg");
    LOG_INFO("FVD++ shutting down...");

    vulkanContext.waitIdle();
    for (auto track : trackList)
        delete track;
    trackList.clear();

    viewport.shutdown();
    destroyWelcomeSvgTexture();

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
            base += '_';
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
            std::string filename = currentFilePath;
            size_t lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                filename = filename.substr(lastSlash + 1);
            }
            showInAppNotification("Project Saved: " + filename);
            addRecentFile(currentFilePath);
        }
    } else {
        std::string newPath = incrementFilename(currentFilePath);
        currentFilePath = newPath;
        saver saveObj(currentFilePath, trackList);
        saveObj.doSave();
        LOG_INFO("Incrementally saved project: %s", currentFilePath.c_str());
        std::string filename = currentFilePath;
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            filename = filename.substr(lastSlash + 1);
        }
        showInAppNotification("Project Saved (Incremental): " + filename);
        addRecentFile(currentFilePath);
    }
}

void Application::forkTrack(trackHandler* sourceTrack, int nodeIdx) {
    if (!sourceTrack || nodeIdx < 0 || nodeIdx >= sourceTrack->trackData->getNumPoints()) {
        return;
    }

    track* origTrack = sourceTrack->trackData;
    origTrack->processPendingUpdates();

    mnode* forkNode = origTrack->getPoint(nodeIdx);
    if (!forkNode) {
        return;
    }

    // Create the new track handler
    std::string newName = origTrack->name + " (Fork)";
    trackHandler* newTrackHandler = new trackHandler(newName, static_cast<int>(trackList.size()) + 1);
    track* newTrack = newTrackHandler->trackData;

    // Calculate world-space position and orientation at the fork node
    glm::dmat4 origAnchorBase = glm::translate(glm::dmat4(1.0), origTrack->startPos) *
                                glm::rotate(glm::dmat4(1.0), (double)TO_RAD(origTrack->startYaw - 90.0), glm::dvec3(0.0, 1.0, 0.0));
    glm::dvec3 forkWorldPos = glm::dvec3(origAnchorBase * glm::dvec4(forkNode->vPos, 1.0));

    glm::dmat4 origRot = glm::rotate(glm::dmat4(1.0), (double)TO_RAD(origTrack->startYaw - 90.0), glm::dvec3(0.0, 1.0, 0.0));
    glm::dvec3 forkWorldDir = glm::dvec3(origRot * glm::dvec4(forkNode->vDir, 0.0));
    glm::dvec3 forkWorldLat = glm::dvec3(origRot * glm::dvec4(forkNode->vLat, 0.0));
    glm::dvec3 forkWorldNorm = glm::dvec3(origRot * glm::dvec4(forkNode->vNorm, 0.0));

    double newYaw = (glm::atan(-forkWorldDir.x, -forkWorldDir.z) * 180.0 / F_PI) + 90.0;
    while (newYaw > 180.0)
        newYaw -= 360.0;
    while (newYaw < -180.0)
        newYaw += 360.0;
    double newPitch = glm::atan(forkWorldDir.y, glm::sqrt(forkWorldDir.x * forkWorldDir.x + forkWorldDir.z * forkWorldDir.z)) * 180.0 / F_PI;

    // Set the properties of the new track's anchor
    newTrack->startPos = forkWorldPos;
    newTrack->startYaw = newYaw;
    newTrack->startPitch = newPitch;

    // Project world-space vectors of the fork point back into the new track's starting frame
    glm::dmat4 invRot = glm::rotate(glm::dmat4(1.0), (double)TO_RAD(-(newYaw - 90.0)), glm::dvec3(0.0, 1.0, 0.0));
    glm::dvec3 localDir = glm::dvec3(invRot * glm::dvec4(forkWorldDir, 0.0));
    glm::dvec3 localLat = glm::dvec3(invRot * glm::dvec4(forkWorldLat, 0.0));
    glm::dvec3 localNorm = glm::dvec3(invRot * glm::dvec4(forkWorldNorm, 0.0));

    // Calculate correct local roll relative to the new starting system
    double calculatedRoll = glm::atan(localLat.y, -localNorm.y) * 180.0 / F_PI;

    // Inherit physical properties to ensure smooth transitions
    mnode* anchor = newTrack->anchorNode;
    anchor->vPos = glm::dvec3(0.0, 0.0, 0.0);
    anchor->vDir = glm::normalize(localDir);
    anchor->vLat = glm::normalize(localLat);
    anchor->updateNorm();

    anchor->forceLateral = forkNode->forceLateral;
    anchor->forceNormal = forkNode->forceNormal;
    anchor->fPitchFromLast = forkNode->fPitchFromLast;
    anchor->fRoll = calculatedRoll;
    anchor->fRollSpeed = forkNode->fRollSpeed;
    anchor->fSmoothSpeed = forkNode->fSmoothSpeed;
    anchor->fVel = forkNode->fVel;
    anchor->fYawFromLast = forkNode->fYawFromLast;
    anchor->fEnergy = 0.5 * forkNode->fVel * forkNode->fVel + F_G * anchor->fPosHearty(0.9 * origTrack->fHeart);

    // Copy global track parameters
    newTrack->fHeart = origTrack->fHeart;
    newTrack->fFriction = origTrack->fFriction;
    newTrack->fResistance = origTrack->fResistance;
    newTrack->enableForceLimits = origTrack->enableForceLimits;
    newTrack->fMaxPosNormal = origTrack->fMaxPosNormal;
    newTrack->fMaxNegNormal = origTrack->fMaxNegNormal;
    newTrack->fMaxLateral = origTrack->fMaxLateral;
    newTrack->style = origTrack->style;
    newTrack->customStyleFile = origTrack->customStyleFile;
    newTrack->customExtrusions = origTrack->customExtrusions;
    newTrack->customAssets = origTrack->customAssets;
    newTrack->trainOffsets = origTrack->trainOffsets;

    // Force update of the new empty track (anchors only)
    newTrack->updateTrack(0, 0);

    newTrack->activeSection = nullptr;

    // Rebuild the mesh for the new track
    if (newTrackHandler->mMesh) {
        newTrackHandler->mMesh->buildMeshes(0);
    }

    // Add the new track to our list and activate it
    trackList.push_back(newTrackHandler);
    activeTrackIdx = static_cast<int>(trackList.size()) - 1;

    // Mark viewport scene dirty and reset POV to start
    if (gViewport) {
        gViewport->markSceneDirty();
        gViewport->setPOVPos(0);
    }

    // Push snapshot to undo history
    pushUndo();
}

void Application::splitTrack(trackHandler* sourceTrack, int sectionIdx) {
    if (!sourceTrack)
        return;

    track* origTrack = sourceTrack->trackData;
    if (!origTrack)
        return;

    if (sectionIdx <= 0 || sectionIdx >= static_cast<int>(origTrack->lSections.size())) {
        return;
    }

    origTrack->processPendingUpdates();

    // The split point is between sectionIdx - 1 and sectionIdx.
    // The start state of the new track is the end node of sectionIdx - 1.
    section* prevSection = origTrack->lSections[sectionIdx - 1];
    if (prevSection->lNodes.empty())
        return;
    mnode* splitNode = &prevSection->lNodes.back();

    // Create the new track handler
    std::string newName = origTrack->name + " (Part 2)";
    trackHandler* newTrackHandler = new trackHandler(newName, static_cast<int>(trackList.size()) + 1);
    track* newTrack = newTrackHandler->trackData;

    // Calculate world-space position and orientation at the split node
    glm::dmat4 origAnchorBase = glm::translate(glm::dmat4(1.0), origTrack->startPos) *
                                glm::rotate(glm::dmat4(1.0), (double)TO_RAD(origTrack->startYaw - 90.0), glm::dvec3(0.0, 1.0, 0.0));
    glm::dvec3 splitWorldPos = glm::dvec3(origAnchorBase * glm::dvec4(splitNode->vPos, 1.0));

    glm::dmat4 origRot = glm::rotate(glm::dmat4(1.0), (double)TO_RAD(origTrack->startYaw - 90.0), glm::dvec3(0.0, 1.0, 0.0));
    glm::dvec3 splitWorldDir = glm::dvec3(origRot * glm::dvec4(splitNode->vDir, 0.0));
    glm::dvec3 splitWorldLat = glm::dvec3(origRot * glm::dvec4(splitNode->vLat, 0.0));
    glm::dvec3 splitWorldNorm = glm::dvec3(origRot * glm::dvec4(splitNode->vNorm, 0.0));

    double newYaw = (glm::atan(-splitWorldDir.x, -splitWorldDir.z) * 180.0 / F_PI) + 90.0;
    while (newYaw > 180.0)
        newYaw -= 360.0;
    while (newYaw < -180.0)
        newYaw += 360.0;
    double newPitch = glm::atan(splitWorldDir.y, glm::sqrt(splitWorldDir.x * splitWorldDir.x + splitWorldDir.z * splitWorldDir.z)) * 180.0 / F_PI;

    // Set the properties of the new track's anchor
    newTrack->startPos = splitWorldPos;
    newTrack->startYaw = newYaw;
    newTrack->startPitch = newPitch;

    // Project world-space vectors of the split point back into the new track's starting frame
    glm::dmat4 invRot = glm::rotate(glm::dmat4(1.0), (double)TO_RAD(-(newYaw - 90.0)), glm::dvec3(0.0, 1.0, 0.0));
    glm::dvec3 localDir = glm::dvec3(invRot * glm::dvec4(splitWorldDir, 0.0));
    glm::dvec3 localLat = glm::dvec3(invRot * glm::dvec4(splitWorldLat, 0.0));
    glm::dvec3 localNorm = glm::dvec3(invRot * glm::dvec4(splitWorldNorm, 0.0));

    // Calculate correct local roll relative to the new starting system
    double calculatedRoll = glm::atan(localLat.y, -localNorm.y) * 180.0 / F_PI;

    // Inherit physical properties to ensure smooth transitions
    mnode* anchor = newTrack->anchorNode;
    anchor->vPos = glm::dvec3(0.0, 0.0, 0.0);
    anchor->vDir = glm::normalize(localDir);
    anchor->vLat = glm::normalize(localLat);
    anchor->updateNorm();

    anchor->forceLateral = splitNode->forceLateral;
    anchor->forceNormal = splitNode->forceNormal;
    anchor->fPitchFromLast = splitNode->fPitchFromLast;
    anchor->fRoll = calculatedRoll;
    anchor->fRollSpeed = splitNode->fRollSpeed;
    anchor->fSmoothSpeed = splitNode->fSmoothSpeed;
    anchor->fVel = splitNode->fVel;
    anchor->fYawFromLast = splitNode->fYawFromLast;
    anchor->fEnergy = 0.5 * splitNode->fVel * splitNode->fVel + F_G * anchor->fPosHearty(0.9 * origTrack->fHeart);

    // Copy global track parameters
    newTrack->fHeart = origTrack->fHeart;
    newTrack->fFriction = origTrack->fFriction;
    newTrack->fResistance = origTrack->fResistance;
    newTrack->enableForceLimits = origTrack->enableForceLimits;
    newTrack->fMaxPosNormal = origTrack->fMaxPosNormal;
    newTrack->fMaxNegNormal = origTrack->fMaxNegNormal;
    newTrack->fMaxLateral = origTrack->fMaxLateral;
    newTrack->style = origTrack->style;
    newTrack->customStyleFile = origTrack->customStyleFile;
    newTrack->customExtrusions = origTrack->customExtrusions;
    newTrack->customAssets = origTrack->customAssets;
    newTrack->trainOffsets = origTrack->trainOffsets;

    // Move sections starting from sectionIdx to newTrack
    size_t countToMove = origTrack->lSections.size() - sectionIdx;
    for (size_t i = sectionIdx; i < origTrack->lSections.size(); ++i) {
        section* sec = origTrack->lSections[i];
        sec->parent = newTrack;
        newTrack->lSections.push_back(sec);
        newTrack->smoothList.push_back(new smoothHandler(newTrack, static_cast<int>(newTrack->lSections.size()) - 1));
    }

    // Clean up smoothList on origTrack
    for (size_t i = 0; i < countToMove; ++i) {
        delete origTrack->smoothList.back();
        origTrack->smoothList.pop_back();
    }

    // Erase sections from origTrack (do not delete the sections!)
    origTrack->lSections.erase(origTrack->lSections.begin() + sectionIdx, origTrack->lSections.end());

    // Update activeSection pointers
    origTrack->activeSection = origTrack->lSections.empty() ? nullptr : origTrack->lSections.back();
    newTrack->activeSection = newTrack->lSections.empty() ? nullptr : newTrack->lSections.front();

    // Update both tracks
    origTrack->updateTrack(0, 0);
    if (sourceTrack->mMesh) {
        sourceTrack->mMesh->buildMeshes(0);
    }

    newTrack->updateTrack(0, 0);
    if (newTrackHandler->mMesh) {
        newTrackHandler->mMesh->buildMeshes(0);
    }

    // Add new track to list and activate it
    trackList.push_back(newTrackHandler);
    activeTrackIdx = static_cast<int>(trackList.size()) - 1;
    leftPanel.selectedSectionIdx = 0;
    gloParent->selectedFunc = nullptr;

    if (gViewport) {
        gViewport->markSceneDirty();
        gViewport->setPOVPos(0);
    }

    pushUndo();
    showInAppNotification("Track split into: " + newName);
}

void Application::mergeTracks(trackHandler* trackA, trackHandler* trackB) {
    if (!trackA || !trackB || trackA == trackB)
        return;

    track* tA = trackA->trackData;
    track* tB = trackB->trackData;
    if (!tA || !tB)
        return;

    if (tA->isReferenceTrack() || tB->isReferenceTrack())
        return;

    tA->processPendingUpdates();
    tB->processPendingUpdates();

    // If Track A has no sections, Track A inherits Track B's anchor completely
    if (tA->lSections.empty()) {
        tA->startPos = tB->startPos;
        tA->startYaw = tB->startYaw;
        tA->startPitch = tB->startPitch;
        *tA->anchorNode = *tB->anchorNode;
    }

    // Move all sections from Track B to Track A
    for (size_t i = 0; i < tB->lSections.size(); ++i) {
        section* sec = tB->lSections[i];
        sec->parent = tA;
        tA->lSections.push_back(sec);
        tA->smoothList.push_back(new smoothHandler(tA, static_cast<int>(tA->lSections.size()) - 1));
    }

    // Clear Track B's section pointers so they aren't deleted on track destruction
    tB->lSections.clear();

    std::string mergedName = tB->name;

    // Find trackB in trackList
    auto it = std::find(trackList.begin(), trackList.end(), trackB);
    if (it != trackList.end()) {
        delete trackB;
        trackList.erase(it);

        // Adjust activeTrackIdx
        auto itA = std::find(trackList.begin(), trackList.end(), trackA);
        if (itA != trackList.end()) {
            activeTrackIdx = static_cast<int>(std::distance(trackList.begin(), itA));
        } else {
            activeTrackIdx = trackList.empty() ? -1 : 0;
        }
    }

    // Set active section on trackA
    tA->activeSection = tA->lSections.empty() ? nullptr : tA->lSections.back();
    leftPanel.selectedSectionIdx = tA->lSections.empty() ? -1 : static_cast<int>(tA->lSections.size()) - 1;
    gloParent->selectedFunc = nullptr;

    // Recalculate track A
    tA->updateTrack(0, 0);
    if (trackA->mMesh) {
        trackA->mMesh->buildMeshes(0);
    }

    if (gViewport) {
        gViewport->markSceneDirty();
        gViewport->setPOVPos(0);
    }

    pushUndo();
    showInAppNotification("Merged '" + mergedName + "' into: " + tA->name);
}

void Application::importReferenceTrack(const std::string& path) {
    // 1. Get file name from path
    std::string newName = path;
    size_t lastSlash = newName.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        newName = newName.substr(lastSlash + 1);
    }
    // strip extension
    size_t lastDot = newName.find_last_of('.');
    if (lastDot != std::string::npos) {
        newName = newName.substr(0, lastDot);
    }
    newName = "Ref: " + newName;

    // 2. Create the new track handler with isReference = true (uses lightweight reftrack class)
    trackHandler* newTrackHandler = new trackHandler(newName, static_cast<int>(trackList.size()) + 1, true);
    track* newTrack = newTrackHandler->trackData;

    // 3. Clear any existing sections and create a single secnlcsv section
    for (auto sec : newTrack->lSections) {
        delete sec;
    }
    newTrack->lSections.clear();

    secnlcsv* csvSec = new secnlcsv(newTrack, newTrack->anchorNode);
    newTrack->lSections.push_back(csvSec);
    newTrack->activeSection = csvSec;

    // 4. Load the CSV track
    csvSec->loadTrack(path);

    // 5. Rebuild meshes and add to trackList
    if (newTrackHandler->mMesh) {
        newTrackHandler->mMesh->buildMeshes(0);
    }

    trackList.push_back(newTrackHandler);
    activeTrackIdx = static_cast<int>(trackList.size()) - 1;

    pushUndo();
    showInAppNotification("Imported Reference Track: " + newName);
}

void Application::loadRecentFiles() {
    recentFiles.clear();
    std::ifstream in("recent_files.txt");
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                std::string normLine;
                try {
                    normLine = std::filesystem::path(line).lexically_normal().string();
                } catch (...) {
                    normLine = line;
                }
                // Only add if it's not already in the list
                if (std::find(recentFiles.begin(), recentFiles.end(), normLine) == recentFiles.end()) {
                    recentFiles.push_back(normLine);
                }
            }
        }
    }
}

void Application::saveRecentFiles() {
    std::ofstream out("recent_files.txt");
    if (out) {
        for (const auto& path : recentFiles) {
            out << path << "\n";
        }
    }
}

void Application::addRecentFile(const std::string& path) {
    if (path.empty())
        return;
    std::string normPath;
    try {
        normPath = std::filesystem::path(path).lexically_normal().string();
    } catch (...) {
        normPath = path;
    }
    recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), normPath), recentFiles.end());
    recentFiles.insert(recentFiles.begin(), normPath);
    if (recentFiles.size() > 10) {
        recentFiles.resize(10);
    }
    saveRecentFiles();
}

void Application::clearRecentFiles() {
    recentFiles.clear();
    saveRecentFiles();
}

void Application::loadProjectFile(const std::string& path) {
    saver loadObj(path, trackList);
    loadObj.doLoad();
    activeTrackIdx = trackList.empty() ? -1 : 0;
    gloParent->selectedFunc = nullptr;
    currentFilePath = path;
    showWelcomeScreen = false;
    LOG_INFO("Loaded project: %s", currentFilePath.c_str());

    viewport.setGroundTextureSize(gloParent->projectGrdTexSize);
    viewport.setGroundHeight(gloParent->projectGrdHeight);
    viewport.loadGroundTexture(gloParent->projectGroundTex);
    viewport.applyProjectEnvironment();
    while (!viewport.glbMeshes.empty())
        viewport.removeGlbMesh(0);
    std::vector<DummyGlobal::GlbSettings> validGlbs;
    for (const auto& glb : gloParent->projectGlbs) {
        if (viewport.addGlbMesh(glb.path)) {
            viewport.glbMeshes.back().visible = glb.visible;
            validGlbs.push_back(glb);
        }
    }
    gloParent->projectGlbs = validGlbs;
    for (auto* th : trackList) {
        if (th && th->trackData) {
            th->trackData->requestUpdateTrack(0, 0);
            th->trackData->processPendingUpdates();
        }
    }
    if (activeTrackIdx >= 0 && activeTrackIdx < (int)trackList.size()) {
        track* curTrack = trackList[activeTrackIdx]->trackData;
        if (curTrack->activeSection && curTrack->activeSection->rollFunc && !curTrack->activeSection->rollFunc->funcList.empty()) {
            gloParent->selectedFunc = curTrack->activeSection->rollFunc->funcList.front();
        }
    }
    viewport.markSceneDirty();
    if (mUndoHandler) {
        mUndoHandler->clearActions();
        mUndoHandler->pushSnapshot();
    }
    addRecentFile(path);
}

void Application::showInAppNotification(const std::string& msg) {
    notificationMessage = msg;
    notificationTimer = 3.0f;
}

void Application::CreateNewProject() {
    for (auto t : trackList)
        delete t;
    trackList.clear();
    activeTrackIdx = -1;
    gloParent->selectedFunc = nullptr;
    currentFilePath = "";
    gloParent->resetEnvironment();
    viewport.setGroundTextureSize(gloParent->projectGrdTexSize);
    viewport.setGroundHeight(gloParent->projectGrdHeight);
    viewport.loadGroundTexture(gloParent->projectGroundTex);
    viewport.applyProjectEnvironment();
    while (!viewport.glbMeshes.empty())
        viewport.removeGlbMesh(0);

    // Auto-create initial Track 1 with straight section
    trackHandler* initialTrack = new trackHandler("Track 1", 1);
    trackList.push_back(initialTrack);
    activeTrackIdx = 0;
    track* myTrack = initialTrack->trackData;
    myTrack->newSection(straight, -1);
    if (!myTrack->lSections.empty()) {
        myTrack->activeSection = myTrack->lSections.front();
        if (myTrack->activeSection->rollFunc && !myTrack->activeSection->rollFunc->funcList.empty()) {
            gloParent->selectedFunc = myTrack->activeSection->rollFunc->funcList.front();
        }
    }
    myTrack->requestUpdateTrack(0, 0);
    myTrack->processPendingUpdates();
    viewport.markSceneDirty();

    if (mUndoHandler) {
        mUndoHandler->clearActions();
        mUndoHandler->pushSnapshot();
    }

    showWelcomeScreen = false;
}

void Application::initWelcomeSvgTexture() {
    const AssetData* asset = getEmbeddedAsset("resources/icons/fvd_path_welcome.svg");
    if (!asset || !asset->data || asset->size == 0) {
        asset = getEmbeddedAsset("resources/icons/fvd_path.svg");
    }
    if (!asset || !asset->data || asset->size == 0) {
        asset = getEmbeddedAsset("resources/icons/fvd.svg");
    }
    std::string svgString;
    if (asset && asset->data && asset->size > 0) {
        svgString.assign(reinterpret_cast<const char*>(asset->data), asset->size);
    } else {
        std::ifstream in("resources/icons/fvd_path_welcome.svg");
        if (!in) {
            in.open("resources/icons/fvd_path.svg");
        }
        if (!in) {
            in.open("resources/icons/fvd.svg");
        }
        if (in) {
            std::stringstream buffer;
            buffer << in.rdbuf();
            svgString = buffer.str();
        }
    }

    if (svgString.empty()) {
        LOG_WARN("initWelcomeSvgTexture: fvd_path_welcome.svg not found");
        return;
    }

    std::vector<char> svgCopy(svgString.begin(), svgString.end());
    svgCopy.push_back('\0');

    NSVGimage* svgImg = nsvgParse(svgCopy.data(), "px", 96.0f);
    if (!svgImg || svgImg->width <= 0.0f || svgImg->height <= 0.0f) {
        LOG_WARN("initWelcomeSvgTexture: failed to parse SVG");
        if (svgImg)
            nsvgDelete(svgImg);
        return;
    }

    const int texWidth = 1024;
    const int texHeight = 1024;
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(svgImg);
        return;
    }

    std::vector<uint8_t> pixels(texWidth * texHeight * 4, 0);
    float scale = (float)texWidth / svgImg->width;
    nsvgRasterize(rast, svgImg, 0, 0, scale, pixels.data(), texWidth, texHeight, texWidth * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(svgImg);

    welcomeSvgTexture.create2d(vulkanContext, texWidth, texHeight, pixels.data(), false);
    welcomeSvgDescriptorSet = ImGui_ImplVulkan_AddTexture(
        welcomeSvgTexture.sampler, welcomeSvgTexture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Application::destroyWelcomeSvgTexture() {
    if (welcomeSvgDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(welcomeSvgDescriptorSet);
        welcomeSvgDescriptorSet = VK_NULL_HANDLE;
    }
    welcomeSvgTexture.destroy();
}

static void openUrl(const std::string& url) {
#if defined(_WIN32)
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open " + url;
    [[maybe_unused]] int result = std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open " + url;
    [[maybe_unused]] int result = std::system(cmd.c_str());
#endif
}

void Application::RenderWelcomeScreen() {
    if (!showWelcomeScreen)
        return;

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();

    // 1. Full-screen backdrop using active theme WindowBg dimmed
    ImGui::SetNextWindowPos(mainViewport->Pos);
    ImGui::SetNextWindowSize(mainViewport->Size);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImVec4 bgCol = style.Colors[ImGuiCol_WindowBg];
    bgCol.x *= 0.65f;
    bgCol.y *= 0.65f;
    bgCol.z *= 0.65f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgCol);
    ImGui::Begin("##WelcomeBackdrop", nullptr, bgFlags);

    if (welcomeSvgDescriptorSet != VK_NULL_HANDLE) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float logoSize = mainViewport->Size.y * 0.75f;
        ImVec2 center = mainViewport->GetCenter();
        ImVec2 pMin(center.x - logoSize * 0.5f, center.y - logoSize * 0.5f);
        ImVec2 pMax(center.x + logoSize * 0.5f, center.y + logoSize * 0.5f);

        ImU32 watermarkCol = IM_COL32(255, 255, 255, 45);
        drawList->AddImage((ImTextureID)welcomeSvgDescriptorSet, pMin, pMax,
                           ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), watermarkCol);
    }

    ImGui::End();
    ImGui::PopStyleColor();

    // 2. Compact centered card (width: 390px)
    float cardWidth = std::min(390.0f, mainViewport->Size.x - 20.0f);
    float cardHeight = std::min(275.0f, mainViewport->Size.y - 20.0f);
    ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(cardWidth, cardHeight), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.Colors[ImGuiCol_PopupBg]);
    ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_Border]);

    ImGuiWindowFlags cardFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##WelcomeCard", nullptr, cardFlags)) {
        // --- Header with Pill Badges ---
        // Theme-compliant pill badges (using FrameBg, slightly larger)
        ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_FrameBg]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_FrameBg]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_FrameBg]);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f, 3.5f));

        std::string verStr = FVD_VERSION;
        if (verStr.empty() || (verStr[0] != 'v' && verStr[0] != 'V')) {
            verStr = "v" + verStr;
        }
        ImGui::Button(verStr.c_str());

        if (gVulkanContext && !gVulkanContext->getGpuName().empty()) {
            ImGui::SameLine();
            std::string gpu = gVulkanContext->getGpuName();
            if (gpu.rfind("NVIDIA ", 0) == 0) {
                gpu = gpu.substr(7);
            }
            if (gpu.length() > 14) {
                gpu = gpu.substr(0, 12) + "..";
            }
            ImGui::Button(gpu.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", gVulkanContext->getGpuName().c_str());
            }
        }

        if (updateAvailable) {
            ImGui::SameLine();
            ImGui::PopStyleColor(3);
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_HeaderActive]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_HeaderHovered]);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_HeaderActive]);
            if (ImGui::Button("Update")) {
                openUrl("https://github.com/H27CK/openFVeiD/releases/latest");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Update available: %s", remoteVersion.c_str());
            }
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Body Split Section ---
        if (ImGui::BeginTable("WelcomeLayoutTable", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("LeftPane", ImGuiTableColumnFlags_WidthFixed, 115.0f);
            ImGui::TableSetupColumn("RightPane", ImGuiTableColumnFlags_WidthStretch);

            // === Left Column: Actions ===
            ImGui::TableNextColumn();
            {
                const float btnHeight = 25.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

                if (ImGui::Button("New Project", ImVec2(-1, btnHeight))) {
                    CreateNewProject();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Start a new design with an initial Track 1 straight section");
                }

                ImGui::Spacing();

                if (ImGui::Button("Open Project...", ImVec2(-1, btnHeight))) {
                    auto f = pfd::open_file("Choose project file", ".", {"FVD++ Projects", "*.fvd", "All Files", "*"}).result();
                    if (!f.empty()) {
                        loadProjectFile(f[0]);
                        showWelcomeScreen = false;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Open an existing .fvd coaster project from disk");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Options", ImVec2(-1, btnHeight))) {
                    showOptions = true;
                }

                ImGui::Spacing();

                if (ImGui::Button("GitHub", ImVec2(-1, btnHeight))) {
                    openUrl("https://github.com/H27CK/openFVeiD");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("https://github.com/H27CK/openFVeiD");
                }

                ImGui::Spacing();

                if (ImGui::Button("Exit", ImVec2(-1, btnHeight))) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Exit application");
                }

                ImGui::PopStyleVar();
            }

            // === Right Column: Recent Projects ===
            ImGui::TableNextColumn();
            {
                ImGui::TextDisabled("RECENT PROJECTS");
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_FrameBg]);
                ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_Border]);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

                if (ImGui::BeginChild("WelcomeRecentList", ImVec2(0, 0), true)) {
                    if (recentFiles.empty()) {
                        float availH = ImGui::GetContentRegionAvail().y;
                        ImGui::Dummy(ImVec2(0, availH * 0.35f));
                        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("No recent projects.").x) * 0.5f);
                        ImGui::TextDisabled("No recent projects.");
                    } else {
                        ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
                        ImU32 colTextMuted = ImGui::GetColorU32(ImGuiCol_TextDisabled);

                        for (size_t i = 0; i < recentFiles.size(); ++i) {
                            const auto& path = recentFiles[i];
                            std::string filename = path;
                            std::string dir = "";
                            size_t lastSlash = filename.find_last_of("/\\");
                            if (lastSlash != std::string::npos) {
                                dir = filename.substr(0, lastSlash);
                                filename = filename.substr(lastSlash + 1);
                            }
                            if (dir.length() > 24) {
                                dir = "..." + dir.substr(dir.length() - 21);
                            }

                            ImGui::PushID((int)i);
                            if (ImGui::Selectable("##RecentCard", false, 0, ImVec2(0, 32.0f))) {
                                loadProjectFile(path);
                                showWelcomeScreen = false;
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", path.c_str());
                            }

                            ImVec2 cardMin = ImGui::GetItemRectMin();
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            drawList->AddText(ImVec2(cardMin.x + 6.0f, cardMin.y + 2.0f), colText, filename.c_str());
                            drawList->AddText(ImVec2(cardMin.x + 6.0f, cardMin.y + 16.0f), colTextMuted, dir.c_str());

                            ImGui::PopID();
                        }
                    }
                }
                ImGui::EndChild();

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

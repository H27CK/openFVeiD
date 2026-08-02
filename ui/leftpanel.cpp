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

#include "leftpanel.h"
#include "graphview.h"
#include "renderer/viewport.h"
#include "trackhandler.h"
#include "trackmesh.h"
#include "track.h"
#include "customstyle.h"
#include "section.h"
#include "secstraight.h"
#include "seccurved.h"
#include "mnode.h"
#include "core/application.h"
#include "core/globalundohandler.h"
#include "dummies.h"
#include "portable-file-dialogs.h"
#include "common.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <glm/gtx/rotate_vector.hpp>

#define BEGIN_PROP_TABLE(id)                                                                        \
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) { \
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);                 \
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

#define PROP_ROW(label, ...)           \
    ImGui::TableNextRow();             \
    ImGui::TableNextColumn();          \
    ImGui::AlignTextToFramePadding();  \
    ImGui::Text("%s", label);          \
    ImGui::TableNextColumn();          \
    ImGui::SetNextItemWidth(-FLT_MIN); \
    __VA_ARGS__

#define END_PROP_TABLE() \
    ImGui::EndTable();   \
    }

static void syncAnchorDerivsToForces(mnode* anchor) {
    double temp = cos(fabs(anchor->getPitch()) * F_PI / 180.0);
    double forceAngle = sqrt(temp * temp * anchor->fYawFromLast * anchor->fYawFromLast + anchor->fPitchFromLast * anchor->fPitchFromLast); // deltaAngle;
    double dirFromLast = glm::atan(anchor->fYawFromLast, anchor->fPitchFromLast) - TO_RAD(anchor->fRoll);

    glm::dvec3 forceVec;
    if (fabs(forceAngle) < std::numeric_limits<double>::epsilon()) {
        forceVec = glm::dvec3(0.0, 1.0, 0.0);
    } else {
        forceVec = glm::dvec3(0.0, 1.0, 0.0) + ((anchor->fVel * anchor->fVel) / (9.80665 * anchor->fVel / forceAngle * 0.18 / F_PI)) * glm::normalize(glm::dvec3(glm::rotate(glm::dmat4(1.0), (double)dirFromLast, -anchor->vDir) * glm::dvec4(-anchor->vNorm, 0.0)));
    }
    anchor->forceNormal = -glm::dot(forceVec, glm::normalize(anchor->vNorm));
    anchor->forceLateral = -glm::dot(forceVec, glm::normalize(anchor->vLat));
}

static void syncAnchorForcesToDerivs(mnode* anchor) {
    glm::dvec3 forceVec = glm::dvec3(0.0, 1.0, 0.0) + anchor->forceNormal * anchor->vNorm + anchor->forceLateral * anchor->vLat;

    glm::dvec3 pitchVec = cos(anchor->fRoll * F_PI / 180.0) * anchor->vNorm - sin(anchor->fRoll * F_PI / 180.0) * anchor->vLat;
    glm::dvec3 yawVec = sin(anchor->fRoll * F_PI / 180.0) * anchor->vNorm + cos(anchor->fRoll * F_PI / 180.0) * anchor->vLat;

    anchor->fPitchFromLast = glm::dot(forceVec, pitchVec) / anchor->fVel * 1.8 / F_PI;
    anchor->fYawFromLast = glm::dot(forceVec, yawVec) / anchor->fVel * 1.8 / F_PI;
}

static std::string getStyleDisplayName(const std::string& fullPath) {
    std::filesystem::path p(fullPath);
    std::string pathStr = p.lexically_normal().string();
    size_t pos = pathStr.find("track_styles");
    if (pos != std::string::npos) {
        std::string sub = pathStr.substr(pos + 12); // "track_styles" has length 12
        if (!sub.empty() && (sub[0] == '/' || sub[0] == '\\')) {
            sub = sub.substr(1);
        }
        return sub;
    }
    return p.filename().string();
}

void LeftPanel::syncAnchorNode(trackHandler* hTrack) {
    track* myTrack = hTrack->trackData;
    mnode* anchor = myTrack->anchorNode;

    // Check if the track has a custom anchor (like a fork)
    if (anchor->vPos != glm::dvec3(0.0, 0.0, 0.0)) {
        // Keep the custom position, direction, and lateral vectors
        // and only update energy/normals based on physical properties
        anchor->updateNorm();
        anchor->fEnergy = 0.5 * anchor->fVel * anchor->fVel + F_G * anchor->fPosHearty(0.9 * myTrack->fHeart);
        return;
    }

    anchor->vPos = glm::dvec3(0.0, 0.0, 0.0);
    double pitchRad = TO_RAD(myTrack->startPitch);
    anchor->vDir = glm::normalize(glm::dvec3(0.0, sin(pitchRad), -cos(pitchRad)));
    if (anchor->vDir.y == 1.0) {
        anchor->vLat = glm::dvec3(glm::angleAxis(TO_RAD(myTrack->anchorNode->fRoll), glm::dvec3(0.0, -1.0, 0.0)) * glm::dvec4(1.0, 0.0, 0.0, 0.0));
    } else {
        anchor->vLat = glm::dvec3(-anchor->vDir.z, 0.0, anchor->vDir.x);
    }
    anchor->vLat.y = tan(myTrack->anchorNode->fRoll * F_PI / 180.0) * sqrt(anchor->vLat.x * anchor->vLat.x + anchor->vLat.z * anchor->vLat.z);
    anchor->vLat = glm::normalize(anchor->vLat);
    anchor->updateNorm();
    anchor->fEnergy = 0.5 * anchor->fVel * anchor->fVel + F_G * anchor->fPosHearty(0.9 * myTrack->fHeart);
}

LeftPanel::LeftPanel()
    : activeTab(0), selectedSectionIdx(-1), selectedSmoothingIdx(0) {}
LeftPanel::~LeftPanel() {}

void LeftPanel::render(Application* app) {
    std::vector<trackHandler*>& trackList = app->trackList;
    int& activeTrackIdx = app->activeTrackIdx;
    bool hasActiveTrack = activeTrackIdx >= 0 && activeTrackIdx < static_cast<int>(trackList.size());

    // 1. Tracks Window
    if (ImGui::Begin("Tracks", nullptr, 0)) {
        renderProjectTab(app);
    }
    ImGui::End();

    // 2. Sections Window
    if (ImGui::Begin("Sections", nullptr, 0)) {
        if (!hasActiveTrack) {
            ImGui::TextDisabled("Please select or create a track first.");
        } else if (trackList[activeTrackIdx]->trackData->isReferenceTrack()) {
            ImGui::TextDisabled("Reference Track (Editing Disabled)");
        } else {
            renderTrackTab(trackList[activeTrackIdx], app);
        }
    }
    ImGui::End();

    /*
    // 3. Smoothing Window
    if (ImGui::Begin("Smoothing", nullptr, 0)) {
        if (!hasActiveTrack) {
            ImGui::TextDisabled("Please select or create a track first.");
        } else {
            ImGui::BeginDisabled();
            renderTrackSmoothing(trackList[activeTrackIdx], app);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Smoothing not yet implemented");
        }
    }
    ImGui::End();
    */

    // 4. Colors Window
    if (ImGui::Begin("Colors", nullptr, 0)) {
        if (!hasActiveTrack) {
            ImGui::TextDisabled("Please select or create a track first.");
        } else {
            renderColorsTab(trackList[activeTrackIdx], app);
        }
    }
    ImGui::End();

    if (ImGui::GetCurrentContext()->ActiveIdPreviousFrame != 0 && ImGui::GetCurrentContext()->ActiveId == 0) {
        app->pushUndo();
    }
}

void LeftPanel::renderProjectTab(Application* app) {
    std::vector<trackHandler*>& trackList = app->trackList;
    int& activeTrackIdx = app->activeTrackIdx;
    if (ImGui::BeginTable("TrackList", 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(trackList.size()); i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool isSelected = (activeTrackIdx == i);
            bool isHidden = (trackList[i]->trackData->drawHeartline == 3);

            if (isHidden) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }

            if (ImGui::Selectable((trackList[i]->trackData->name + "##" + std::to_string(i)).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                activeTrackIdx = i;
                selectedSectionIdx = -1;
                if (gViewport) {
                    gViewport->markSceneDirty();
                    if (gloParent->mOptions->autoFocusOnSelection)
                        gViewport->focusOnSection(-1);
                }
            }

            if (isHidden) {
                ImGui::PopStyleColor();
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                trackList[i]->trackData->drawHeartline = isHidden ? 0 : 3;
                if (gViewport) {
                    gViewport->markSceneDirty();
                }
            }
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Add")) {
        trackList.push_back(new trackHandler("New Coaster", static_cast<int>(trackList.size()) + 1));
        activeTrackIdx = static_cast<int>(trackList.size()) - 1;
        if (gViewport)
            gViewport->markSceneDirty();
    }
    ImGui::SameLine();
    bool hasSelection = (activeTrackIdx >= 0 && activeTrackIdx < static_cast<int>(trackList.size()));
    if (!hasSelection)
        ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        delete trackList[activeTrackIdx];
        trackList.erase(trackList.begin() + activeTrackIdx);
        activeTrackIdx = trackList.empty() ? -1 : 0;
        selectedSectionIdx = -1;
        gloParent->selectedFunc = nullptr;
        if (gViewport)
            gViewport->markSceneDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit"))
        ImGui::SetWindowFocus("Sections");
    if (!hasSelection)
        ImGui::EndDisabled();

    // Recalculate selection status after potential modifications
    hasSelection = (activeTrackIdx >= 0 && activeTrackIdx < static_cast<int>(trackList.size()));

    if (hasSelection) {
        ImGui::Separator();
        ImGui::BeginChild("ProjectTrackProperties", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        renderTrackProperties(trackList[activeTrackIdx], app);
        ImGui::EndChild();
    } else {
        ImGui::Text("No track selected.");
    }
}

void LeftPanel::renderTrackProperties(trackHandler* hTrack, Application* app) {
    track* myTrack = hTrack->trackData;
    bool isRef = myTrack->isReferenceTrack();
    if (isRef) {
        ImGui::BeginDisabled();
    }

    if (ImGui::TreeNodeEx("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("TrackPropsGeneral")
        char nameBuf[256];
        strncpy(nameBuf, myTrack->name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        PROP_ROW("Name", if (ImGui::InputText("##TrackName", nameBuf, sizeof(nameBuf))) myTrack->name = nameBuf;)
        const char* drawModes[] = {"Everything", "Track only", "Heartline only", "Nothing"};
        PROP_ROW(
            "Draw", if (ImGui::Combo("##Draw", &myTrack->drawHeartline, drawModes, IM_ARRAYSIZE(drawModes))) { if (gViewport) gViewport->markSceneDirty(); })
        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("TrackPropsPhysics")
        float friction = (float)myTrack->fFriction;
        PROP_ROW(
            "Friction Param", if (ImGui::DragFloat("##Friction", &friction, 0.001f, 0.0f, 1.0f, "%.4f m/m") || common::ValueScroll(&friction, 0.01f, 0.001f)) { myTrack->fFriction = std::max(0.0f, friction); myTrack->requestUpdateTrack(0, 0); })
        float resistance = (float)myTrack->fResistance * 1e5f;
        PROP_ROW(
            "Air Res.", if (ImGui::DragFloat("##Resistance", &resistance, 0.01f, 0.0f, 10.0f, "%.2f e-005") || common::ValueScroll(&resistance, 1.0f, 0.1f)) { myTrack->fResistance = (double)std::max(0.0f, resistance) / 1e5f; myTrack->requestUpdateTrack(0, 0); })
        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Track Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Scan track_styles for *.fvdstyle files recursively with cache (max once every 2 seconds)
        static std::vector<std::string> cachedStyles;
        static double lastScanTime = -10.0;
        double currentTime = ImGui::GetTime();
        if (cachedStyles.empty() || currentTime - lastScanTime > 2.0) {
            cachedStyles.clear();
            try {
                if (std::filesystem::exists("track_styles")) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator("track_styles")) {
                        if (entry.is_regular_file() && entry.path().extension() == ".fvdstyle") {
                            cachedStyles.push_back(entry.path().string());
                        }
                    }
                }
            } catch (...) {
            }
            std::sort(cachedStyles.begin(), cachedStyles.end());
            lastScanTime = currentTime;
        }

        // Deduce style file if empty (e.g. after loading project)
        if (myTrack->customStyleFile.empty() && !myTrack->customAssets.empty()) {
            for (const auto& asset : myTrack->customAssets) {
                if (!asset.filepath.empty()) {
                    std::filesystem::path assetPath(asset.filepath);
                    std::filesystem::path parentDir = assetPath.parent_path();
                    if (std::filesystem::exists(parentDir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(parentDir)) {
                            if (entry.is_regular_file() && entry.path().extension() == ".fvdstyle") {
                                myTrack->customStyleFile = entry.path().string();
                                break;
                            }
                        }
                    }
                }
                if (!myTrack->customStyleFile.empty())
                    break;
            }
        }

        BEGIN_PROP_TABLE("TrackPropsSettings")
        std::string lenFmt = "%.2f " + gloParent->mOptions->getLengthString();
        float heart = (float)myTrack->fHeart * gloParent->mOptions->getLengthFactor();
        PROP_ROW(
            "Heartline", if (ImGui::DragFloat("##Heartline", &heart, 0.01f, -10.0f, 10.0f, lenFmt.c_str()) || common::ValueScroll(&heart, 1.0f, 0.1f)) { myTrack->fHeart = (double)std::clamp(heart, -10.0f * (float)gloParent->mOptions->getLengthFactor(), 10.0f * (float)gloParent->mOptions->getLengthFactor()) / gloParent->mOptions->getLengthFactor(); myTrack->requestUpdateTrack(0, 0); })

        std::string currentStyleLabel = "None / Custom";
        if (!myTrack->customStyleFile.empty()) {
            currentStyleLabel = getStyleDisplayName(myTrack->customStyleFile);
        }

        if (isRef) {
            ImGui::EndDisabled();
        }

        PROP_ROW(
            "Style",
            if (ImGui::BeginCombo("##StyleDropdown", currentStyleLabel.c_str())) {
                if (ImGui::Selectable("None / Custom", myTrack->customStyleFile.empty())) {
                    if (hTrack->mMesh) {
                        hTrack->mMesh->clearParametricStyles();
                    }
                    myTrack->customStyleFile = "";
                    myTrack->customExtrusions.clear();
                    for (auto& asset : myTrack->customAssets) {
                        if (asset.loadedModel)
                            delete asset.loadedModel;
                    }
                    myTrack->customAssets.clear();
                    myTrack->requestUpdateTrack(0, 0);
                }
                for (const auto& stylePath : cachedStyles) {
                    std::string styleName = getStyleDisplayName(stylePath);
                    bool isSelected = (myTrack->customStyleFile == stylePath);
                    if (ImGui::Selectable(styleName.c_str(), isSelected)) {
                        if (hTrack->mMesh) {
                            hTrack->mMesh->clearParametricStyles();
                        }
                        myTrack->importParametricStyle(stylePath);
                    }
                }
                ImGui::EndCombo();
            })

        if (isRef) {
            ImGui::BeginDisabled();
        }

        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Limits: Force", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("SafetyLimitsProps")

        bool enableLimits = myTrack->enableForceLimits;
        PROP_ROW(
            "Enforce Limits",
            if (ImGui::Checkbox("##EnforceLimits", &enableLimits)) {
                myTrack->enableForceLimits = enableLimits;
                myTrack->requestUpdateTrack(0, 0);
            })

        if (enableLimits) {
            float maxPosNorm = (float)myTrack->fMaxPosNormal;
            PROP_ROW(
                "Norm: Max",
                if (ImGui::DragFloat("##MaxPosNormal", &maxPosNorm, 0.05f, 0.1f, 20.0f, "%.2f G") || common::ValueScroll(&maxPosNorm, 0.5f, 0.1f)) {
                    myTrack->fMaxPosNormal = std::clamp((double)maxPosNorm, 0.1, 20.0);
                    myTrack->requestUpdateTrack(0, 0);
                })

            float maxNegNorm = (float)myTrack->fMaxNegNormal;
            PROP_ROW(
                "Norm: Min",
                if (ImGui::DragFloat("##MaxNegNormal", &maxNegNorm, 0.05f, -10.0f, -0.1f, "%.2f G") || common::ValueScroll(&maxNegNorm, 0.5f, 0.1f)) {
                    myTrack->fMaxNegNormal = std::clamp((double)maxNegNorm, -10.0, -0.1);
                    myTrack->requestUpdateTrack(0, 0);
                })

            float maxLat = (float)myTrack->fMaxLateral;
            PROP_ROW(
                "Lat: Max",
                if (ImGui::DragFloat("##MaxLateral", &maxLat, 0.05f, 0.1f, 15.0f, "%.2f G") || common::ValueScroll(&maxLat, 0.5f, 0.1f)) {
                    myTrack->fMaxLateral = std::clamp((double)maxLat, 0.1, 15.0);
                    myTrack->requestUpdateTrack(0, 0);
                })

            float minLat = (float)myTrack->fMinLateral;
            PROP_ROW(
                "Lat: Min",
                if (ImGui::DragFloat("##MinLateral", &minLat, 0.05f, -15.0f, -0.1f, "%.2f G") || common::ValueScroll(&minLat, 0.5f, 0.1f)) {
                    myTrack->fMinLateral = std::clamp((double)minLat, -15.0, -0.1);
                    myTrack->requestUpdateTrack(0, 0);
                })
        }
        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Limits: Radius", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("RadiusLimitsProps")

        bool enforceRad = myTrack->enforceMinRadius;
        PROP_ROW(
            "Min Radius Limit",
            if (ImGui::Checkbox("##EnforceRad", &enforceRad)) {
                myTrack->enforceMinRadius = enforceRad;
                myTrack->requestUpdateTrack(0, 0);
            })

        if (enforceRad) {
            float minRad = (float)myTrack->minRadius;
            PROP_ROW(
                "Radius: Min",
                if (ImGui::DragFloat("##MinRadius", &minRad, 0.1f, 0.1f, 500.0f, "%.1f m") || common::ValueScroll(&minRad, 1.0f, 0.1f)) {
                    myTrack->minRadius = std::max(0.1, (double)minRad);
                    myTrack->requestUpdateTrack(0, 0);
                })
        }
        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (isRef) {
        ImGui::EndDisabled();
    }
}

void LeftPanel::renderTrackSmoothing(trackHandler* track, Application* app) {
    ImGui::Text("Track smoothing implementation goes here.");
}

void LeftPanel::renderTrackTab(trackHandler* hTrack, Application* app) {
    track* myTrack = hTrack->trackData;
    if (selectedSectionIdx >= static_cast<int>(myTrack->lSections.size())) {
        selectedSectionIdx = static_cast<int>(myTrack->lSections.size()) - 1;
    }
    if (ImGui::BeginTable("SectionList", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool anchorSelected = (selectedSectionIdx == -1);
        if (ImGui::Selectable("Anchor", anchorSelected, ImGuiSelectableFlags_SpanAllColumns)) {
            selectedSectionIdx = -1;
            myTrack->activeSection = nullptr;
            gloParent->selectedFunc = nullptr;
            myTrack->requestUpdateTrack(0, 0);
            if (gViewport && gloParent->mOptions->autoFocusOnSelection)
                gViewport->focusOnSection(-1);
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("Anchor");
        for (int i = 0; i < static_cast<int>(myTrack->lSections.size()); i++) {
            bool secStalled = myTrack->lSections[i]->isStalled;
            if (secStalled) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // Highlight stalled sections
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool isSelected = (selectedSectionIdx == i);
            std::string displayName = std::to_string(i) + ". " + myTrack->lSections[i]->sName;
            if (ImGui::Selectable((displayName + "##" + std::to_string(i)).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedSectionIdx = i;
                myTrack->activeSection = myTrack->lSections[i];
                gloParent->selectedFunc = nullptr;
                myTrack->requestUpdateTrack(0, 0);
                if (gViewport && gloParent->mOptions->autoFocusOnSelection)
                    gViewport->focusOnSection(i);
            }
            ImGui::TableSetColumnIndex(1);
            const char* typeStr = "Unknown";
            switch (myTrack->lSections[i]->type) {
            case straight:
                typeStr = "Straight";
                break;
            case curved:
                typeStr = "Curved";
                break;
            case forced:
                typeStr = "Force";
                break;
            case geometric:
                typeStr = "Geometric";
                break;
            case geometricriderlocal:
                typeStr = "Geometric Rider-Local";
                break;
            case static_spline:
                typeStr = "Static Spline";
                break;
            default:
                break;
            }
            ImGui::Text("%s", typeStr);
            if (secStalled) {
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndTable();
    }

    bool styleIsLocked = false;
    if (styleIsLocked)
        ImGui::BeginDisabled();

    // 1. "Add" Button
    if (ImGui::Button("Add"))
        ImGui::OpenPopup("AddSectionPopup");
    if (ImGui::BeginPopup("AddSectionPopup")) {
        bool added = false;
        int insertIdx = (selectedSectionIdx == -1) ? -1 : (selectedSectionIdx + 1);
        if (ImGui::MenuItem("Straight")) {
            myTrack->newSection(straight, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Curved")) {
            myTrack->newSection(curved, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Force")) {
            myTrack->newSection(forced, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Geometric")) {
            myTrack->newSection(geometric, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Geometric Rider-Local")) {
            myTrack->newSection(geometricriderlocal, insertIdx);
            added = true;
        }

        if (ImGui::BeginMenu("Templates")) {
            static std::vector<std::pair<std::string, std::string>> cachedTemplates;
            static double lastScanTime = -10.0;
            double currentTime = ImGui::GetTime();
            if (cachedTemplates.empty() || currentTime - lastScanTime > 2.0) {
                cachedTemplates.clear();
                try {
                    if (std::filesystem::exists("templates")) {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator("templates")) {
                            if (entry.is_regular_file() && entry.path().extension() == ".fvdtpl") {
                                cachedTemplates.push_back({entry.path().stem().string(), entry.path().string()});
                            }
                        }
                    }
                } catch (...) {
                }
                std::sort(cachedTemplates.begin(), cachedTemplates.end());
                lastScanTime = currentTime;
            }

            if (cachedTemplates.empty()) {
                ImGui::TextDisabled("No templates found");
            } else {
                for (const auto& tpl : cachedTemplates) {
                    if (ImGui::MenuItem(tpl.first.c_str())) {
                        if (myTrack->loadTemplate(tpl.second, insertIdx)) {
                            if (insertIdx == -1)
                                selectedSectionIdx = static_cast<int>(myTrack->lSections.size()) - 1;
                            else
                                selectedSectionIdx = insertIdx;
                            myTrack->activeSection = myTrack->lSections[selectedSectionIdx];
                            gloParent->selectedFunc = nullptr;
                            app->pushUndo();
                            added = true;
                        }
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (added) {
            if (insertIdx == -1)
                selectedSectionIdx = static_cast<int>(myTrack->lSections.size()) - 1;
            else
                selectedSectionIdx = insertIdx;
            myTrack->activeSection = myTrack->lSections[selectedSectionIdx];
            gloParent->selectedFunc = nullptr;
            myTrack->requestUpdateTrack(0, 0);
            app->pushUndo();
            if (gViewport && gloParent->mOptions->autoFocusOnSelection)
                gViewport->focusOnSection(selectedSectionIdx);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // 2. "Delete" Button
    bool canDelete = (selectedSectionIdx >= 0) && !styleIsLocked;
    if (!canDelete)
        ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        myTrack->removeSection(selectedSectionIdx);
        selectedSectionIdx--;
        if (selectedSectionIdx < -1)
            selectedSectionIdx = -1;
        if (selectedSectionIdx >= 0)
            myTrack->activeSection = myTrack->lSections.at(selectedSectionIdx);
        else
            myTrack->activeSection = nullptr;
        myTrack->requestUpdateTrack(0, 0);
        gloParent->selectedFunc = nullptr;
        if (gViewport && gloParent->mOptions->autoFocusOnSelection)
            gViewport->focusOnSection(selectedSectionIdx);
        app->pushUndo();
    }
    if (!canDelete)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // 3. "Prepend" Button
    if (ImGui::Button("Prepend"))
        ImGui::OpenPopup("PrependSectionPopup");
    if (ImGui::BeginPopup("PrependSectionPopup")) {
        bool added = false;
        int insertIdx = (selectedSectionIdx == -1) ? 0 : selectedSectionIdx;
        if (ImGui::MenuItem("Straight")) {
            myTrack->newSection(straight, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Curved")) {
            myTrack->newSection(curved, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Force")) {
            myTrack->newSection(forced, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Geometric")) {
            myTrack->newSection(geometric, insertIdx);
            added = true;
        }
        if (ImGui::MenuItem("Geometric Rider-Local")) {
            myTrack->newSection(geometricriderlocal, insertIdx);
            added = true;
        }

        if (ImGui::BeginMenu("Templates")) {
            static std::vector<std::pair<std::string, std::string>> cachedTemplates;
            static double lastScanTime = -10.0;
            double currentTime = ImGui::GetTime();
            if (cachedTemplates.empty() || currentTime - lastScanTime > 2.0) {
                cachedTemplates.clear();
                try {
                    if (std::filesystem::exists("templates")) {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator("templates")) {
                            if (entry.is_regular_file() && entry.path().extension() == ".fvdtpl") {
                                cachedTemplates.push_back({entry.path().stem().string(), entry.path().string()});
                            }
                        }
                    }
                } catch (...) {
                }
                std::sort(cachedTemplates.begin(), cachedTemplates.end());
                lastScanTime = currentTime;
            }

            if (cachedTemplates.empty()) {
                ImGui::TextDisabled("No templates found");
            } else {
                for (const auto& tpl : cachedTemplates) {
                    if (ImGui::MenuItem(tpl.first.c_str())) {
                        if (myTrack->loadTemplate(tpl.second, insertIdx)) {
                            if (insertIdx == -1)
                                selectedSectionIdx = static_cast<int>(myTrack->lSections.size()) - 1;
                            else
                                selectedSectionIdx = insertIdx;
                            myTrack->activeSection = myTrack->lSections[selectedSectionIdx];
                            gloParent->selectedFunc = nullptr;
                            app->pushUndo();
                            added = true;
                        }
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (added) {
            selectedSectionIdx = insertIdx;
            myTrack->activeSection = myTrack->lSections[selectedSectionIdx];
            gloParent->selectedFunc = nullptr;
            myTrack->requestUpdateTrack(0, 0);
            app->pushUndo();
            if (gViewport && gloParent->mOptions->autoFocusOnSelection)
                gViewport->focusOnSection(selectedSectionIdx);
        }
        ImGui::EndPopup();
    }

    if (styleIsLocked)
        ImGui::EndDisabled();

    if (selectedSectionIdx >= -1 && selectedSectionIdx < static_cast<int>(myTrack->lSections.size())) {
        ImGui::Separator();
        if (styleIsLocked) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Track editing is disabled while a Custom 3D Style is active.");
            ImGui::BeginDisabled();
        }
        if (selectedSectionIdx == -1)
            renderSectionProperties(hTrack, nullptr, app);
        else
            renderSectionProperties(hTrack, myTrack->lSections[selectedSectionIdx], app);
        if (styleIsLocked)
            ImGui::EndDisabled();
    }
}

void LeftPanel::renderSectionProperties(trackHandler* hTrack, section* sec, Application* app) {
    ImGui::BeginChild("SectionPropsScroll");
    if (sec) {
        BEGIN_PROP_TABLE("SectionBaseProps")
        char nameBuf[256];
        strncpy(nameBuf, sec->sName.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        PROP_ROW("Name", if (ImGui::InputText("##SectionName", nameBuf, sizeof(nameBuf))) sec->sName = nameBuf;)

        if (sec->type != anchor) {
            float timeVal = sec->lNodes.empty() ? 0.0f : static_cast<float>(sec->lNodes.size() - 1) / F_HZ;
            PROP_ROW("Time", ImGui::Text("%.3f s", timeVal);)
            PROP_ROW("Length", ImGui::Text("%.2f %s", sec->length * gloParent->mOptions->getLengthFactor(), gloParent->mOptions->getLengthString().c_str());)
            int orientation = sec->bOrientation ? 1 : 0;
            const char* orientations[] = {"Quaternion", "Euler"};
            bool disableOrientation = (sec->type == straight || sec->type == geometricriderlocal || sec->type == static_spline);
            if (disableOrientation) {
                sec->bOrientation = false;
                orientation = 0;
                ImGui::BeginDisabled();
            }
            PROP_ROW(
                "Orientation", if (ImGui::Combo("##Orientation", &orientation, orientations, IM_ARRAYSIZE(orientations))) { sec->bOrientation = orientation == 1; hTrack->trackData->requestUpdateTrack(sec, 0); })
            if (disableOrientation)
                ImGui::EndDisabled();
            int argument = sec->bArgument ? 1 : 0;
            const char* arguments[] = {"Time", "Distance"};
            bool disableArgument = (sec->type == straight || sec->type == curved || sec->type == static_spline);
            if (disableArgument) {
                sec->bArgument = 0;
                argument = 0;
                ImGui::BeginDisabled();
            }
            PROP_ROW(
                "Function w.r.t", if (ImGui::Combo("##Function", &argument, arguments, IM_ARRAYSIZE(arguments))) { sec->bArgument = argument == 1; hTrack->trackData->requestUpdateTrack(sec, 0); })
            if (disableArgument)
                ImGui::EndDisabled();
        }
        END_PROP_TABLE()
        ImGui::Separator();

        switch (sec->type) {
        case anchor:
            renderAnchorProperties(hTrack, sec, app);
            break;
        case straight:
            renderStraightProperties(hTrack, sec, app);
            break;
        case curved:
            renderCurvedProperties(hTrack, sec, app);
            break;
        case forced:
        case geometric:
        case geometricriderlocal:
            renderForcedProperties(hTrack, sec, app);
            break;
        case static_spline:
            renderStaticProperties(hTrack, sec, app);
            break;
        default:
            break;
        }
    } else {
        ImGui::Text("Section: Anchor");
        ImGui::Separator();
        renderAnchorProperties(hTrack, nullptr, app);
    }

    if (sec && sec->type != anchor) {
        ImGui::Separator();
        static bool initTemplateIndices = true;
        if (ImGui::Button("Save Section as Template...")) {
            ImGui::OpenPopup("SaveTemplatePopup");
            initTemplateIndices = true;
        }

        if (ImGui::BeginPopupModal("SaveTemplatePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char templateName[128] = "";
            static int startNode = 0;
            static int endNode = 0;

            track* myTrack = hTrack->trackData;
            int totalNodes = myTrack->getNumPoints();

            auto getSectionStartNode = [](track* t, int secIdx) {
                if (!t || secIdx < 0 || secIdx >= (int)t->lSections.size())
                    return 0;
                int currentPoints = 0;
                for (int i = 0; i < secIdx; ++i) {
                    currentPoints += (int)t->lSections[i]->lNodes.size() - 1;
                }
                return currentPoints;
            };

            auto getSectionEndNode = [](track* t, int secIdx) {
                if (!t || secIdx < 0 || secIdx >= (int)t->lSections.size())
                    return 0;
                int currentPoints = 0;
                for (int i = 0; i < secIdx; ++i) {
                    currentPoints += (int)t->lSections[i]->lNodes.size() - 1;
                }
                currentPoints += (int)t->lSections[secIdx]->lNodes.size() - 1;
                return currentPoints;
            };

            int selectedSectionIdx = myTrack->getSectionNumber(sec);

            if (initTemplateIndices) {
                extern Viewport* gViewport;
                int playheadPos = (gViewport) ? gViewport->getPOVPos() : getSectionStartNode(myTrack, selectedSectionIdx);
                startNode = std::clamp(playheadPos, 0, std::max(0, totalNodes - 1));
                int defaultEndNode = getSectionEndNode(myTrack, selectedSectionIdx);
                endNode = std::clamp(defaultEndNode, startNode, std::max(0, totalNodes - 1));
                templateName[0] = '\0';
                initTemplateIndices = false;
            }

            ImGui::Text("Save Precise Node Range as Template");
            ImGui::Separator();

            ImGui::InputText("Template Name", templateName, IM_ARRAYSIZE(templateName));

            ImGui::SeparatorText("Select Precise Node Index Range");
            ImGui::InputInt("Start Node", &startNode);
            ImGui::InputInt("End Node", &endNode);

            // Clamp inputs safely to valid track node ranges
            startNode = std::clamp(startNode, 0, std::max(0, totalNodes - 1));
            endNode = std::clamp(endNode, startNode, std::max(0, totalNodes - 1));

            ImGui::SeparatorText("Template Geometry");
            ImGui::Text("Nodes included: %d to %d (%d nodes)", startNode, endNode, endNode - startNode + 1);
            ImGui::TextDisabled("Note: Selected nodes will be baked relative to the start node.");

            ImGui::Separator();

            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (strlen(templateName) > 0) {
                    std::string filepath = "templates/" + std::string(templateName) + ".fvdtpl";
                    if (myTrack->saveTemplate(filepath, startNode, endNode)) {
                        initTemplateIndices = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                initTemplateIndices = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::EndChild();
}

void LeftPanel::renderAnchorProperties(trackHandler* hTrack, section* sec, Application* app) {
    track* myTrack = hTrack->trackData;
    bool changed = false;
    float lenFact = gloParent->mOptions->getLengthFactor();
    std::string lenFmt = "%.3f " + gloParent->mOptions->getLengthString();

    if (ImGui::TreeNodeEx("Position", 0)) {
        BEGIN_PROP_TABLE("AnchorPropsPos")
        PROP_ROW(
            "Position",
            glm::vec3 pos = (glm::vec3)myTrack->startPos * lenFact;
            bool posChanged = false;
            ImGui::Text("X"); ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 3.0f - 15.0f);
            if (ImGui::DragFloat("##AnchorPosX", &pos.x, 0.1f, 0.0f, 0.0f, lenFmt.c_str()) || common::ValueScroll(&pos.x, 1.0f, 0.1f)) posChanged = true;
            ImGui::SameLine(); ImGui::Text("Y"); ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2.0f - 10.0f);
            if (ImGui::DragFloat("##AnchorPosY", &pos.y, 0.1f, 0.0f, 0.0f, lenFmt.c_str()) || common::ValueScroll(&pos.y, 1.0f, 0.1f)) posChanged = true;
            ImGui::SameLine(); ImGui::Text("Z"); ImGui::SameLine(); ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##AnchorPosZ", &pos.z, 0.1f, 0.0f, 0.0f, lenFmt.c_str()) || common::ValueScroll(&pos.z, 1.0f, 0.1f)) posChanged = true;
            if (posChanged) { myTrack->startPos = (glm::dvec3)(pos / (double)lenFact); changed = true; })
        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Rotation", 0)) {
        BEGIN_PROP_TABLE("AnchorPropsRot")
        float yaw = (float)myTrack->startYaw, pitch = (float)myTrack->startPitch, roll = (float)myTrack->anchorNode->fRoll;
        PROP_ROW(
            "Psi (Yaw)", if (ImGui::DragFloat("##AnchorPsi", &yaw, 0.1f, -1800.0f, 1800.0f, "%.3f deg") || common::ValueScroll(&yaw, 1.0f, 0.1f)) { myTrack->startYaw = std::clamp(yaw, -1800.0f, 1800.0f); syncAnchorNode(hTrack); syncAnchorDerivsToForces(myTrack->anchorNode); changed = true; })
        PROP_ROW(
            "Theta (Pitch)", if (ImGui::DragFloat("##AnchorTheta", &pitch, 0.1f, -90.0f, 90.0f, "%.3f deg") || common::ValueScroll(&pitch, 1.0f, 0.1f)) { myTrack->startPitch = std::clamp(pitch, -90.0f, 90.0f); syncAnchorNode(hTrack); syncAnchorDerivsToForces(myTrack->anchorNode); changed = true; })
        PROP_ROW(
            "Phi (Roll)", if (ImGui::DragFloat("##AnchorPhi", &roll, 0.1f, -1800.0f, 1800.0f, "%.3f deg") || common::ValueScroll(&roll, 1.0f, 0.1f)) { myTrack->anchorNode->fRoll = std::clamp(roll, -1800.0f, 1800.0f); syncAnchorNode(hTrack); syncAnchorDerivsToForces(myTrack->anchorNode); changed = true; })
        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Rates", 0)) {
        BEGIN_PROP_TABLE("AnchorPropsDeriv")
        float dPsi = (float)myTrack->anchorNode->getYawChange(), dTheta = (float)myTrack->anchorNode->getPitchChange();
        PROP_ROW(
            "dPsi/dt", if (ImGui::DragFloat("##AnchorDPsi", &dPsi, 0.1f, -1800.0f, 1800.0f, "%.3f deg/s") || common::ValueScroll(&dPsi, 1.0f, 0.1f)) { myTrack->anchorNode->fYawFromLast = (double)std::clamp(dPsi, -1800.0f, 1800.0f) / F_HZ; syncAnchorNode(hTrack); syncAnchorDerivsToForces(myTrack->anchorNode); changed = true; })
        PROP_ROW(
            "dTheta/dt", if (ImGui::DragFloat("##AnchorDTheta", &dTheta, 0.1f, -1800.0f, 1800.0f, "%.3f deg/s") || common::ValueScroll(&dTheta, 1.0f, 0.1f)) { myTrack->anchorNode->fPitchFromLast = (double)std::clamp(dTheta, -1800.0f, 1800.0f) / F_HZ; syncAnchorNode(hTrack); syncAnchorDerivsToForces(myTrack->anchorNode); changed = true; })
        float displaySpeed = (float)(myTrack->anchorNode->fVel * gloParent->mOptions->getSpeedFactor());
        PROP_ROW(
            "Speed", if (ImGui::DragFloat("##AnchorSpeed", &displaySpeed, 0.1f, 0.1f, 500.0f, ("%.3f " + gloParent->mOptions->getSpeedString()).c_str()) || common::ValueScroll(&displaySpeed, 1.0f, 0.1f)) { myTrack->anchorNode->fVel = (double)std::max(0.1f * (float)gloParent->mOptions->getSpeedFactor(), displaySpeed) / gloParent->mOptions->getSpeedFactor(); syncAnchorNode(hTrack); syncAnchorDerivsToForces(myTrack->anchorNode); changed = true; })
        END_PROP_TABLE()
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Forces", 0)) {
        BEGIN_PROP_TABLE("AnchorPropsForces")
        float fn = (float)myTrack->anchorNode->forceNormal, fl = (float)myTrack->anchorNode->forceLateral;
        PROP_ROW(
            "Normal", if (ImGui::DragFloat("##AnchorNormal", &fn, 0.01f, -20.0f, 20.0f, "%.3f g") || common::ValueScroll(&fn, 1.0f, 0.1f)) { myTrack->anchorNode->forceNormal = std::clamp(fn, -20.0f, 20.0f); syncAnchorNode(hTrack); syncAnchorForcesToDerivs(myTrack->anchorNode); changed = true; })
        PROP_ROW(
            "Lateral", if (ImGui::DragFloat("##AnchorLateral", &fl, 0.01f, -15.0f, 15.0f, "%.3f g") || common::ValueScroll(&fl, 1.0f, 0.1f)) { myTrack->anchorNode->forceLateral = std::clamp(fl, -15.0f, 15.0f); syncAnchorNode(hTrack); syncAnchorForcesToDerivs(myTrack->anchorNode); changed = true; })
        END_PROP_TABLE()
        ImGui::TreePop();
    }
    if (changed)
        myTrack->requestUpdateTrack(0, 0);
}

void LeftPanel::renderStraightProperties(trackHandler* hTrack, section* sec, Application* app) {
    secstraight* s = static_cast<secstraight*>(sec);
    bool changed = false;
    BEGIN_PROP_TABLE("StraightProps")
    int speedModeRaw = s->bSpeed ? 0 : (s->fAccel == 0.0 ? 1 : 2);
    static int speedMode = 0;
    static section* lastSec = nullptr;
    if (sec != lastSec || !ImGui::IsAnyItemActive()) {
        lastSec = sec;
        speedMode = speedModeRaw;
    }
    PROP_ROW(
        "Motion Profile",
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##StraightSpeedMode", &speedMode, "Coasting\0Constant Velocity\0Constant Acceleration\0")) {
            if (speedMode == 0) {
                s->bSpeed = true;
                s->fAccel = 0.0;
            } else if (speedMode == 1) {
                s->bSpeed = false;
                s->fAccel = 0.0;
            } else {
                s->bSpeed = false;
                if (s->fAccel == 0.0)
                    s->fAccel = F_G;
            }
            changed = true;
            app->pushUndo();
        })
    if (!s->bSpeed) {
        if (speedMode == 1) {
            float displaySpeed = (float)s->fVel * gloParent->mOptions->getSpeedFactor();
            std::string spdFmt = "%.3f " + gloParent->mOptions->getSpeedString();
            PROP_ROW(
                "Speed",
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##StraightSpeed", &displaySpeed, 0.1f, 0.1f, 500.0f * gloParent->mOptions->getSpeedFactor(), spdFmt.c_str()) || common::ValueScroll(&displaySpeed, 1.0f, 0.1f)) {
                    s->fVel = (double)std::max(0.1f * (float)gloParent->mOptions->getSpeedFactor(), displaySpeed) / gloParent->mOptions->getSpeedFactor();
                    changed = true;
                })
        } else if (speedMode == 2) {
            float displayAccel = (float)(s->fAccel / F_G);
            PROP_ROW(
                "Acceleration",
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##StraightAccel", &displayAccel, 0.01f, -10.0f, 10.0f, "%.2f G") || common::ValueScroll(&displayAccel, 0.1f, 0.01f)) {
                    s->fAccel = (double)std::clamp(displayAccel, -10.0f, 10.0f) * F_G;
                    changed = true;
                })
        }
    }
    float displayLength = (float)(s->fHLength * gloParent->mOptions->getLengthFactor());
    std::string lenFmt = "%.3f " + gloParent->mOptions->getLengthString();
    PROP_ROW(
        "Length", if (ImGui::DragFloat("##StraightLength", &displayLength, 0.01f, 0.1f, 1000.0f * gloParent->mOptions->getLengthFactor(), lenFmt.c_str()) || common::ValueScroll(&displayLength, 1.0f, 0.1f)) { s->fHLength = std::max(0.1f * (float)gloParent->mOptions->getLengthFactor(), displayLength) / gloParent->mOptions->getLengthFactor(); s->rollFunc->setMaxArgument(s->fHLength); changed = true; })
    END_PROP_TABLE()
    if (changed)
        hTrack->trackData->requestUpdateTrack(sec, 0);
}

void LeftPanel::renderCurvedProperties(trackHandler* hTrack, section* sec, Application* app) {
    seccurved* c = static_cast<seccurved*>(sec);
    bool changed = false;
    BEGIN_PROP_TABLE("CurvedProps")
    int speedModeRaw = c->bSpeed ? 0 : (c->fAccel == 0.0 ? 1 : 2);
    static int speedMode = 0;
    static section* lastSec = nullptr;
    if (sec != lastSec || !ImGui::IsAnyItemActive()) {
        lastSec = sec;
        speedMode = speedModeRaw;
    }
    PROP_ROW(
        "Motion Profile",
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##CurvedSpeedMode", &speedMode, "Coasting\0Constant Velocity\0Constant Acceleration\0")) {
            if (speedMode == 0) {
                c->bSpeed = true;
                c->fAccel = 0.0;
            } else if (speedMode == 1) {
                c->bSpeed = false;
                c->fAccel = 0.0;
            } else {
                c->bSpeed = false;
                if (c->fAccel == 0.0)
                    c->fAccel = F_G;
            }
            changed = true;
            app->pushUndo();
        })
    if (!c->bSpeed) {
        if (speedMode == 1) {
            float displaySpeed = (float)c->fVel * gloParent->mOptions->getSpeedFactor();
            std::string spdFmt = "%.3f " + gloParent->mOptions->getSpeedString();
            PROP_ROW(
                "Speed",
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##CurvedSpeed", &displaySpeed, 0.1f, 0.1f, 500.0f * gloParent->mOptions->getSpeedFactor(), spdFmt.c_str()) || common::ValueScroll(&displaySpeed, 1.0f, 0.1f)) {
                    c->fVel = (double)std::max(0.1f * (float)gloParent->mOptions->getSpeedFactor(), displaySpeed) / gloParent->mOptions->getSpeedFactor();
                    changed = true;
                })
        } else if (speedMode == 2) {
            float displayAccel = (float)(c->fAccel / F_G);
            PROP_ROW(
                "Acceleration",
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##CurvedAccel", &displayAccel, 0.01f, -10.0f, 10.0f, "%.2f G") || common::ValueScroll(&displayAccel, 0.1f, 0.01f)) {
                    c->fAccel = (double)std::clamp(displayAccel, -10.0f, 10.0f) * F_G;
                    changed = true;
                })
        }
    }
    float displayRadius = (float)(c->fRadius * gloParent->mOptions->getLengthFactor());
    std::string lenFmt = "%.3f " + gloParent->mOptions->getLengthString();
    PROP_ROW(
        "Track radius", if (ImGui::DragFloat("##CurvedRadius", &displayRadius, 0.01f, 0.1f, 500.0f * gloParent->mOptions->getLengthFactor(), lenFmt.c_str()) || common::ValueScroll(&displayRadius, 1.0f, 0.1f)) { c->fRadius = std::max(0.1f, displayRadius / gloParent->mOptions->getLengthFactor()); changed = true; })
    float angle = (float)c->fAngle, direction = (float)c->fDirection, leadIn = (float)c->fLeadIn, leadOut = (float)c->fLeadOut;
    PROP_ROW(
        "Total angle", if (ImGui::DragFloat("##CurvedAngle", &angle, 0.01f, 0.1f, 5000.0f, "%.3f deg") || common::ValueScroll(&angle, 1.0f, 0.1f)) { c->fAngle = std::max(0.1f, angle); c->rollFunc->setMaxArgument(c->fAngle); changed = true; })
    PROP_ROW(
        "Direction", if (ImGui::DragFloat("##CurvedDir", &direction, 0.1f, -180.0f, 180.0f, "%.3f deg") || common::ValueScroll(&direction, 1.0f, 0.1f)) { c->fDirection = std::clamp(direction, -180.0f, 180.0f); changed = true; })
    PROP_ROW(
        "Lead in", if (ImGui::DragFloat("##CurvedLeadIn", &leadIn, 0.1f, 0.0f, 500.0f, "%.3f deg") || common::ValueScroll(&leadIn, 1.0f, 0.1f)) { c->fLeadIn = std::max(0.0f, leadIn); changed = true; })
    PROP_ROW(
        "Lead out", if (ImGui::DragFloat("##CurvedLeadOut", &leadOut, 0.1f, 0.0f, 500.0f, "%.3f deg") || common::ValueScroll(&leadOut, 1.0f, 0.1f)) { c->fLeadOut = std::max(0.0f, leadOut); changed = true; })
    END_PROP_TABLE()
    if (changed)
        hTrack->trackData->requestUpdateTrack(sec, 0);
}

void LeftPanel::renderForcedProperties(trackHandler* hTrack, section* sec, Application* app) {
    bool changed = false;
    BEGIN_PROP_TABLE("ForcedProps")
    int speedModeRaw = sec->bSpeed ? 0 : (sec->fAccel == 0.0 ? 1 : 2);
    static int speedMode = 0;
    static section* lastSec = nullptr;
    if (sec != lastSec || !ImGui::IsAnyItemActive()) {
        lastSec = sec;
        speedMode = speedModeRaw;
    }
    PROP_ROW(
        "Motion Profile",
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##ForcedSpeedMode", &speedMode, "Coasting\0Constant Velocity\0Constant Acceleration\0")) {
            if (speedMode == 0) {
                sec->bSpeed = true;
                sec->fAccel = 0.0;
            } else if (speedMode == 1) {
                sec->bSpeed = false;
                sec->fAccel = 0.0;
            } else {
                sec->bSpeed = false;
                if (sec->fAccel == 0.0)
                    sec->fAccel = F_G;
            }
            changed = true;
        })
    if (!sec->bSpeed) {
        if (speedMode == 1) {
            float displaySpeed = (float)sec->fVel * gloParent->mOptions->getSpeedFactor();
            std::string spdFmt = "%.3f " + gloParent->mOptions->getSpeedString();
            PROP_ROW(
                "Speed",
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##ForcedSpeed", &displaySpeed, 0.1f, 0.1f, 500.0f * gloParent->mOptions->getSpeedFactor(), spdFmt.c_str()) || common::ValueScroll(&displaySpeed, 1.0f, 0.1f)) {
                    sec->fVel = (double)std::max(0.1f * (float)gloParent->mOptions->getSpeedFactor(), displaySpeed) / gloParent->mOptions->getSpeedFactor();
                    changed = true;
                })
        } else if (speedMode == 2) {
            float displayAccel = (float)(sec->fAccel / F_G);
            PROP_ROW(
                "Acceleration",
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##ForcedAccel", &displayAccel, 0.01f, -10.0f, 10.0f, "%.2f G") || common::ValueScroll(&displayAccel, 0.1f, 0.01f)) {
                    sec->fAccel = (double)std::clamp(displayAccel, -10.0f, 10.0f) * F_G;
                    changed = true;
                })
        }
    }
    END_PROP_TABLE()
    if (changed)
        hTrack->trackData->requestUpdateTrack(sec, 0);
}

void LeftPanel::renderStaticProperties(trackHandler* hTrack, section* sec, Application* app) {
    bool changed = false;
    BEGIN_PROP_TABLE("StaticProps")

    int speedModeRaw = 0;
    if (sec->bSpeed) {
        speedModeRaw = 1;
    } else if (sec->fAccel != 0.0) {
        speedModeRaw = 3;
    } else if (sec->fVel > 0.0) {
        speedModeRaw = 2;
    }

    static int speedMode = 0;
    static section* lastSec = nullptr;
    if (sec != lastSec || !ImGui::IsAnyItemActive()) {
        lastSec = sec;
        speedMode = speedModeRaw;
    }

    PROP_ROW(
        "Motion Profile",
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##StaticSpeedMode", &speedMode, "Inherit Speed\0Coasting\0Constant Velocity\0Constant Acceleration\0")) {
            if (speedMode == 0) {
                sec->bSpeed = false;
                sec->fAccel = 0.0;
                sec->fVel = 0.0;
            } else if (speedMode == 1) {
                sec->bSpeed = true;
                sec->fAccel = 0.0;
            } else if (speedMode == 2) {
                sec->bSpeed = false;
                sec->fAccel = 0.0;
                if (sec->fVel == 0.0)
                    sec->fVel = 10.0;
            } else {
                sec->bSpeed = false;
                if (sec->fAccel == 0.0)
                    sec->fAccel = F_G;
            }
            changed = true;
            app->pushUndo();
        })

    if (speedMode == 2) {
        float displaySpeed = (float)sec->fVel * gloParent->mOptions->getSpeedFactor();
        std::string spdFmt = "%.3f " + gloParent->mOptions->getSpeedString();
        PROP_ROW(
            "Speed",
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##StaticSpeed", &displaySpeed, 0.1f, 0.1f, 500.0f * gloParent->mOptions->getSpeedFactor(), spdFmt.c_str()) || common::ValueScroll(&displaySpeed, 1.0f, 0.1f)) {
                sec->fVel = (double)std::max(0.1f * (float)gloParent->mOptions->getSpeedFactor(), displaySpeed) / gloParent->mOptions->getSpeedFactor();
                changed = true;
            })
    } else if (speedMode == 3) {
        float displayAccel = (float)(sec->fAccel / F_G);
        PROP_ROW(
            "Acceleration",
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##StaticAccel", &displayAccel, 0.01f, -10.0f, 10.0f, "%.2f G") || common::ValueScroll(&displayAccel, 0.1f, 0.01f)) {
                sec->fAccel = (double)std::clamp(displayAccel, -10.0f, 10.0f) * F_G;
                changed = true;
            })
    }

    END_PROP_TABLE()
    if (changed)
        hTrack->trackData->requestUpdateTrack(sec, 0);
}

void LeftPanel::renderColorsTab(trackHandler* hTrack, Application* app) {
    if (!hTrack)
        return;
    if (ImGui::TreeNodeEx("Track Colors", 0)) {
        BEGIN_PROP_TABLE("TrackColTable")
        PROP_ROW("Default", ImGui::ColorEdit3("##TrkCol0", &hTrack->trackColors[0].x);)
        PROP_ROW("Section", ImGui::ColorEdit3("##TrkCol1", &hTrack->trackColors[1].x);)
        PROP_ROW("Transition", ImGui::ColorEdit3("##TrkCol2", &hTrack->trackColors[2].x);)
        PROP_ROW("Heartline", ImGui::ColorEdit3("##TrkCol3", &hTrack->trackColors[3].x);)
        END_PROP_TABLE()
        ImGui::TreePop();
    }
    ImGui::Separator();
    if (ImGui::TreeNodeEx("Graph Colors", 0)) {
        BEGIN_PROP_TABLE("GraphColTable")
        auto graphColRow = [&](const char* label, int idx) { PROP_ROW(label, ImGui::ColorEdit3((std::string("##GrCol") + std::to_string(idx)).c_str(), &gloParent->mOptions->graphColors[idx].x);); };
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Editable");
        ImGui::TableNextColumn();
        graphColRow("Roll Speed", (int)GraphType::EditRoll);
        graphColRow("Normal Force", (int)GraphType::EditNormal);
        graphColRow("Lateral Force", (int)GraphType::EditLateral);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Resulting");
        ImGui::TableNextColumn();
        graphColRow("Banking", (int)GraphType::Banking);
        graphColRow("Roll Speed", (int)GraphType::RollSpeed);
        graphColRow("Roll Accel", (int)GraphType::RollAccel);
        graphColRow("Normal Force", (int)GraphType::NForce);
        graphColRow("N-Force Change", (int)GraphType::NForceChange);
        graphColRow("Lateral Force", (int)GraphType::LForce);
        graphColRow("L-Force Change", (int)GraphType::LForceChange);
        graphColRow("Rider Pitch Change", (int)GraphType::PitchChange);
        graphColRow("Rider Yaw Change", (int)GraphType::YawChange);
        graphColRow("World Pitch Change", (int)GraphType::WorldPitchChange);
        graphColRow("World Yaw Change", (int)GraphType::WorldYawChange);
        END_PROP_TABLE()
        ImGui::TreePop();
    }
}

void LeftPanel::renderEnvironmentTab() {
    if (ImGui::TreeNodeEx("Ground", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("GroundProps")
        float size = gloParent->projectGrdTexSize;
        PROP_ROW(
            "Texture Size", if (ImGui::DragFloat("##GrdSize", &size, 1.0f, 1.0f, 10000.0f, "%.1f") || common::ValueScroll(&size, 10.0f, 1.0f)) { gloParent->projectGrdTexSize = std::max(1.0f, size); if (gViewport) gViewport->setGroundTextureSize(gloParent->projectGrdTexSize); })
        PROP_ROW(
            "Texture", if (ImGui::Button("Load...")) { auto f = pfd::open_file("Open Ground Texture", ".", {"Image Files", "*.jpg *.png *.bmp", "All Files", "*"}).result(); if (!f.empty()) { if (gViewport) { gViewport->loadGroundTexture(f[0]); gloParent->projectGroundTex = f[0]; } } })
        END_PROP_TABLE()
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Lighting & Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("ShadowProps")
        extern Viewport* gViewport;
        PROP_ROW(
            "Enable Shadows", if (ImGui::Checkbox("##EnableShadows", &gloParent->mOptions->shadowsEnabled)) { if (gViewport) gViewport->setShadowMode(gloParent->mOptions->shadowsEnabled ? 1 : 0); })
        PROP_ROW(
            "Sun Pitch", if (ImGui::SliderFloat("##SunPitch", &gloParent->mOptions->sunPitch, -90.0f, 0.0f, "%.1f deg")) {
                if (gViewport)
                    gViewport->setLightDirection(gloParent->mOptions->sunPitch, gloParent->mOptions->sunYaw);
            })
        PROP_ROW(
            "Sun Yaw", if (ImGui::SliderFloat("##SunYaw", &gloParent->mOptions->sunYaw, -180.0f, 180.0f, "%.1f deg")) {
                if (gViewport)
                    gViewport->setLightDirection(gloParent->mOptions->sunPitch, gloParent->mOptions->sunYaw);
            })
        END_PROP_TABLE()
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("SkyboxProps")
        if (!gloParent->skyboxAvailable)
            ImGui::BeginDisabled();
        PROP_ROW(
            "Custom Skybox", if (ImGui::Checkbox("##SkyboxEnabled", &gloParent->mOptions->skyboxEnabled)) {
                extern Viewport* gViewport;
                if (gViewport)
                    gViewport->markSceneDirty();
            })
        if (!gloParent->skyboxAvailable) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Custom skybox requires cubemap_0.png through cubemap_5.png in the 'skybox' folder.");
        }
        END_PROP_TABLE()
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Mist (Far Field)", ImGuiTreeNodeFlags_DefaultOpen)) {
        BEGIN_PROP_TABLE("MistProps")
        extern Viewport* gViewport;
        PROP_ROW(
            "Enabled", if (ImGui::Checkbox("##MistEnabled", &gloParent->mOptions->mistEnabled)) { if (gViewport) gViewport->markSceneDirty(); })
        PROP_ROW(
            "Near Dist", if (ImGui::DragFloat("##MistNear", &gloParent->mOptions->mistNear, 1.0f, 0.0f, 5000.0f, "%.1f m") || common::ValueScroll(&gloParent->mOptions->mistNear, 10.0f, 1.0f)) {
                gloParent->mOptions->mistNear = std::clamp(gloParent->mOptions->mistNear, 0.0f, 5000.0f);
                if (gViewport)
                    gViewport->markSceneDirty();
            })
        PROP_ROW(
            "Far Dist", if (ImGui::DragFloat("##MistFar", &gloParent->mOptions->mistFar, 1.0f, 0.0f, 10000.0f, "%.1f m") || common::ValueScroll(&gloParent->mOptions->mistFar, 10.0f, 1.0f)) {
                gloParent->mOptions->mistFar = std::clamp(gloParent->mOptions->mistFar, gloParent->mOptions->mistNear, 10000.0f);
                if (gViewport)
                    gViewport->markSceneDirty();
            })
        PROP_ROW(
            "Color", if (ImGui::ColorEdit3("##MistColor", &gloParent->mOptions->mistColor.r)) {
                if (gViewport)
                    gViewport->setMistColor(gloParent->mOptions->mistColor);
            })
        END_PROP_TABLE()
        ImGui::TreePop();
    }
}

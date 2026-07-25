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

#include "graphview.h"
#include "renderer/viewport.h"
#include "trackhandler.h"
#include "track.h"
#include "section.h"
#include "seccurved.h"
#include "function.h"
#include "subfunction.h"
#include "dummies.h"
#include "portable-file-dialogs.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>

GraphView::GraphView()
    : needsUpdate(true), playheadSnappingEnabled(true), doAutoFocus(false), autoScaleYEdit(true), autoScaleYResult(true), autoScaleYMeasure(true), playheadLocked(false), lockedPOVPos(0), selectedSubfunc(nullptr), lastSelectedMinArg(-1.0), lastSelectedMaxArg(-1.0) {
    graphs.resize((int)GraphType::Count);

    auto getCol = [](GraphType type) {
        if (type >= GraphType::Velocity) {
            return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        }
        glm::vec3 c = gloParent->mOptions->graphColors[(int)type];
        return ImVec4(c.x, c.y, c.z, 1.0f);
    };

    graphs[(int)GraphType::EditRoll] = {{}, "Roll Speed", getCol(GraphType::EditRoll), true};
    graphs[(int)GraphType::EditNormal] = {{}, "Normal Force", getCol(GraphType::EditNormal), true};
    graphs[(int)GraphType::EditLateral] = {{}, "Lateral Force", getCol(GraphType::EditLateral), true};

    graphs[(int)GraphType::Banking] = {{}, "Banking", getCol(GraphType::Banking), false};
    graphs[(int)GraphType::RollSpeed] = {{}, "Roll Speed", getCol(GraphType::RollSpeed), false};
    graphs[(int)GraphType::RollAccel] = {{}, "Roll Accel", getCol(GraphType::RollAccel), false};
    graphs[(int)GraphType::NForce] = {{}, "Normal Force", getCol(GraphType::NForce), false};
    graphs[(int)GraphType::NForceChange] = {{}, "Force Change", getCol(GraphType::NForceChange), false};
    graphs[(int)GraphType::LForce] = {{}, "Lateral Force", getCol(GraphType::LForce), false};
    graphs[(int)GraphType::LForceChange] = {{}, "Force Change", getCol(GraphType::LForceChange), false};
    graphs[(int)GraphType::PitchChange] = {{}, "Rider Pitch Change", getCol(GraphType::PitchChange), false};
    graphs[(int)GraphType::YawChange] = {{}, "Rider Yaw Change", getCol(GraphType::YawChange), false};
    graphs[(int)GraphType::Velocity] = {{}, "Velocity", ImVec4(0.2f, 0.8f, 0.8f, 1.0f), false};
    graphs[(int)GraphType::WorldPitch] = {{}, "World Pitch", ImVec4(0.8f, 0.8f, 0.2f, 1.0f), false};
    graphs[(int)GraphType::WorldPitchChange] = {{}, "World Pitch Change", ImVec4(0.9f, 0.4f, 0.1f, 1.0f), false};
    graphs[(int)GraphType::WorldYawChange] = {{}, "World Yaw Change", ImVec4(0.1f, 0.9f, 0.4f, 1.0f), false};
}

GraphView::~GraphView() {}

void GraphView::lockPlayheadToCurrentTransition(trackHandler* hTrack) {
    playheadLocked = false;
    lockedTransition = nullptr;
    lockedPOVPos = gViewport ? gViewport->getPOVPos() : 0;

    if (gloParent && gloParent->selectedFunc) {
        lockedTransition = gloParent->selectedFunc;
        playheadLocked = true;
    }
}

void GraphView::renderList(trackHandler* hTrack) {
    if (!hTrack || !hTrack->trackData)
        return;

    // Refresh colors from Options
    auto getCol = [](GraphType type) {
        if (type >= GraphType::Velocity) {
            return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        }
        glm::vec3 c = gloParent->mOptions->graphColors[(int)type];
        return ImVec4(c.x, c.y, c.z, 1.0f);
    };
    for (int i = 0; i < (int)GraphType::Count; ++i) {
        graphs[i].color = getCol((GraphType)i);
    }

    section* activeSec = hTrack->trackData->activeSection;
    doAutoFocus = false;

    if (lastVisible.size() != (int)GraphType::Count) {
        lastVisible.resize((int)GraphType::Count, false);
    }

    for (int i = 0; i < (int)GraphType::Count; i++) {
        if (graphs[i].visible != lastVisible[i]) {
            doAutoFocus = true;
            if (graphs[i].visible && i >= (int)GraphType::Banking) {
                switchToResultingTab = true;
            }
            lastVisible[i] = graphs[i].visible;
        }
    }

    if (hTrack->trackData->activeSection != lastActiveSection) {
        lastActiveSection = hTrack->trackData->activeSection;
        needsUpdate = true;
        if (lastActiveSection) {
            doAutoFocus = true;
            if (playheadSnappingEnabled) {
                int startIdx = hTrack->trackData->getNumPoints(lastActiveSection);
                int endIdx = startIdx + (int)lastActiveSection->lNodes.size() - 1;
                gViewport->setPOVPos(endIdx);
                povAccumulator = (float)endIdx;
            }
        }
    }

    if (needsUpdate || hTrack->trackData->graphChanged) {
        updateData(hTrack);
        needsUpdate = false;
        hTrack->trackData->graphChanged = false;
    }

    for (auto& eg : editableGraphs) {
        if (!eg.sf || !eg.sf->parent)
            continue;
        GraphType type = GraphType::EditRoll;
        if (eg.sf->parent->type == funcPitch || eg.sf->parent->type == funcNormal)
            type = GraphType::EditNormal;
        else if (eg.sf->parent->type == funcYaw || eg.sf->parent->type == funcLateral)
            type = GraphType::EditLateral;
        eg.color = getCol(type);
    }

    bool isForced = activeSec && activeSec->type == forced;
    bool isGeo = activeSec && (activeSec->type == geometric || activeSec->type == geometricriderlocal);

    bool distArg = activeSec && activeSec->bArgument == 1;
    std::string rateUnit = distArg ? "[deg/m]" : "[deg/s]";

    if (ImGui::TreeNodeEx("Editable Graphs", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::EditRoll].color);
        ImGui::Checkbox("Roll Speed [deg/s]##eg", &graphs[(int)GraphType::EditRoll].visible);
        ImGui::PopStyleColor();
        if (isForced || isGeo) {
            std::string nLabel = isGeo ? "Pitch Change [deg/s]##eg" : "Normal Force [g]##eg";
            std::string lLabel = isGeo ? "Yaw Change [deg/s]##eg" : "Lateral Force [g]##eg";
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::EditNormal].color);
            ImGui::Checkbox(nLabel.c_str(), &graphs[(int)GraphType::EditNormal].visible);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::EditLateral].color);
            ImGui::Checkbox(lLabel.c_str(), &graphs[(int)GraphType::EditLateral].visible);
            ImGui::PopStyleColor();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Resulting Graphs", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::TreeNodeEx("Roll##rg", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::Banking].color);
            ImGui::Checkbox("Banking [deg]##rg", &graphs[(int)GraphType::Banking].visible);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::RollAccel].color);
            ImGui::Checkbox("Roll Accel [deg/s²]##rg", &graphs[(int)GraphType::RollAccel].visible);
            ImGui::PopStyleColor();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Normal Force##rg", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::NForce].color);
            ImGui::Checkbox("Normal Force [g]##rg_nf", &graphs[(int)GraphType::NForce].visible);
            ImGui::PopStyleColor();
            ImGui::BeginDisabled(true);
            graphs[(int)GraphType::NForceChange].visible = false;
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::NForceChange].color);
            ImGui::Checkbox("Force Change [g/s]##rg_nfc", &graphs[(int)GraphType::NForceChange].visible);
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Lateral Force##rg", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::LForce].color);
            ImGui::Checkbox("Lateral Force [g]##rg_lf", &graphs[(int)GraphType::LForce].visible);
            ImGui::PopStyleColor();
            ImGui::BeginDisabled(true);
            graphs[(int)GraphType::LForceChange].visible = false;
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::LForceChange].color);
            ImGui::Checkbox("Force Change [g/s]##rg_lfc", &graphs[(int)GraphType::LForceChange].visible);
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Geometric##rg", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::string rpcLabel = "Rider Pitch Change " + rateUnit + "##rg";
            std::string rycLabel = "Rider Yaw Change " + rateUnit + "##rg";
            std::string wpcLabel = "World Pitch Change " + rateUnit + "##rg";
            std::string wycLabel = "World Yaw Change " + rateUnit + "##rg";
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::PitchChange].color);
            ImGui::Checkbox(rpcLabel.c_str(), &graphs[(int)GraphType::PitchChange].visible);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::YawChange].color);
            ImGui::Checkbox(rycLabel.c_str(), &graphs[(int)GraphType::YawChange].visible);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::WorldPitchChange].color);
            ImGui::Checkbox(wpcLabel.c_str(), &graphs[(int)GraphType::WorldPitchChange].visible);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, graphs[(int)GraphType::WorldYawChange].color);
            ImGui::Checkbox(wycLabel.c_str(), &graphs[(int)GraphType::WorldYawChange].visible);
            ImGui::PopStyleColor();
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Options")) {
        ImGui::Checkbox("Graph Hover Overlay", &gloParent->mOptions->graphOverlayEnabled);
        ImGui::Checkbox("Playhead Snapping", &playheadSnappingEnabled);
        ImGui::TreePop();
    }
    // (Scaling configuration has been moved directly to individual graph tabs)

    // Display performance metrics (plotted points count)
    int totalPlotted = 0;
    for (int i = (int)GraphType::Banking; i < (int)GraphType::Count; ++i) {
        if (graphs[i].visible) {
            for (const auto& sec : graphs[i].sections) {
                totalPlotted += sec.x.size();
            }
        }
    }
    for (int i = 0; i < (int)GraphType::Banking; ++i) {
        if (graphs[i].visible) {
            for (const auto& eg : editableGraphs) {
                totalPlotted += eg.x.size();
            }
        }
    }
    if (totalPlotted > 0) {
        ImGui::Separator();
        ImGui::TextDisabled("Plotted Points: %d", totalPlotted);
    }

    ImGui::Separator();
    if (ImGui::Button("Export Tab to CSV...##exportCsv", ImVec2(-1, 0))) {
        auto selection = pfd::save_file("Export Plotted Curves as CSV", "graphs.csv", {"CSV Files", "*.csv"}).result();
        if (!selection.empty()) {
            exportToCSV(selection, hTrack);
        }
    }
}

void GraphView::renderMeasurementsMenu(trackHandler* hTrack) {
    if (!hTrack || !hTrack->trackData)
        return;

    bool hasOffsets = !hTrack->trackData->trainOffsets.empty();

    ImGui::BeginDisabled(!hasOffsets);
    ImGui::SeparatorText("Analysis");
    if (ImGui::BeginMenu("Highlight Peaks")) {
        int& mode = hTrack->trackData->forceHighlightMode;
        bool isNone = (mode == 0);
        if (ImGui::MenuItem("None", nullptr, &isNone) && isNone)
            mode = 0;
        bool isNorm = (mode == 1);
        if (ImGui::MenuItem("Normal Force", nullptr, &isNorm) && isNorm)
            mode = 1;
        bool isLat = (mode == 2);
        if (ImGui::MenuItem("Lateral Force", nullptr, &isLat) && isLat)
            mode = 2;
        ImGui::EndMenu();
    }
    ImGui::EndDisabled();
}

bool GraphView::hasResultingGraphsVisible() const {
    for (int i = (int)GraphType::Banking; i < (int)GraphType::Count; i++) {
        if (graphs[i].visible)
            return true;
    }
    return false;
}

int GraphView::getTotalPlottedPoints() const {
    int totalPlotted = 0;
    for (int i = (int)GraphType::Banking; i < (int)GraphType::Count; ++i) {
        if (graphs[i].visible) {
            for (const auto& sec : graphs[i].sections) {
                totalPlotted += sec.x.size();
            }
        }
    }
    for (int i = 0; i < (int)GraphType::Banking; ++i) {
        if (graphs[i].visible) {
            for (const auto& eg : editableGraphs) {
                totalPlotted += eg.x.size();
            }
        }
    }
    return totalPlotted;
}

void GraphView::renderPlot(trackHandler* hTrack) {
    static bool fitFullExtents = false;

    if (!hTrack || !hTrack->trackData)
        return;

    renderTimeline(hTrack);

    ImGui::Checkbox("Auto-Scale Y", &autoScaleYEdit);
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        ImPlot::SetNextAxesToFit();
    }
    ImGui::SameLine();
    bool canLock = playheadLocked || (gloParent && gloParent->selectedFunc != nullptr);
    if (!canLock)
        ImGui::BeginDisabled();
    std::string lockLabel = playheadLocked ? "Unlock Playhead" : "Lock Playhead";
    if (ImGui::Button(lockLabel.c_str())) {
        playheadLocked = !playheadLocked;
        if (playheadLocked) {
            lockPlayheadToCurrentTransition(hTrack);
        } else {
            lockedTransition = nullptr;
        }
    }
    if (!canLock)
        ImGui::EndDisabled();
    if (!playheadLocked && !canLock) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Select a transition in the graphs or transition list to lock the playhead to.");
        }
    }
    if (!autoScaleYEdit) {
        ImGui::SameLine();
        ImGui::TextDisabled("|  Tip: Hover over any active Y-axis on the sides and use your scroll wheel to zoom, or drag to pan.");
    }

    section* activeSec = hTrack->trackData->activeSection;
    bool isForced = activeSec && activeSec->type == forced;
    bool isGeo = activeSec && (activeSec->type == geometric || activeSec->type == geometricriderlocal);
    bool isStraight = activeSec && activeSec->type == straight;
    bool isCurved = activeSec && activeSec->type == curved;

    auto isVisible = [&](int i) {
        if (i == (int)GraphType::EditRoll)
            return graphs[(int)GraphType::EditRoll].visible;
        if (i == (int)GraphType::EditNormal)
            return (isForced || isGeo) && graphs[(int)GraphType::EditNormal].visible;
        if (i == (int)GraphType::EditLateral)
            return (isForced || isGeo) && graphs[(int)GraphType::EditLateral].visible;

        return false;
    };

    auto isResultingVisible = [&](int i) {
        if (i >= (int)GraphType::Banking && i < (int)GraphType::Count)
            return graphs[i].visible;
        return false;
    };

    auto getUnderlyingType = [&](int editType) {
        if (editType == (int)GraphType::EditRoll)
            return (int)GraphType::RollSpeed;
        if (editType == (int)GraphType::EditNormal) {
            if (isGeo)
                return (int)GraphType::PitchChange;
            return (int)GraphType::NForce;
        }
        if (editType == (int)GraphType::EditLateral) {
            if (isGeo)
                return (int)GraphType::YawChange;
            return (int)GraphType::LForce;
        }
        return editType;
    };

    auto getAxis = [&](int type) {
        if (type == (int)GraphType::YawChange || type == (int)GraphType::WorldYawChange)
            return (int)ImAxis_Y2;
        if (type == (int)GraphType::PitchChange || type == (int)GraphType::WorldPitchChange)
            return (int)ImAxis_Y4;
        if (type == (int)GraphType::Banking || type == (int)GraphType::RollSpeed || type == (int)GraphType::RollAccel)
            return (int)ImAxis_Y1;
        if (type == (int)GraphType::NForce || type == (int)GraphType::NForceChange || type == (int)GraphType::LForce || type == (int)GraphType::LForceChange)
            return isForced ? (int)ImAxis_Y2 : (int)ImAxis_Y3;

        if (type == (int)GraphType::EditRoll)
            return (int)ImAxis_Y1;
        if (type == (int)GraphType::EditNormal)
            return isGeo ? (int)ImAxis_Y4 : (int)ImAxis_Y2;
        if (type == (int)GraphType::EditLateral)
            return isGeo ? (int)ImAxis_Y2 : (int)ImAxis_Y2;

        return (int)ImAxis_Y1;
    };

    if (fitFullExtents) {
        ImPlot::SetNextAxesToFit();
        fitFullExtents = false;
        doAutoFocus = false;
    }

    bool appliedFocus = false;
    double focusMinX = 0, focusMaxX = 1;
    if (doAutoFocus) {
        double startX = 0, endX = 1.0;
        int secIdx = hTrack->trackData->getSectionNumber(lastActiveSection);
        if (secIdx >= 0 && secIdx < (int)sectionBoundaries.size() - 1) {
            startX = sectionBoundaries[secIdx];
            endX = sectionBoundaries[secIdx + 1];
            double padding = (endX - startX) * 0.05;
            if (padding <= 0)
                padding = 0.5;
            focusMinX = startX - padding;
            focusMaxX = endX + padding;
            ImPlot::SetNextAxisLimits(ImAxis_X1, focusMinX, focusMaxX, ImGuiCond_Always);
        } else {
            startX = 0;
            endX = sectionBoundaries.empty() ? 10.0 : sectionBoundaries.back();
            focusMinX = startX;
            focusMaxX = endX;
            ImPlot::SetNextAxisLimits(ImAxis_X1, focusMinX, focusMaxX, ImGuiCond_Always);
        }

        linkedXMin = focusMinX;
        linkedXMax = focusMaxX;

        doAutoFocus = false;
        appliedFocus = true;
    }

    if (autoScaleYEdit) {
        // 0.2f applies 10% of the total range to both the top and bottom.
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.0f, 0.2f));
    }

    bool pushedTrans = gloParent->mOptions->transparentGraphs;
    ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, ImVec4(0, 0, 0, 0));
    if (pushedTrans) {
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    }

    bool distArg = hTrack->trackData->activeSection && hTrack->trackData->activeSection->bArgument == 1;
    if (isStraight || isCurved)
        distArg = false;

    std::string xLabel = distArg ? ("Distance (" + gloParent->mOptions->getLengthString() + ")") : "Time (s)";

    if (ImPlot::BeginPlot("##CoasterGraphs", ImVec2(-1, -1), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        auto isSectionVisibleInViewport = [&](const auto& sec) {
            if (sec.x.empty())
                return false;
            if (linkedXMin >= linkedXMax)
                return true; // first frame default
            double secStart = sec.x.front();
            double secEnd = sec.x.back();
            return (secEnd >= linkedXMin && secStart <= linkedXMax);
        };

        bool useY[4] = {false, false, false, false}; // 0:Y1(Banking), 1:Y3(Forces), 2:Y4(Pitch), 3:Y2(Yaw)
        double yMin[4] = {100000, 100000, 100000, 100000};
        double yMax[4] = {-100000, -100000, -100000, -100000};

        auto getA = [&](int ax) {
            if (ax == (int)ImAxis_Y1)
                return 0;
            if (ax == (int)ImAxis_Y3)
                return 1;
            if (ax == (int)ImAxis_Y4)
                return 2;
            if (ax == (int)ImAxis_Y2)
                return 3;
            return 0;
        };

        auto updateBounds = [&](int ax, double val) {
            int a = getA(ax);
            if (val < yMin[a])
                yMin[a] = val;
            if (val > yMax[a])
                yMax[a] = val;
        };

        ImVec4 axisColors[4] = {ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1)};

        for (int i = 0; i < (int)GraphType::Count; i++) {
            bool visible = isVisible(i);
            if (visible) {
                int ax = getAxis(i);
                int a = getA(ax);
                if (!useY[a]) { // Use the color of the first visible graph for this axis
                    axisColors[a] = graphs[i].color;
                    axisColors[a].w = 1.0f;
                }
                useY[a] = true;
                if (autoScaleYEdit) {
                    for (const auto& sec : graphs[i].sections) {
                        if (sec.x.empty())
                            continue;
                        if (!sec.isActive && !isSectionVisibleInViewport(sec))
                            continue;
                        for (double val : sec.y) {
                            updateBounds(ax, val);
                        }
                    }
                }
            }
        }
        for (const auto& eg : editableGraphs) {
            if (!eg.sf || !eg.sf->parent)
                continue;
            int type = (int)GraphType::EditRoll;
            if (eg.sf->parent->type == funcPitch || eg.sf->parent->type == funcNormal)
                type = (int)GraphType::EditNormal;
            else if (eg.sf->parent->type == funcYaw || eg.sf->parent->type == funcLateral)
                type = (int)GraphType::EditLateral;
            if (isVisible(type)) {
                int ax = getAxis(getUnderlyingType(type));
                useY[getA(ax)] = true;
                if (autoScaleYEdit) {
                    for (double val : eg.y) {
                        updateBounds(ax, val);
                    }
                }
            }
        }

        auto includeInactiveEditableBounds = [&](int editType) {
            if (!graphs[editType].visible)
                return;
            if ((editType == (int)GraphType::EditNormal || editType == (int)GraphType::EditLateral) && !isForced && !isGeo)
                return;

            int underType = getUnderlyingType(editType);
            int ax = getAxis(underType);
            useY[getA(ax)] = true;
            if (autoScaleYEdit) {
                for (const auto& secData : graphs[underType].sections) {
                    if (secData.isActive || secData.x.empty())
                        continue;
                    if (isSectionVisibleInViewport(secData)) {
                        for (double val : secData.y)
                            updateBounds(ax, val);
                    }
                }
            }
        };
        includeInactiveEditableBounds((int)GraphType::EditRoll);
        includeInactiveEditableBounds((int)GraphType::EditNormal);
        includeInactiveEditableBounds((int)GraphType::EditLateral);

        bool useY1 = useY[0];
        bool useY3 = useY[1];
        bool useY4 = useY[2];
        bool useY2 = useY[3];

        ImPlot::SetupAxis(ImAxis_X1, nullptr);
        ImPlot::SetupAxisLinks(ImAxis_X1, &linkedXMin, &linkedXMax);

        int activeYAxis = ImAxis_Y1;
        bool hasActiveYAxis = false;
        ImVec4 activeYColor = ImVec4(1, 1, 1, 1);

        if (gloParent && gloParent->selectedFunc && gloParent->selectedFunc->parent) {
            int type = (int)GraphType::EditRoll;
            if (gloParent->selectedFunc->parent->type == funcPitch || gloParent->selectedFunc->parent->type == funcNormal)
                type = (int)GraphType::EditNormal;
            else if (gloParent->selectedFunc->parent->type == funcYaw || gloParent->selectedFunc->parent->type == funcLateral)
                type = (int)GraphType::EditLateral;
            activeYAxis = getAxis(getUnderlyingType(type));
            hasActiveYAxis = true;
            activeYColor = graphs[type].color;
            activeYColor.w = 1.0f; // Make it opaque for text
        } else {
            if (useY1)
                activeYAxis = ImAxis_Y1;
            else if (useY2)
                activeYAxis = ImAxis_Y2;
            else if (useY3)
                activeYAxis = ImAxis_Y3;
            else if (useY4)
                activeYAxis = ImAxis_Y4;
        }

        auto applyBounds = [&](int ax, int a, const char* label, ImPlotAxisFlags flags) {
            bool highlight = hasActiveYAxis && ax == activeYAxis;
            ImVec4 col = highlight ? activeYColor : axisColors[a];
            bool pushCol = highlight || (useY[a] && (axisColors[a].x != 1.0f || axisColors[a].y != 1.0f || axisColors[a].z != 1.0f));
            if (pushCol) {
                ImPlot::PushStyleColor(ImPlotCol_AxisText, col);
            }
            ImPlot::SetupAxis(ax, nullptr, flags);
            if (pushCol) {
                ImPlot::PopStyleColor();
            }

            if (useY[a]) {
                double lowerBound = yMin[a];
                double upperBound = yMax[a];
                double final_edge = 0.0;
                if (lowerBound > upperBound) {
                    lowerBound = -0.1;
                    upperBound = 0.1 + 0.02 * a;
                } else {
                    double edge1 = (upperBound - lowerBound) / (6.0 - a);
                    double edge2 = edge1 > std::abs(upperBound) ? edge1 : std::abs(upperBound) / (6.0 - a);
                    final_edge = edge2 > std::abs(lowerBound) ? edge2 : std::abs(lowerBound) / (6.0 - a);
                    upperBound += final_edge;
                    lowerBound -= final_edge;
                }

                double minAllowedMax = 0.1 + 0.02 * a;
                double minLim = -0.1;
                double maxLim = minAllowedMax;
                if (final_edge < 0 || (upperBound <= minAllowedMax && lowerBound >= -0.1)) {
                    minLim = -0.1;
                    maxLim = minAllowedMax;
                } else if (upperBound > minAllowedMax && lowerBound >= -0.1) {
                    minLim = -0.1;
                    maxLim = upperBound;
                } else if (upperBound <= minAllowedMax && lowerBound < -0.1) {
                    minLim = lowerBound;
                    maxLim = minAllowedMax;
                } else {
                    minLim = lowerBound;
                    maxLim = upperBound;
                }
                ImPlot::SetupAxisLimits(ax, minLim, maxLim, autoScaleYEdit ? ImGuiCond_Always : ImGuiCond_Appearing);

                if (ax == ImAxis_Y2 || ax == ImAxis_Y3 || ax == ImAxis_Y4) {
                    const char* const emptyLabels[] = {"", ""};
                    ImPlot::SetupAxisTicks(ax, minLim, maxLim, 2, emptyLabels, true);
                }
            } else {
                ImPlot::SetupAxisLimits(ax, -1, 1, ImGuiCond_Appearing);
            }
        };

        ImPlotAxisFlags y1Flags = 0;
        if (activeYAxis != ImAxis_Y1)
            y1Flags |= ImPlotAxisFlags_NoGridLines;
        if (!useY1)
            y1Flags |= ImPlotAxisFlags_NoDecorations;
        applyBounds(ImAxis_Y1, 0, "Banking", y1Flags);

        const char* y2Label = isForced ? "Forces (g)" : "Yaw";
        const char* y3Label = isForced ? "Yaw" : "Forces (g)";

        ImPlotAxisFlags y2Flags = ImPlotAxisFlags_Opposite;
        if (activeYAxis != ImAxis_Y2)
            y2Flags |= ImPlotAxisFlags_NoGridLines;
        if (!useY2)
            y2Flags |= ImPlotAxisFlags_NoDecorations;
        applyBounds(ImAxis_Y2, 3, y2Label, y2Flags);

        ImPlotAxisFlags y3Flags = ImPlotAxisFlags_Opposite;
        if (activeYAxis != ImAxis_Y3)
            y3Flags |= ImPlotAxisFlags_NoGridLines;
        if (!useY3)
            y3Flags |= ImPlotAxisFlags_NoDecorations;
        applyBounds(ImAxis_Y3, 1, y3Label, y3Flags);

        ImPlotAxisFlags y4Flags = ImPlotAxisFlags_Opposite;
        if (activeYAxis != ImAxis_Y4)
            y4Flags |= ImPlotAxisFlags_NoGridLines;
        if (!useY4)
            y4Flags |= ImPlotAxisFlags_NoDecorations;
        applyBounds(ImAxis_Y4, 2, "Pitch", y4Flags);

        // 1. Draw Inactive Sections for Editable Graphs (Ghosted)
        auto plotInactiveEditable = [&](int editType) {
            if (!graphs[editType].visible)
                return;
            if ((editType == (int)GraphType::EditNormal || editType == (int)GraphType::EditLateral) && !isForced && !isGeo)
                return;

            int underType = getUnderlyingType(editType);
            ImPlot::SetAxis(getAxis(underType));
            for (size_t s = 0; s < graphs[underType].sections.size(); ++s) {
                const auto& secData = graphs[underType].sections[s];
                if (secData.isActive || secData.x.empty())
                    continue;

                ImPlotSpec spec;
                spec.LineColor = graphs[editType].color;
                spec.LineColor.w = 0.5f;
                spec.LineWeight = 1.0f;
                std::string label = "##" + graphs[editType].label + "_ghost_" + std::to_string(s);

                ImPlotSpec fillSpec = spec;
                fillSpec.FillColor = spec.LineColor;
                fillSpec.FillAlpha = spec.LineColor.w * 0.3f;
                fillSpec.Flags |= ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                ImPlot::PlotShaded(label.c_str(), secData.x.data(), secData.y.data(), (int)secData.x.size(), 0.0, fillSpec);
                ImPlot::PlotLine(label.c_str(), secData.x.data(), secData.y.data(), (int)secData.x.size(), spec);
                if (!secData.x.empty()) {
                    double xZero[2] = {secData.x.front(), secData.x.back()};
                    double yZero[2] = {0.0, 0.0};
                    ImPlotSpec zeroSpec = spec;
                    zeroSpec.LineWeight = 1.0f;
                    zeroSpec.LineColor.w *= 0.35f;
                    zeroSpec.Flags |= ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                    ImPlot::PlotLine("##ZeroLine", xZero, yZero, 2, zeroSpec);
                }
            }
        };
        plotInactiveEditable((int)GraphType::EditRoll);
        plotInactiveEditable((int)GraphType::EditNormal);
        plotInactiveEditable((int)GraphType::EditLateral);

        // 2. Draw Active Sections for Editable Graphs (Solid, thick line)
        for (const auto& eg : editableGraphs) {
            if (!eg.sf || !eg.sf->parent)
                continue;
            int editType = (int)GraphType::EditRoll;
            std::string dLabel = "Roll Speed";
            if (eg.sf->parent->type == funcPitch || eg.sf->parent->type == funcNormal) {
                editType = (int)GraphType::EditNormal;
                dLabel = isGeo ? "Pitch Change" : "Normal Force";
            } else if (eg.sf->parent->type == funcYaw || eg.sf->parent->type == funcLateral) {
                editType = (int)GraphType::EditLateral;
                dLabel = isGeo ? "Yaw Change" : "Lateral Force";
            }

            if (graphs[editType].visible) {
                if ((editType == (int)GraphType::EditNormal || editType == (int)GraphType::EditLateral) && !isForced && !isGeo)
                    continue;

                ImPlot::SetAxis(getAxis(getUnderlyingType(editType)));
                ImPlotSpec spec;
                spec.LineColor = eg.color;
                if (gloParent && gloParent->selectedFunc == eg.sf)
                    spec.LineWeight = 4.0f;
                else
                    spec.LineWeight = 2.0f;

                ImPlotSpec fillSpec = spec;
                fillSpec.FillColor = spec.LineColor;
                fillSpec.FillAlpha = spec.LineColor.w * 0.3f;
                fillSpec.Flags |= ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                ImPlot::PlotShaded(dLabel.c_str(), eg.x.data(), eg.y.data(), (int)eg.x.size(), 0.0, fillSpec);
                ImPlot::PlotLine(dLabel.c_str(), eg.x.data(), eg.y.data(), (int)eg.x.size(), spec);
                if (!eg.x.empty()) {
                    double xZero[2] = {eg.x.front(), eg.x.back()};
                    double yZero[2] = {0.0, 0.0};
                    ImPlotSpec zeroSpec = spec;
                    zeroSpec.LineWeight = 1.0f;
                    zeroSpec.LineColor.w *= 0.35f;
                    zeroSpec.Flags |= ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                    ImPlot::PlotLine("##ZeroLine", xZero, yZero, 2, zeroSpec);
                }
            }
        }

        // 3. Draw Resulting Graphs (Solid, thinner lines)
        // (Removed: Resulting graphs are now plotted in the "Resulting Graphs" tab instead of the "Graphs" tab)

        ImPlot::PushPlotClipRect();
        if (!gimbalLockRegions.empty()) {
            for (const auto& region : gimbalLockRegions) {
                ImVec2 p_min = ImPlot::PlotToPixels(region.first, ImPlot::GetPlotLimits().Y.Min);
                ImVec2 p_max = ImPlot::PlotToPixels(region.second, ImPlot::GetPlotLimits().Y.Max);
                ImPlot::GetPlotDrawList()->AddRectFilled(p_min, p_max, IM_COL32(255, 180, 50, 50));
            }
        }
        if (!radiusLimitRegions.empty()) {
            for (const auto& region : radiusLimitRegions) {
                ImVec2 p_min = ImPlot::PlotToPixels(region.first, ImPlot::GetPlotLimits().Y.Min);
                ImVec2 p_max = ImPlot::PlotToPixels(region.second, ImPlot::GetPlotLimits().Y.Max);
                ImPlot::GetPlotDrawList()->AddRectFilled(p_min, p_max, IM_COL32(255, 180, 50, 50));
            }
        }
        if (!forceLimitRegions.empty()) {
            for (const auto& region : forceLimitRegions) {
                ImVec2 p_min = ImPlot::PlotToPixels(region.first, ImPlot::GetPlotLimits().Y.Min);
                ImVec2 p_max = ImPlot::PlotToPixels(region.second, ImPlot::GetPlotLimits().Y.Max);
                ImPlot::GetPlotDrawList()->AddRectFilled(p_min, p_max, IM_COL32(255, 180, 50, 50));
            }
        }
        ImPlot::PopPlotClipRect();

        if (!sectionBoundaries.empty()) {
            ImPlot::SetAxis(ImAxis_Y1);
            ImPlotSpec vspec;
            vspec.LineColor = ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
            ImPlot::PlotInfLines("Sections", sectionBoundaries.data(), (int)sectionBoundaries.size(), vspec);
        }

        if (gViewport) {
            mnode* povNode = gViewport->getPOVNode();
            if (povNode) {
                double x = distArg ? povNode->fTotalLength : (double)gViewport->getPOVPos() / F_HZ;
                ImPlot::SetAxis(ImAxis_Y1);
                ImPlotSpec pspec;
                pspec.LineColor = playheadLocked ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : ((gloParent && gloParent->mOptions->theme == 1) ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                pspec.LineWeight = 2.0f;
                pspec.Flags = ImPlotInfLinesFlags_None; // Vertical is default
                ImPlot::PlotInfLines("POV", &x, 1, pspec);

                // Plot the black sphere at the intersection
                ImPlot::PushPlotClipRect();
                ImVec2 pt = ImPlot::PlotToPixels(x, 0.0, ImAxis_X1, ImAxis_Y1);
                // Draw a circle on the plot's X axis at Y=0
                ImDrawList* drawList = ImPlot::GetPlotDrawList();
                drawList->AddCircleFilled(pt, 4.0f, IM_COL32(0, 0, 0, 255));
                ImPlot::PopPlotClipRect();
            }
        }

        if (ImPlot::IsPlotHovered()) {
            if (gloParent && gloParent->mOptions->graphOverlayEnabled) {
                ImPlotPoint mouse_pt = ImPlot::GetPlotMousePos();
                int j = distArg ? hTrack->trackData->getIndexFromDist(mouse_pt.x) : (int)std::round(mouse_pt.x * F_HZ);
                j = std::clamp(j, 0, hTrack->trackData->getNumPoints());
                mnode* hoveredNode = hTrack->trackData->getPoint(j);
                if (hoveredNode) {
                    ImPlot::PushPlotClipRect();
                    double cx = distArg ? hoveredNode->fTotalLength : (double)j / F_HZ;
                    ImPlotSpec cxSpec;
                    cxSpec.LineColor = ImVec4(0.8f, 0.8f, 0.8f, 0.4f);
                    cxSpec.LineWeight = 1.0f;
                    ImPlot::PlotInfLines("##Crosshair", &cx, 1, cxSpec);
                    ImPlot::PopPlotClipRect();

                    const auto& pt = graphProcessor.getData();
                    if (j >= 0 && j < (int)pt.roll.size()) {
                        ImGui::BeginTooltip();
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f), "Node Index: %d", j);
                        ImGui::Separator();
                        ImGui::Text("Distance: %.2f m", hoveredNode->fTotalLength);
                        ImGui::Text("Time: %.3f s", (double)j / F_HZ);
                        ImGui::Text("Velocity: %.1f m/s (%.1f km/h)", hoveredNode->fVel, hoveredNode->fVel * 3.6);
                        ImGui::Text("Banking: %.1f deg", pt.roll[j]);
                        ImGui::Text("Roll Speed: %.1f deg/s", pt.rollSpeed[j]);
                        ImGui::Text("Normal Force: %.2f G", pt.forceNormal[j]);
                        ImGui::Text("Lateral Force: %.2f G", pt.forceLateral[j]);
                        ImGui::EndTooltip();
                    }
                }
            }

            if (ImGui::IsMouseClicked(0)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                subfunc* bestSf = nullptr;
                float minDist = 15.0f;
                for (const auto& eg : editableGraphs) {
                    if (!eg.sf || !eg.sf->parent)
                        continue;
                    int type = (int)GraphType::EditRoll;
                    if (eg.sf->parent->type == funcPitch || eg.sf->parent->type == funcNormal)
                        type = (int)GraphType::EditNormal;
                    else if (eg.sf->parent->type == funcYaw || eg.sf->parent->type == funcLateral)
                        type = (int)GraphType::EditLateral;

                    if (graphs[type].visible && eg.x.size() > 1) {
                        if ((type == (int)GraphType::EditNormal || type == (int)GraphType::EditLateral) && !isForced && !isGeo)
                            continue;

                        int yAxis = getAxis(getUnderlyingType(type));
                        for (size_t k = 0; k < eg.x.size() - 1; ++k) {
                            ImVec2 p1 = ImPlot::PlotToPixels(eg.x[k], eg.y[k], ImAxis_X1, yAxis);
                            ImVec2 p2 = ImPlot::PlotToPixels(eg.x[k + 1], eg.y[k + 1], ImAxis_X1, yAxis);
                            float dx = p2.x - p1.x, dy = p2.y - p1.y;
                            float l2 = dx * dx + dy * dy;
                            float d = (l2 == 0.0f) ? std::sqrt((mousePos.x - p1.x) * (mousePos.x - p1.x) + (mousePos.y - p1.y) * (mousePos.y - p1.y)) : std::sqrt((mousePos.x - (p1.x + std::max(0.0f, std::min(1.0f, ((mousePos.x - p1.x) * dx + (mousePos.y - p1.y) * dy) / l2)) * dx)) * (mousePos.x - (p1.x + std::max(0.0f, std::min(1.0f, ((mousePos.x - p1.x) * dx + (mousePos.y - p1.y) * dy) / l2)) * dx)) + (mousePos.y - (p1.y + std::max(0.0f, std::min(1.0f, ((mousePos.x - p1.x) * dx + (mousePos.y - p1.y) * dy) / l2)) * dy)) * (mousePos.y - (p1.y + std::max(0.0f, std::min(1.0f, ((mousePos.x - p1.x) * dx + (mousePos.y - p1.y) * dy) / l2)) * dy)));
                            if (d < minDist) {
                                minDist = d;
                                bestSf = eg.sf;
                            }
                        }
                    }
                }
                if (gloParent) {
                    if (gloParent->selectedFunc != bestSf) {
                        gloParent->selectedFunc = bestSf;
                        if (hTrack && hTrack->trackData) {
                            hTrack->trackData->requestUpdateTrack(0, 0);
                        }
                    }
                }
            }
        }

        ImPlot::EndPlot();
    }

    ImPlot::PopStyleColor(pushedTrans ? 4 : 2);
    if (autoScaleYEdit)
        ImPlot::PopStyleVar();
}

void GraphView::updateData(trackHandler* track) {
    sectionBoundaries.clear();
    if (track && track->trackData) {
        section* activeSec = track->trackData->activeSection;
        bool useDistance = (activeSec && activeSec->bArgument == 1);
        int currentPoints = 0;
        sectionBoundaries.push_back(0.0);
        for (section* sec : track->trackData->lSections) {
            currentPoints += std::max(0, (int)sec->lNodes.size() - 1);
            mnode* endNode = track->trackData->getPoint(currentPoints);
            if (endNode)
                sectionBoundaries.push_back(useDistance ? endNode->fTotalLength : (currentPoints / F_HZ));
            else
                sectionBoundaries.push_back(sectionBoundaries.back());
        }

        // Delegate raw data extraction, preparation passes, and Savitzky-Golay filtering to the processors!
        graphProcessor.update(track);
        measurementProcessor.update(track, graphProcessor);

        gimbalLockRegions.clear();
        radiusLimitRegions.clear();
        forceLimitRegions.clear();

        bool inGimbalLockRegion = false;
        double regionStart = 0.0;

        bool inRadiusRegion = false;
        double radiusRegionStart = 0.0;

        bool inForceRegion = false;
        double forceRegionStart = 0.0;

        int numPoints = track->trackData->getNumPoints();
        for (int i = 0; i <= numPoints; ++i) {
            mnode* node = track->trackData->getPoint(i);
            if (!node)
                continue;

            double x = useDistance ? node->fTotalLength : (i / F_HZ);

            // Gimbal lock
            if (node->nearGimbalLock && !inGimbalLockRegion) {
                inGimbalLockRegion = true;
                regionStart = x;
            } else if (!node->nearGimbalLock && inGimbalLockRegion) {
                inGimbalLockRegion = false;
                gimbalLockRegions.push_back({regionStart, x});
            }

            // Radius violations
            bool isRadiusViolated = false;
            if (track->trackData->enforceMinRadius) {
                float minRadius = track->trackData->minRadius;
                if (minRadius > 0.0f) {
                    mnode* prevNode = (i > 0) ? track->trackData->getPoint(i - 1) : node;
                    double dotProd = glm::dot(node->vDir, prevNode->vDir);
                    double deltaThetaRad = acos(glm::clamp(dotProd, -1.0, 1.0));
                    double ds = node->fDistFromLast;
                    if (deltaThetaRad > 1e-7) {
                        double physicalRadius = ds / deltaThetaRad;
                        if (physicalRadius < minRadius) {
                            isRadiusViolated = true;
                        }
                    }
                }
            }

            if (isRadiusViolated && !inRadiusRegion) {
                inRadiusRegion = true;
                radiusRegionStart = x;
            } else if (!isRadiusViolated && inRadiusRegion) {
                inRadiusRegion = false;
                radiusLimitRegions.push_back({radiusRegionStart, x});
            }

            // Force violations
            bool isForceViolated = false;
            if (track->trackData->enableForceLimits) {
                if (node->forceNormal > track->trackData->fMaxPosNormal ||
                    node->forceNormal < track->trackData->fMaxNegNormal ||
                    node->forceLateral > track->trackData->fMaxLateral ||
                    node->forceLateral < track->trackData->fMinLateral) {
                    isForceViolated = true;
                }
            }

            if (isForceViolated && !inForceRegion) {
                inForceRegion = true;
                forceRegionStart = x;
            } else if (!isForceViolated && inForceRegion) {
                inForceRegion = false;
                forceLimitRegions.push_back({forceRegionStart, x});
            }
        }

        double x_end = useDistance ? track->trackData->getTotalLength() : (numPoints / F_HZ);
        if (inGimbalLockRegion) {
            gimbalLockRegions.push_back({regionStart, x_end});
        }
        if (inRadiusRegion) {
            radiusLimitRegions.push_back({radiusRegionStart, x_end});
        }
        if (inForceRegion) {
            forceLimitRegions.push_back({forceRegionStart, x_end});
        }
    }
    for (int i = (int)GraphType::Banking; i < (int)GraphType::Count; i++)
        sampleGraph(track, (GraphType)i);
    sampleEditable(track);
}

void GraphView::sampleEditable(trackHandler* hTrack) {
    editableGraphs.clear();
    if (!hTrack || !hTrack->trackData || !hTrack->trackData->activeSection)
        return;
    section* sec = hTrack->trackData->activeSection;
    if (sec->type == anchor)
        return;
    bool useDistance = (sec->bArgument == 1);
    int startIdx = hTrack->trackData->getNumPoints(sec);
    int numNodes = (int)sec->lNodes.size();

    auto getNodeArg = [&](int k) -> double {
        if (sec->type == curved) {
            return (double)static_cast<seccurved*>(sec)->getAngles()[k];
        } else if (useDistance) {
            if (sec->type == geometric || sec->type == geometricriderlocal || sec->type == forced) {
                double h_offset = sec->lNodes[k].fTotalHeartLength - sec->lNodes[0].fTotalHeartLength;
                double h_total = sec->lNodes.back().fTotalHeartLength - sec->lNodes[0].fTotalHeartLength;
                double r_total = sec->length;
                double dist_projected = h_offset;
                if (h_total > 1e-6) {
                    dist_projected = h_offset * (r_total / h_total);
                }
                return dist_projected;
            } else {
                return sec->lNodes[k].fTotalHeartLength - sec->lNodes[0].fTotalHeartLength;
            }
        } else {
            return (double)k / F_HZ;
        }
    };

    double xOffset = 0;
    mnode* startNode = hTrack->trackData->getPoint(startIdx);
    if (startNode) {
        xOffset = useDistance ? startNode->fTotalLength : ((double)startIdx / F_HZ);
    }

    auto sampleFunc = [&](func* f, GraphType type) {
        if (!f)
            return;
        int prevIdx = -1;
        for (subfunc* sf : f->funcList) {
            double minArg = sf->minArgument, maxArg = sf->maxArgument;
            if (maxArg <= minArg)
                continue;
            EditableGraphData eData;
            eData.sf = sf;
            eData.color = graphs[(int)type].color;
            eData.label = graphs[(int)type].label + " (Editable)##" + std::to_string((uintptr_t)sf);

            for (int k = 0; k < numNodes; ++k) {
                double t = getNodeArg(k);
                if (t >= minArg - 1e-4 && t <= maxArg + 1e-4) {
                    double x = useDistance ? sec->lNodes[k].fTotalLength : ((startIdx + k) / F_HZ);
                    double y = sf->getValue(t, true);

                    if (std::abs(y) < 1e-4)
                        y = 0.0;
                    eData.x.push_back(x);
                    eData.y.push_back(y);
                    eData.nodes.push_back(startIdx + k);
                }
            }

            // Append the connecting node to ensure the curve meets the next transition or section end cleanly
            if (!eData.x.empty()) {
                int lastNodeLocal = eData.nodes.back() - startIdx;
                if (lastNodeLocal + 1 < numNodes) {
                    double tNext = getNodeArg(lastNodeLocal + 1);
                    double xNext = useDistance ? sec->lNodes[lastNodeLocal + 1].fTotalLength : ((startIdx + lastNodeLocal + 1) / F_HZ);
                    double tNextClamped = std::clamp(tNext, minArg, maxArg);
                    double yNext = sf->getValue(tNextClamped, true);
                    if (std::abs(yNext) < 1e-4)
                        yNext = 0.0;
                    eData.x.push_back(xNext);
                    eData.y.push_back(yNext);
                    eData.nodes.push_back(startIdx + lastNodeLocal + 1);
                }
            }

            if (!eData.x.empty()) {
                editableGraphs.push_back(eData);
                prevIdx = (int)editableGraphs.size() - 1;
            }

            // If the transition extends past the simulated section length, plot the remaining part as a faint ghost line
            if (numNodes > 1 && !(sec->type == straight || sec->type == curved || sf->degree == tozero)) {
                double simulatedEndArg = getNodeArg(numNodes - 1);
                if (maxArg > simulatedEndArg + 1e-4) {
                    EditableGraphData dashedData;
                    dashedData.sf = sf;
                    dashedData.color = graphs[(int)type].color;
                    dashedData.color.w *= 0.40f; // Faint ghost alpha
                    dashedData.label = "##" + graphs[(int)type].label + " (Ghost)##" + std::to_string((uintptr_t)sf);

                    int n = 30;
                    double startT = simulatedEndArg;
                    double step = (maxArg - startT) / (float)n;
                    for (int i = 0; i <= n; ++i) {
                        double t = startT + i * step;
                        if (t > maxArg)
                            t = maxArg;
                        double y = sf->getValue(t, true);
                        if (std::abs(y) < 1e-4)
                            y = 0.0;

                        double x = xOffset + t;
                        dashedData.x.push_back(x);
                        dashedData.y.push_back(y);
                        dashedData.nodes.push_back(startIdx + numNodes - 1);
                    }
                    editableGraphs.push_back(dashedData);
                }
            }
        }
    };
    sampleFunc(sec->rollFunc, GraphType::EditRoll);
    if (sec->type == forced || sec->type == geometric || sec->type == geometricriderlocal) {
        sampleFunc(sec->normForce, GraphType::EditNormal);
        sampleFunc(sec->latForce, GraphType::EditLateral);
    }
}

void GraphView::sampleGraph(trackHandler* hTrack, GraphType type) {
    const auto& processedTrack = graphProcessor.getData();
    GraphData& data = graphs[(int)type];
    data.sections.clear();
    if (!hTrack || !hTrack->trackData || hTrack->trackData->lSections.empty())
        return;
    section* activeSec = hTrack->trackData->activeSection;
    bool useDistance = (activeSec && activeSec->bArgument == 1);
    int currentPoints = 0;
    for (section* sec : hTrack->trackData->lSections) {
        SectionGraphData sData;
        sData.isActive = (sec == activeSec);
        int numNodes = (int)sec->lNodes.size();
        if (numNodes <= 1) {
            currentPoints += std::max(0, numNodes - 1);
            continue;
        }

        double last_dist = -999.0;
        double spacing_limit = gloParent ? (double)gloParent->mOptions->graphSpacingLimit : 0.1;

        for (int k = 0; k < numNodes; ++k) {
            int j = currentPoints + k;
            mnode* curNode = hTrack->trackData->getPoint(j);
            if (!curNode)
                continue;

            double cur_dist = curNode->fTotalLength;
            if (k == 0 || k == numNodes - 1 || (cur_dist - last_dist >= spacing_limit)) {
                last_dist = cur_dist;

                double x = 0.0;
                double y = 0.0;
                if (j >= 0 && j < (int)processedTrack.roll.size()) {
                    x = useDistance ? processedTrack.xDistance[j] : processedTrack.xTime[j];
                    switch (type) {
                    case GraphType::Banking:
                        y = processedTrack.roll[j];
                        break;
                    case GraphType::RollSpeed:
                        y = processedTrack.rollSpeed[j];
                        break;
                    case GraphType::RollAccel:
                        y = processedTrack.rollAccel[j] * F_HZ;
                        break;
                    case GraphType::NForce:
                        y = processedTrack.forceNormal[j];
                        break;
                    case GraphType::NForceChange:
                        y = processedTrack.forceNormalChange[j] * F_HZ;
                        break;
                    case GraphType::LForce:
                        y = processedTrack.forceLateral[j];
                        break;
                    case GraphType::LForceChange:
                        y = processedTrack.forceLateralChange[j] * F_HZ;
                        break;
                    case GraphType::PitchChange: {
                        bool isRiderLocal = activeSec && activeSec->type == geometricriderlocal;
                        double raw_pitch = (sec == activeSec || isRiderLocal) ? processedTrack.pitchChange[j] : processedTrack.worldPitchChange[j];
                        y = raw_pitch / (useDistance ? std::max(0.01, processedTrack.vel[j]) : 1.0);
                    } break;
                    case GraphType::YawChange: {
                        bool isRiderLocal = activeSec && activeSec->type == geometricriderlocal;
                        double raw_yaw = (sec == activeSec || isRiderLocal) ? processedTrack.yawChange[j] : processedTrack.worldYawChange[j];
                        y = raw_yaw / (useDistance ? std::max(0.01, processedTrack.vel[j]) : 1.0);
                    } break;
                    case GraphType::Velocity:
                        y = processedTrack.vel[j];
                        break;
                    case GraphType::WorldPitch:
                        y = processedTrack.worldPitch[j];
                        break;
                    case GraphType::WorldPitchChange:
                        y = processedTrack.worldPitchChange[j] / (useDistance ? std::max(0.01, processedTrack.vel[j]) : 1.0);
                        break;
                    case GraphType::WorldYawChange:
                        y = processedTrack.worldYawChange[j] / (useDistance ? std::max(0.01, processedTrack.vel[j]) : 1.0);
                        break;
                    default:
                        break;
                    }
                }
                if (std::abs(y) < 1e-4)
                    y = 0.0;
                sData.x.push_back(x);
                sData.y.push_back(y);
            }
        }

        data.sections.push_back(sData);
        currentPoints += numNodes - 1;
    }
}
void GraphView::renderResultingPlot(trackHandler* hTrack) {
    if (!hTrack || !hTrack->trackData)
        return;
    section* activeSec = hTrack->trackData->activeSection;
    bool isForced = activeSec && activeSec->type == forced;
    renderTimeline(hTrack);

    ImGui::Checkbox("Auto-Scale Y", &autoScaleYResult);
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        ImPlot::SetNextAxesToFit();
    }
    ImGui::SameLine();
    bool canLock = playheadLocked || (gloParent && gloParent->selectedFunc != nullptr);
    if (!canLock)
        ImGui::BeginDisabled();
    std::string lockLabel = playheadLocked ? "Unlock Playhead" : "Lock Playhead";
    if (ImGui::Button(lockLabel.c_str())) {
        playheadLocked = !playheadLocked;
        if (playheadLocked) {
            lockPlayheadToCurrentTransition(hTrack);
        } else {
            lockedTransition = nullptr;
        }
    }
    if (!canLock)
        ImGui::EndDisabled();
    if (!playheadLocked && !canLock) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Select a transition in the graphs or transition list to lock the playhead to.");
        }
    }
    if (!autoScaleYResult) {
        ImGui::SameLine();
        ImGui::TextDisabled("|  Tip: Hover over any active Y-axis on the sides and use your scroll wheel to zoom, or drag to pan.");
    }

    bool distArg = hTrack->trackData->activeSection && hTrack->trackData->activeSection->bArgument == 1;
    bool isStraight = hTrack->trackData->activeSection && hTrack->trackData->activeSection->type == straight;
    bool isCurved = hTrack->trackData->activeSection && hTrack->trackData->activeSection->type == curved;
    if (isStraight || isCurved)
        distArg = false;

    std::string xLabel = distArg ? ("Distance (" + gloParent->mOptions->getLengthString() + ")") : "Time (s)";

    bool pushedTrans = gloParent->mOptions->transparentGraphs;
    if (pushedTrans) {
        ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, ImVec4(0, 0, 0, 0));
        ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, ImVec4(0, 0, 0, 0));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    }

    if (ImPlot::BeginPlot("##ResultingGraphs", ImVec2(-1, -1), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        auto isSectionVisibleInViewport = [&](const auto& sec) {
            if (sec.x.empty())
                return false;
            if (linkedXMin >= linkedXMax)
                return true; // first frame default
            double secStart = sec.x.front();
            double secEnd = sec.x.back();
            return (secEnd >= linkedXMin && secStart <= linkedXMax);
        };

        bool useY[4] = {false, false, false, false}; // 0:Y1(Banking), 1:Y3(Forces), 2:Y4(Pitch), 3:Y2(Yaw)
        double yMin[4] = {100000, 100000, 100000, 100000};
        double yMax[4] = {-100000, -100000, -100000, -100000};

        auto getA = [&](int ax) {
            if (ax == (int)ImAxis_Y1)
                return 0;
            if (ax == (int)ImAxis_Y3)
                return 1;
            if (ax == (int)ImAxis_Y4)
                return 2;
            if (ax == (int)ImAxis_Y2)
                return 3;
            return 0;
        };

        auto getAxis = [&](int type) {
            if (type == (int)GraphType::YawChange || type == (int)GraphType::WorldYawChange)
                return (int)ImAxis_Y2;
            if (type == (int)GraphType::PitchChange || type == (int)GraphType::WorldPitchChange)
                return (int)ImAxis_Y4;
            if (type == (int)GraphType::Banking || type == (int)GraphType::RollSpeed || type == (int)GraphType::RollAccel)
                return (int)ImAxis_Y1;
            if (type == (int)GraphType::NForce || type == (int)GraphType::NForceChange || type == (int)GraphType::LForce || type == (int)GraphType::LForceChange)
                return isForced ? (int)ImAxis_Y2 : (int)ImAxis_Y3;
            return (int)ImAxis_Y1;
        };

        auto updateBounds = [&](int ax, double val) {
            int a = getA(ax);
            if (val < yMin[a])
                yMin[a] = val;
            if (val > yMax[a])
                yMax[a] = val;
        };

        ImVec4 axisColors[4] = {ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1)};

        for (int i = (int)GraphType::Banking; i < (int)GraphType::Count; i++) {
            if (graphs[i].visible) {
                int ax = getAxis(i);
                int a = getA(ax);
                if (!useY[a]) { // Use the color of the first visible graph for this axis
                    axisColors[a] = graphs[i].color;
                    axisColors[a].w = 1.0f;
                }
                useY[a] = true;
                for (const auto& sec : graphs[i].sections) {
                    if (sec.x.empty())
                        continue;
                    if (!sec.isActive && !isSectionVisibleInViewport(sec))
                        continue;
                    for (double val : sec.y) {
                        updateBounds(ax, val);
                    }
                }
            }
        }

        bool useY1 = useY[0];
        bool useY3 = useY[1];
        bool useY4 = useY[2];
        bool useY2 = useY[3];

        ImPlot::SetupAxis(ImAxis_X1, nullptr);
        ImPlot::SetupAxisLinks(ImAxis_X1, &linkedXMin, &linkedXMax);

        auto applyBounds = [&](int ax, int a, const char* label, ImPlotAxisFlags flags) {
            if (useY[a]) {
                ImPlot::PushStyleColor(ImPlotCol_AxisText, axisColors[a]);
            }
            ImPlot::SetupAxis(ax, nullptr, flags);
            if (useY[a]) {
                ImPlot::PopStyleColor();
            }
            if (useY[a]) {
                if (yMin[a] <= yMax[a]) {
                    double range = yMax[a] - yMin[a];
                    if (range < 0.1)
                        range = 0.1;
                    double minLim = yMin[a] - range * 0.1;
                    double maxLim = yMax[a] + range * 0.1;
                    ImPlot::SetupAxisLimits(ax, minLim, maxLim, autoScaleYResult ? ImGuiCond_Always : ImGuiCond_Appearing);
                    if (ax == ImAxis_Y2 || ax == ImAxis_Y3 || ax == ImAxis_Y4) {
                        const char* const emptyLabels[] = {"", ""};
                        ImPlot::SetupAxisTicks(ax, minLim, maxLim, 2, emptyLabels, true);
                    }
                }
            }
        };

        const char* y2Label = isForced ? "Forces (g)" : "Yaw";
        const char* y3Label = isForced ? "Yaw" : "Forces (g)";

        ImPlotAxisFlags y1Flags = useY1 ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoDecorations;
        ImPlotAxisFlags y2Flags = ImPlotAxisFlags_Opposite | (useY2 ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoDecorations);
        ImPlotAxisFlags y3Flags = ImPlotAxisFlags_Opposite | (useY3 ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoDecorations);
        ImPlotAxisFlags y4Flags = ImPlotAxisFlags_Opposite | (useY4 ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoDecorations);

        applyBounds(ImAxis_Y1, 0, "Banking", y1Flags);
        applyBounds(ImAxis_Y2, 3, y2Label, y2Flags);
        applyBounds(ImAxis_Y3, 1, y3Label, y3Flags);
        applyBounds(ImAxis_Y4, 2, "Pitch", y4Flags);

        for (int i = (int)GraphType::Banking; i < (int)GraphType::Count; i++) {
            if (graphs[i].visible) {
                ImPlot::SetAxis(getAxis(i));
                bool firstVisible = true;

                for (size_t s = 0; s < graphs[i].sections.size(); ++s) {
                    const auto& secData = graphs[i].sections[s];
                    if (secData.x.empty())
                        continue;

                    ImPlotSpec spec;
                    spec.LineColor = graphs[i].color;
                    spec.LineWeight = secData.isActive ? 2.0f : 1.0f;
                    spec.LineColor.w = secData.isActive ? 0.9f : 0.4f;

                    std::string label = graphs[i].label;
                    if (!firstVisible)
                        label = "##" + label + "_" + std::to_string(s);
                    firstVisible = false;

                    ImPlot::PlotLine(label.c_str(), secData.x.data(), secData.y.data(), (int)secData.x.size(), spec);
                    if (!secData.x.empty()) {
                        double xZero[2] = {secData.x.front(), secData.x.back()};
                        double yZero[2] = {0.0, 0.0};
                        ImPlotSpec zeroSpec = spec;
                        zeroSpec.LineWeight = 1.0f;
                        zeroSpec.LineColor.w *= 0.35f;
                        zeroSpec.Flags |= ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                        ImPlot::PlotLine("##ZeroLine", xZero, yZero, 2, zeroSpec);
                    }
                }
            }
        }

        ImPlot::PushPlotClipRect();
        if (!gimbalLockRegions.empty()) {
            for (const auto& region : gimbalLockRegions) {
                ImVec2 p_min = ImPlot::PlotToPixels(region.first, ImPlot::GetPlotLimits().Y.Min);
                ImVec2 p_max = ImPlot::PlotToPixels(region.second, ImPlot::GetPlotLimits().Y.Max);
                ImPlot::GetPlotDrawList()->AddRectFilled(p_min, p_max, IM_COL32(255, 180, 50, 50));
            }
        }
        if (!radiusLimitRegions.empty()) {
            for (const auto& region : radiusLimitRegions) {
                ImVec2 p_min = ImPlot::PlotToPixels(region.first, ImPlot::GetPlotLimits().Y.Min);
                ImVec2 p_max = ImPlot::PlotToPixels(region.second, ImPlot::GetPlotLimits().Y.Max);
                ImPlot::GetPlotDrawList()->AddRectFilled(p_min, p_max, IM_COL32(255, 180, 50, 50));
            }
        }
        if (!forceLimitRegions.empty()) {
            for (const auto& region : forceLimitRegions) {
                ImVec2 p_min = ImPlot::PlotToPixels(region.first, ImPlot::GetPlotLimits().Y.Min);
                ImVec2 p_max = ImPlot::PlotToPixels(region.second, ImPlot::GetPlotLimits().Y.Max);
                ImPlot::GetPlotDrawList()->AddRectFilled(p_min, p_max, IM_COL32(255, 180, 50, 50));
            }
        }
        ImPlot::PopPlotClipRect();

        if (!sectionBoundaries.empty()) {
            ImPlot::SetAxis(ImAxis_Y1);
            ImPlotSpec vspec;
            vspec.LineColor = ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
            ImPlot::PlotInfLines("Sections", sectionBoundaries.data(), (int)sectionBoundaries.size(), vspec);
        }

        if (gViewport) {
            mnode* povNode = gViewport->getPOVNode();
            if (povNode) {
                double x = distArg ? povNode->fTotalLength : (double)gViewport->getPOVPos() / F_HZ;
                ImPlot::SetAxis(ImAxis_Y1);
                ImPlotSpec pspec;
                pspec.LineColor = playheadLocked ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : ((gloParent && gloParent->mOptions->theme == 1) ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                pspec.LineWeight = 2.0f;
                pspec.Flags = ImPlotInfLinesFlags_None;
                ImPlot::PlotInfLines("POV", &x, 1, pspec);

                ImPlot::PushPlotClipRect();
                ImVec2 pt = ImPlot::PlotToPixels(x, 0.0, ImAxis_X1, ImAxis_Y1);
                ImDrawList* drawList = ImPlot::GetPlotDrawList();
                drawList->AddCircleFilled(pt, 4.0f, IM_COL32(0, 0, 0, 255));
                ImPlot::PopPlotClipRect();
            }
        }

        if (ImPlot::IsPlotHovered()) {
            if (gloParent && gloParent->mOptions->graphOverlayEnabled) {
                ImPlotPoint mouse_pt = ImPlot::GetPlotMousePos();
                int j = distArg ? hTrack->trackData->getIndexFromDist(mouse_pt.x) : (int)std::round(mouse_pt.x * F_HZ);
                j = std::clamp(j, 0, hTrack->trackData->getNumPoints());
                mnode* hoveredNode = hTrack->trackData->getPoint(j);
                if (hoveredNode) {
                    ImPlot::PushPlotClipRect();
                    double cx = distArg ? hoveredNode->fTotalLength : (double)j / F_HZ;
                    ImPlotSpec cxSpec;
                    cxSpec.LineColor = ImVec4(0.8f, 0.8f, 0.8f, 0.4f);
                    cxSpec.LineWeight = 1.0f;
                    ImPlot::PlotInfLines("##Crosshair", &cx, 1, cxSpec);
                    ImPlot::PopPlotClipRect();

                    const auto& pt = graphProcessor.getData();
                    if (j >= 0 && j < (int)pt.roll.size()) {
                        ImGui::BeginTooltip();
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f), "Node Index: %d", j);
                        ImGui::Separator();
                        ImGui::Text("Distance: %.2f m", hoveredNode->fTotalLength);
                        ImGui::Text("Time: %.3f s", (double)j / F_HZ);
                        ImGui::Text("Velocity: %.1f m/s (%.1f km/h)", hoveredNode->fVel, hoveredNode->fVel * 3.6);
                        ImGui::Text("Banking: %.1f deg", pt.roll[j]);
                        ImGui::Text("Roll Speed: %.1f deg/s", pt.rollSpeed[j]);
                        ImGui::Text("Normal Force: %.2f G", pt.forceNormal[j]);
                        ImGui::Text("Lateral Force: %.2f G", pt.forceLateral[j]);
                        ImGui::EndTooltip();
                    }
                }
            }
        }

        ImPlot::EndPlot();
    }
    if (pushedTrans) {
        ImPlot::PopStyleColor(4);
    }
}

bool GraphView::hasMeasurementGraphsVisible(trackHandler* hTrack) const {
    if (!hTrack || !hTrack->trackData)
        return false;
    for (const auto& offset : hTrack->trackData->trainOffsets) {
        if (offset.showNormal || offset.showLateral)
            return true;
    }
    return false;
}

void GraphView::renderMeasurementPlot(trackHandler* hTrack) {
    if (!hTrack || !hTrack->trackData)
        return;
    renderTimeline(hTrack);

    ImGui::Checkbox("Auto-Scale Y", &autoScaleYMeasure);
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        ImPlot::SetNextAxesToFit();
    }
    ImGui::SameLine();
    bool canLock = playheadLocked || (gloParent && gloParent->selectedFunc != nullptr);
    if (!canLock)
        ImGui::BeginDisabled();
    std::string lockLabel = playheadLocked ? "Unlock Playhead" : "Lock Playhead";
    if (ImGui::Button(lockLabel.c_str())) {
        playheadLocked = !playheadLocked;
        if (playheadLocked) {
            lockPlayheadToCurrentTransition(hTrack);
        } else {
            lockedTransition = nullptr;
        }
    }
    if (!canLock)
        ImGui::EndDisabled();
    if (!playheadLocked && !canLock) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Select a transition in the graphs or transition list to lock the playhead to.");
        }
    }
    if (!autoScaleYMeasure) {
        ImGui::SameLine();
        ImGui::TextDisabled("|  Tip: Hover over any active Y-axis on the sides and use your scroll wheel to zoom, or drag to pan.");
    }

    const auto& measurementData = measurementProcessor.getData();
    size_t numOffsetsGraph = std::min(measurementData.size(), hTrack->trackData->trainOffsets.size());
    if (numOffsetsGraph == 0)
        return;

    bool distArg = hTrack->trackData->activeSection && hTrack->trackData->activeSection->bArgument == 1;
    bool isStraight = hTrack->trackData->activeSection && hTrack->trackData->activeSection->type == straight;
    bool isCurved = hTrack->trackData->activeSection && hTrack->trackData->activeSection->type == curved;
    if (isStraight || isCurved)
        distArg = false;

    std::string xLabel = distArg ? ("Distance (" + gloParent->mOptions->getLengthString() + ")") : "Time (s)";

    bool showAnyNormal = false;
    bool showAnyLateral = false;
    for (size_t i = 0; i < numOffsetsGraph; ++i) {
        if (hTrack->trackData->trainOffsets[i].showNormal)
            showAnyNormal = true;
        if (hTrack->trackData->trainOffsets[i].showLateral)
            showAnyLateral = true;
    }

    int rows = 0;
    if (showAnyNormal)
        rows++;
    if (showAnyLateral)
        rows++;
    if (rows == 0)
        return;

    bool pushedTrans = gloParent->mOptions->transparentGraphs;
    if (pushedTrans) {
        ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, ImVec4(0, 0, 0, 0));
        ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, ImVec4(0, 0, 0, 0));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    }

    if (ImPlot::BeginSubplots("##MeasurementSubplots", rows, 1, ImVec2(-1, -1), ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoTitle)) {
        if (showAnyNormal) {
            if (ImPlot::BeginPlot("##NormalSubplot", ImVec2(-1, 0), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
                if (showAnyLateral) {
                    ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoTickLabels);
                } else {
                    ImPlot::SetupAxis(ImAxis_X1, nullptr);
                }
                ImPlot::SetupAxisLinks(ImAxis_X1, &linkedXMin, &linkedXMax);
                ImPlot::SetupAxis(ImAxis_Y1, nullptr);

                if (true) {
                    double minBound = 1e9, maxBound = -1e9;
                    for (size_t i = 0; i < numOffsetsGraph; ++i) {
                        if (hTrack->trackData->trainOffsets[i].showNormal) {
                            for (double val : measurementData[i].yNormal) {
                                if (val < minBound)
                                    minBound = val;
                                if (val > maxBound)
                                    maxBound = val;
                            }
                        }
                    }
                    if (minBound <= maxBound) {
                        double range = maxBound - minBound;
                        if (range < 0.1)
                            range = 0.1;
                        ImPlot::SetupAxisLimits(ImAxis_Y1, minBound - range * 0.1, maxBound + range * 0.1, autoScaleYMeasure ? ImGuiCond_Always : ImGuiCond_Appearing);
                    }
                }

                for (size_t i = 0; i < numOffsetsGraph; ++i) {
                    ImPlot::SetAxis(ImAxis_Y1);
                    ImPlotSpec spec;
                    spec.LineWeight = 1.0f;
                    const auto& offset = hTrack->trackData->trainOffsets[i];
                    spec.LineColor = ImVec4(offset.color.x, offset.color.y, offset.color.z, 0.8f);

                    if (hTrack->trackData->forceHighlightMode == 1) {
                        if ((int)i == hTrack->trackData->maxNormalOffsetIdx) {
                            spec.LineWeight = 3.0f;
                            spec.LineColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                        } else if ((int)i == hTrack->trackData->minNormalOffsetIdx) {
                            spec.LineWeight = 3.0f;
                            spec.LineColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
                        } else {
                            spec.LineColor.w = 0.2f;
                        }
                    }

                    char nbuf[128];
                    snprintf(nbuf, sizeof(nbuf), "%s (N)", offset.name);

                    if (offset.showNormal && !measurementData[i].x.empty()) {
                        ImPlot::PlotLine(nbuf, measurementData[i].x.data(), measurementData[i].yNormal.data(), measurementData[i].x.size(), spec);
                        if (!measurementData[i].x.empty()) {
                            double xZero[2] = {measurementData[i].x.front(), measurementData[i].x.back()};
                            double yZero[2] = {0.0, 0.0};
                            ImPlotSpec zeroSpec = spec;
                            zeroSpec.LineWeight = 1.0f;
                            zeroSpec.LineColor.w *= 0.35f;
                            zeroSpec.Flags |= ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                            ImPlot::PlotLine("##ZeroLine", xZero, yZero, 2, zeroSpec);
                        }
                    }
                }

                if (gViewport) {
                    mnode* povNode = gViewport->getPOVNode();
                    if (povNode) {
                        double x = distArg ? povNode->fTotalLength : (double)gViewport->getPOVPos() / F_HZ;
                        ImPlot::SetAxis(ImAxis_Y1);
                        ImPlotSpec pspec;
                        pspec.LineColor = playheadLocked ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : ((gloParent && gloParent->mOptions->theme == 1) ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                        pspec.LineWeight = 2.0f;
                        pspec.Flags = ImPlotInfLinesFlags_None;
                        ImPlot::PlotInfLines("POV", &x, 1, pspec);

                        ImPlot::PushPlotClipRect();
                        ImVec2 pt = ImPlot::PlotToPixels(x, 0.0, ImAxis_X1, ImAxis_Y1);
                        ImDrawList* drawList = ImPlot::GetPlotDrawList();
                        drawList->AddCircleFilled(pt, 4.0f, IM_COL32(0, 0, 0, 255));
                        ImPlot::PopPlotClipRect();
                    }
                }

                if (!sectionBoundaries.empty()) {
                    ImPlot::SetAxis(ImAxis_Y1);
                    ImPlotSpec vspec;
                    vspec.LineColor = ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
                    ImPlot::PlotInfLines("Sections", sectionBoundaries.data(), (int)sectionBoundaries.size(), vspec);
                }

                ImVec2 plotPos = ImPlot::GetPlotPos();
                ImVec4 labelCol = ImGui::GetStyle().Colors[ImGuiCol_Text];
                labelCol.w *= 0.7f;
                ImPlot::GetPlotDrawList()->AddText(ImVec2(plotPos.x + 10.0f, plotPos.y + 10.0f), ImGui::GetColorU32(labelCol), "Normal Force");

                ImPlot::EndPlot();
            }
        }

        if (showAnyLateral) {
            if (ImPlot::BeginPlot("##LateralSubplot", ImVec2(-1, 0), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
                ImPlot::SetupAxis(ImAxis_X1, nullptr);
                ImPlot::SetupAxisLinks(ImAxis_X1, &linkedXMin, &linkedXMax);
                ImPlot::SetupAxis(ImAxis_Y1, nullptr);

                if (true) {
                    double minBound = 1e9, maxBound = -1e9;
                    for (size_t i = 0; i < numOffsetsGraph; ++i) {
                        if (hTrack->trackData->trainOffsets[i].showLateral) {
                            for (double val : measurementData[i].yLateral) {
                                if (val < minBound)
                                    minBound = val;
                                if (val > maxBound)
                                    maxBound = val;
                            }
                        }
                    }
                    if (minBound <= maxBound) {
                        double range = maxBound - minBound;
                        if (range < 0.1)
                            range = 0.1;
                        ImPlot::SetupAxisLimits(ImAxis_Y1, minBound - range * 0.1, maxBound + range * 0.1, autoScaleYMeasure ? ImGuiCond_Always : ImGuiCond_Appearing);
                    }
                }

                for (size_t i = 0; i < numOffsetsGraph; ++i) {
                    ImPlot::SetAxis(ImAxis_Y1);
                    ImPlotSpec spec;
                    spec.LineWeight = 1.0f;
                    const auto& offset = hTrack->trackData->trainOffsets[i];
                    spec.LineColor = ImVec4(offset.color.x, offset.color.y, offset.color.z, 0.8f);

                    if (hTrack->trackData->forceHighlightMode == 2) {
                        if ((int)i == hTrack->trackData->maxLateralOffsetIdx) {
                            spec.LineWeight = 3.0f;
                            spec.LineColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                        } else if ((int)i == hTrack->trackData->minLateralOffsetIdx) {
                            spec.LineWeight = 3.0f;
                            spec.LineColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
                        } else {
                            spec.LineColor.w = 0.2f;
                        }
                    }

                    char lbuf[128];
                    snprintf(lbuf, sizeof(lbuf), "%s (L)", offset.name);

                    if (offset.showLateral && !measurementData[i].x.empty()) {
                        ImPlot::PlotLine(lbuf, measurementData[i].x.data(), measurementData[i].yLateral.data(), measurementData[i].x.size(), spec);
                        if (!measurementData[i].x.empty()) {
                            double xZero[2] = {measurementData[i].x.front(), measurementData[i].x.back()};
                            double yZero[2] = {0.0, 0.0};
                            ImPlotSpec zeroSpec = spec;
                            zeroSpec.LineWeight = 1.0f;
                            zeroSpec.LineColor.w *= 0.35f;
                            zeroSpec.Flags |= ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
                            ImPlot::PlotLine("##ZeroLine", xZero, yZero, 2, zeroSpec);
                        }
                    }
                }

                if (gViewport) {
                    mnode* povNode = gViewport->getPOVNode();
                    if (povNode) {
                        double x = distArg ? povNode->fTotalLength : (double)gViewport->getPOVPos() / F_HZ;
                        ImPlot::SetAxis(ImAxis_Y1);
                        ImPlotSpec pspec;
                        pspec.LineColor = playheadLocked ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : ((gloParent && gloParent->mOptions->theme == 1) ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                        pspec.LineWeight = 2.0f;
                        pspec.Flags = ImPlotInfLinesFlags_None;
                        ImPlot::PlotInfLines("POV", &x, 1, pspec);

                        ImPlot::PushPlotClipRect();
                        ImVec2 pt = ImPlot::PlotToPixels(x, 0.0, ImAxis_X1, ImAxis_Y1);
                        ImDrawList* drawList = ImPlot::GetPlotDrawList();
                        drawList->AddCircleFilled(pt, 4.0f, IM_COL32(0, 0, 0, 255));
                        ImPlot::PopPlotClipRect();
                    }
                }

                if (!sectionBoundaries.empty()) {
                    ImPlot::SetAxis(ImAxis_Y1);
                    ImPlotSpec vspec;
                    vspec.LineColor = ImVec4(0.5f, 0.5f, 0.5f, 0.5f);
                    ImPlot::PlotInfLines("Sections", sectionBoundaries.data(), (int)sectionBoundaries.size(), vspec);
                }

                ImVec2 plotPos = ImPlot::GetPlotPos();
                ImVec4 labelCol = ImGui::GetStyle().Colors[ImGuiCol_Text];
                labelCol.w *= 0.7f;
                ImPlot::GetPlotDrawList()->AddText(ImVec2(plotPos.x + 10.0f, plotPos.y + 10.0f), ImGui::GetColorU32(labelCol), "Lateral Force");

                ImPlot::EndPlot();
            }
        }
        ImPlot::EndSubplots();
    }
    if (pushedTrans) {
        ImPlot::PopStyleColor(4);
    }
}

void GraphView::renderTimeline(trackHandler* hTrack) {
    if (!hTrack || !hTrack->trackData || !gViewport)
        return;

    if (playheadLocked) {
        if (lockedTransition) {
            bool transitionExists = false;
            func* parentFunc = lockedTransition->parent;
            if (parentFunc) {
                for (subfunc* sf : parentFunc->funcList) {
                    if (sf == lockedTransition) {
                        transitionExists = true;
                        break;
                    }
                }
            }

            if (!transitionExists) {
                // Transition was deleted! Release the lock!
                playheadLocked = false;
                lockedTransition = nullptr;
            } else {
                // Update playhead position to match the end of the transition (maxArgument)!
                section* secParent = parentFunc->secParent;
                if (secParent && !secParent->lNodes.empty()) {
                    double endArg = lockedTransition->maxArgument;

                    int localNodeIdx = 0;
                    if (secParent->bArgument == DISTANCE) {
                        double targetDist = secParent->lNodes.front().fTotalLength + endArg;
                        double minDiff = 1e9;
                        for (size_t j = 0; j < secParent->lNodes.size(); ++j) {
                            double diff = std::abs(secParent->lNodes[j].fTotalLength - targetDist);
                            if (diff < minDiff) {
                                minDiff = diff;
                                localNodeIdx = (int)j;
                            }
                        }
                    } else {
                        localNodeIdx = (int)(endArg * F_HZ + 0.5);
                        if (localNodeIdx >= (int)secParent->lNodes.size()) {
                            localNodeIdx = (int)secParent->lNodes.size() - 1;
                        }
                    }

                    int secStartNodeIdx = hTrack->trackData->getNumPoints(secParent);
                    lockedPOVPos = secStartNodeIdx + localNodeIdx;
                }
            }
        }
        gViewport->setPOVPos(lockedPOVPos);
        povAccumulator = (float)lockedPOVPos;
    }

    const auto& processedTrack = graphProcessor.getData();

    static int lastUpdateFrame = -1;
    int currentFrame = ImGui::GetFrameCount();

    if (currentFrame != lastUpdateFrame) {
        lastUpdateFrame = currentFrame;

        // Ensure any pending track rebuilds (triggered by UI edits) are processed before we try to snap or plot.
        // This is critical if the Viewport window is not currently visible/rendering.
        hTrack->trackData->processPendingUpdates();

        // Handle timeline snapping to selected transition
        bool shouldSnap = false;
        if (gloParent) {
            if (gloParent->selectedFunc != selectedSubfunc) {
                shouldSnap = true;
            } else if (selectedSubfunc) {
                bool minChanged = std::abs(selectedSubfunc->minArgument - lastSelectedMinArg) > 1e-6;
                bool maxChanged = std::abs(selectedSubfunc->maxArgument - lastSelectedMaxArg) > 1e-6;
                if (minChanged || maxChanged) {
                    shouldSnap = true;
                }
            }
        }

        if (shouldSnap && playheadSnappingEnabled && !playheadLocked) {
            selectedSubfunc = gloParent->selectedFunc;
            if (selectedSubfunc) {
                lastSelectedMinArg = selectedSubfunc->minArgument;
                lastSelectedMaxArg = selectedSubfunc->maxArgument;

                bool snapped = false;
                for (const auto& eg : editableGraphs) {
                    if (eg.sf == selectedSubfunc && !eg.nodes.empty()) {
                        int targetIdx = eg.nodes.back();
                        gViewport->setPOVPos(targetIdx);
                        povAccumulator = (float)targetIdx;
                        snapped = true;
                        break;
                    }
                }

                if (!snapped && selectedSubfunc->parent && selectedSubfunc->parent->secParent && selectedSubfunc->parent->secParent->parent == hTrack->trackData) {
                    section* sec = selectedSubfunc->parent->secParent;
                    track* t = hTrack->trackData;
                    int targetIdx = t->getNumPoints(sec) + (int)sec->lNodes.size() - 1;
                    gViewport->setPOVPos(targetIdx);
                    povAccumulator = (float)targetIdx;
                }
            } else {
                lastSelectedMaxArg = -1.0;
            }
        } else if (shouldSnap) {
            // Keep selectedSubfunc and track parameters synchronized even when snapping is disabled
            selectedSubfunc = gloParent->selectedFunc;
            if (selectedSubfunc) {
                lastSelectedMinArg = selectedSubfunc->minArgument;
                lastSelectedMaxArg = selectedSubfunc->maxArgument;
            } else {
                lastSelectedMaxArg = -1.0;
            }
        }

        bool pPressed = ImGui::IsKeyPressed(ImGuiKey_P, false) && !ImGui::GetIO().WantTextInput;
        if (pPressed) {
            isPlayingPOV = !isPlayingPOV;
            if (isPlayingPOV)
                povAccumulator = (float)gViewport->getPOVPos();
        }

        int maxPoints = hTrack->trackData->getNumPoints();
        if (maxPoints > 0) {
            if (isPlayingPOV && !playheadLocked) {
                float speed = F_HZ; // Real-time nodes per second (matches simulation sample rate)
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                    speed *= gloParent->mOptions->sprintMultiplier;
                }
                povAccumulator += speed * ImGui::GetIO().DeltaTime;
                if (povAccumulator > maxPoints)
                    povAccumulator = 0.0f;
                gViewport->setPOVPos((int)povAccumulator);
            }

            int povPos = gViewport->getPOVPos();

            // Calculate instantaneous peak forces
            hTrack->trackData->maxNormalOffsetIdx = -1;
            hTrack->trackData->minNormalOffsetIdx = -1;
            hTrack->trackData->maxLateralOffsetIdx = -1;
            hTrack->trackData->minLateralOffsetIdx = -1;

            if (hTrack->trackData->forceHighlightMode != 0) {
                double maxN = -1e9, minN = 1e9;
                double maxL = -1e9, minL = 1e9;
                mnode* comNode = hTrack->trackData->getPoint(povPos);
                if (comNode) {
                    for (size_t i = 0; i < hTrack->trackData->trainOffsets.size(); ++i) {
                        const auto& offset = hTrack->trackData->trainOffsets[i];
                        double targetDist = comNode->fTotalLength + offset.offset.z;
                        int searchIdx = hTrack->trackData->getIndexFromDist(targetDist);
                        mnode* targetNode = hTrack->trackData->getPoint(searchIdx);
                        if (!targetNode)
                            continue;

                        double g_norm = -glm::dot(glm::dvec3(0.0, 1.0, 0.0), targetNode->vNorm);
                        double g_lat = -glm::dot(glm::dvec3(0.0, 1.0, 0.0), targetNode->vLat);
                        double dyn_norm = processedTrack.forceNormal[searchIdx] - g_norm;
                        double dyn_lat = processedTrack.forceLateral[searchIdx] - g_lat;

                        double v_ratio = 1.0;
                        double v_ratio_sq = 1.0;
                        if (targetNode->fVel > 0.01) {
                            v_ratio = comNode->fVel / targetNode->fVel;
                            v_ratio_sq = v_ratio * v_ratio;
                        }

                        double w_roll = (processedTrack.rollSpeed[searchIdx] * F_PI / 180.0) * v_ratio;
                        double w_pitch = (processedTrack.pitchChange[searchIdx] * F_PI / 180.0) * v_ratio;
                        double w_yaw = (processedTrack.yawChange[searchIdx] * F_PI / 180.0) * v_ratio;

                        double alpha_roll = processedTrack.rollAccel[searchIdx] * F_HZ * F_PI / 180.0 * v_ratio_sq;
                        double alpha_pitch = processedTrack.pitchChangeDeriv[searchIdx] * F_HZ * F_PI / 180.0 * v_ratio_sq;
                        double alpha_yaw = processedTrack.yawChangeDeriv[searchIdx] * F_HZ * F_PI / 180.0 * v_ratio_sq;

                        glm::dvec3 omega = w_roll * targetNode->vDir + w_pitch * targetNode->vLat - w_yaw * targetNode->vNorm;
                        glm::dvec3 alpha = alpha_roll * targetNode->vDir + alpha_pitch * targetNode->vLat - alpha_yaw * targetNode->vNorm;
                        glm::dvec3 r = targetNode->vLat * (double)offset.offset.x - targetNode->vNorm * (double)offset.offset.y;

                        glm::dvec3 a_cen = glm::cross(omega, glm::cross(omega, r));
                        glm::dvec3 a_tan = glm::cross(alpha, r);
                        glm::dvec3 a_extra = a_cen + a_tan;
                        glm::dvec3 felt_extra_force = a_extra / 9.80665;

                        double extra_norm = -glm::dot(felt_extra_force, targetNode->vNorm);
                        double extra_lat = -glm::dot(felt_extra_force, targetNode->vLat);

                        double adj_norm = g_norm + dyn_norm * v_ratio_sq + extra_norm;
                        double adj_lat = g_lat + dyn_lat * v_ratio_sq + extra_lat;

                        if (adj_norm > maxN) {
                            maxN = adj_norm;
                            hTrack->trackData->maxNormalOffsetIdx = i;
                        }
                        if (adj_norm < minN) {
                            minN = adj_norm;
                            hTrack->trackData->minNormalOffsetIdx = i;
                        }
                        if (adj_lat > maxL) {
                            maxL = adj_lat;
                            hTrack->trackData->maxLateralOffsetIdx = i;
                        }
                        if (adj_lat < minL) {
                            minL = adj_lat;
                            hTrack->trackData->minLateralOffsetIdx = i;
                        }
                    }
                }
            }
        }
    }

    ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && (dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y < 9.0f)) {
        isPlayingPOV = !isPlayingPOV;
        if (isPlayingPOV)
            povAccumulator = (float)gViewport->getPOVPos();
    }

    int maxPoints = hTrack->trackData->getNumPoints();
    if (maxPoints > 0) {
        int povPos = gViewport->getPOVPos();
        float buttonSize = ImGui::GetFrameHeight();

        int step = 0;
        static int lastProcessedFrame = -1;
        int currentFrame = ImGui::GetFrameCount();
        bool isFirstCallThisFrame = (currentFrame != lastProcessedFrame);

        // Button < (Decrement)
        if (ImGui::Button("<", ImVec2(buttonSize, buttonSize))) {
            if (isFirstCallThisFrame)
                step = -1;
        }
        bool decActive = ImGui::IsItemActive();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonSize - ImGui::GetStyle().ItemSpacing.x);

        ImGui::PushID("TimelineSlider");
        if (ImGui::SliderInt("", &povPos, 0, maxPoints, "Timeline: %d")) {
            gViewport->setPOVPos(povPos);
            povAccumulator = (float)povPos;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Right-click on the timeline to play/pause, or press P.");
        }
        ImGui::PopID();

        ImGui::SameLine();
        // Button > (Increment)
        if (ImGui::Button(">", ImVec2(buttonSize, buttonSize))) {
            if (isFirstCallThisFrame)
                step = 1;
        }
        bool incActive = ImGui::IsItemActive();

        // Keyboard holds and single taps (run exactly once per frame)
        if (isFirstCallThisFrame) {
            lastProcessedFrame = currentFrame;

            // Keyboard single taps (global, triggers immediately on key press)
            if (!ImGui::GetIO().WantTextInput) {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
                    step = -1;
                } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
                    step = 1;
                }
            }

            // Keyboard holds (Left/Right arrows are global, except when typing in a text input)
            bool decKeyActive = !ImGui::GetIO().WantTextInput && ImGui::IsKeyDown(ImGuiKey_LeftArrow);
            bool incKeyActive = !ImGui::GetIO().WantTextInput && ImGui::IsKeyDown(ImGuiKey_RightArrow);

            static float holdTimer = 0.0f;
            static float repeatTimer = 0.0f;

            bool anyActive = decActive || incActive || decKeyActive || incKeyActive;
            if (anyActive) {
                holdTimer += ImGui::GetIO().DeltaTime;
                if (holdTimer > 0.4f) {
                    repeatTimer += ImGui::GetIO().DeltaTime;
                    float repeatInterval = 0.05f;
                    if (holdTimer > 1.5f)
                        repeatInterval = 0.01f; // speed up!

                    if (repeatTimer >= repeatInterval) {
                        step = (decActive || decKeyActive) ? -1 : 1;
                        if (holdTimer > 3.0f) {
                            step *= 10; // Fast-forward!
                        }
                        repeatTimer = 0.0f;
                    }
                }
            } else {
                holdTimer = 0.0f;
                repeatTimer = 0.0f;
            }
        }

        if (step != 0 && !playheadLocked) {
            povPos = std::clamp(povPos + step, 0, maxPoints);
            gViewport->setPOVPos(povPos);
            povAccumulator = (float)povPos;
        }
    }
}

void GraphView::exportToCSV(const std::string& filepath, trackHandler* hTrack) {
    if (!hTrack || !hTrack->trackData)
        return;

    std::ofstream fout(filepath);
    if (!fout.is_open())
        return;

    section* sec = hTrack->trackData->activeSection;
    bool isGeo = (sec && sec->type != forced && sec->type != straight && sec->type != curved);

    // Identify which resulting graphs are visible
    std::vector<int> visibleResultingIndices;
    for (int i = (int)GraphType::Banking; i < (int)GraphType::Count; ++i) {
        if (i != (int)GraphType::Velocity && i != (int)GraphType::WorldPitch && graphs[i].visible) {
            visibleResultingIndices.push_back(i);
        }
    }
    // Always append Velocity and World Pitch at the end of resulting columns for physical cross-validation
    visibleResultingIndices.push_back((int)GraphType::Velocity);
    visibleResultingIndices.push_back((int)GraphType::WorldPitch);

    // Identify which editable graphs are visible
    std::vector<int> visibleEditableIndices;
    for (int i = 0; i < (int)GraphType::Banking; ++i) {
        if (graphs[i].visible) {
            visibleEditableIndices.push_back(i);
        }
    }

    // Write CSV Header with explicit "Resulting_" and "Editable_" prefixes
    bool first = true;
    for (int idx : visibleResultingIndices) {
        if (!first)
            fout << ",";
        fout << "Resulting_" << graphs[idx].label << "_X,"
             << "Resulting_" << graphs[idx].label << "_Y";
        first = false;
    }
    for (int idx : visibleEditableIndices) {
        if (!first)
            fout << ",";
        std::string label = "";
        if (idx == (int)GraphType::EditRoll) {
            label = "Roll Speed";
        } else if (idx == (int)GraphType::EditNormal) {
            label = isGeo ? "Pitch Change" : "Normal Force";
        } else if (idx == (int)GraphType::EditLateral) {
            label = isGeo ? "Yaw Change" : "Lateral Force";
        }
        fout << "Editable_" << label << "_X,"
             << "Editable_" << label << "_Y";
        first = false;
    }
    fout << "\n";

    // Since each graph can have a different number of points (due to different spacing or sections),
    // we find the maximum number of rows we need to write:
    size_t maxRows = 0;
    std::vector<std::vector<double>> col_X;
    std::vector<std::vector<double>> col_Y;

    // Populate resulting columns
    for (int idx : visibleResultingIndices) {
        std::vector<double> x_vals;
        std::vector<double> y_vals;
        for (const auto& secData : graphs[idx].sections) {
            x_vals.insert(x_vals.end(), secData.x.begin(), secData.x.end());
            y_vals.insert(y_vals.end(), secData.y.begin(), secData.y.end());
        }
        maxRows = std::max(maxRows, x_vals.size());
        col_X.push_back(x_vals);
        col_Y.push_back(y_vals);
    }

    // Populate editable columns
    for (int idx : visibleEditableIndices) {
        std::vector<double> x_vals;
        std::vector<double> y_vals;
        for (const auto& eg : editableGraphs) {
            int editType = (int)GraphType::EditRoll;
            if (eg.sf && eg.sf->parent) {
                if (eg.sf->parent->type == funcPitch || eg.sf->parent->type == funcNormal) {
                    editType = (int)GraphType::EditNormal;
                } else if (eg.sf->parent->type == funcYaw || eg.sf->parent->type == funcLateral) {
                    editType = (int)GraphType::EditLateral;
                }
            }
            if (editType == idx) {
                x_vals.insert(x_vals.end(), eg.x.begin(), eg.x.end());
                y_vals.insert(y_vals.end(), eg.y.begin(), eg.y.end());
            }
        }
        maxRows = std::max(maxRows, x_vals.size());
        col_X.push_back(x_vals);
        col_Y.push_back(y_vals);
    }

    // Write Rows
    fout << std::fixed << std::setprecision(6);
    for (size_t row = 0; row < maxRows; ++row) {
        for (size_t col = 0; col < col_X.size(); ++col) {
            if (col > 0)
                fout << ",";
            if (row < col_X[col].size()) {
                fout << col_X[col][row] << "," << col_Y[col][row];
            } else {
                fout << ","; // Empty fields for short columns
            }
        }
        fout << "\n";
    }

    fout.close();
}

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

#ifndef GRAPHVIEW_H
#define GRAPHVIEW_H

#include "imgui.h"
#include "implot.h"
#include "resultinggraphprocessor.h"
#include "measurementgraphprocessor.h"
#include <vector>
#include <string>

#ifndef ImAxis_Y4
#define ImAxis_Y4 (ImAxis_Y1 + 3)
#endif

class trackHandler;
class func;
class subfunc;
class section;

enum class GraphType {
    // Editable
    EditRoll = 0,
    EditNormal,
    EditLateral,
    // Resulting
    Banking,
    RollSpeed,
    RollAccel,
    NForce,
    NForceChange,
    LForce,
    LForceChange,
    PitchChange,
    YawChange,
    WorldPitchChange,
    WorldYawChange,
    Velocity,
    WorldPitch,
    Count
};

struct SectionGraphData {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<int> nodes;
    bool isActive;
};

struct GraphData {
    std::vector<SectionGraphData> sections;
    std::string label;
    ImVec4 color;
    bool visible;
};

struct EditableGraphData {
    std::vector<double> x;
    std::vector<double> y;
    std::vector<int> nodes;
    subfunc* sf;
    ImVec4 color;
    std::string label;
};

class GraphView {
public:
    GraphView();
    ~GraphView();

    void renderList(trackHandler* track);
    void lockPlayheadToCurrentTransition(trackHandler* hTrack);
    void renderMeasurementsMenu(trackHandler* track);
    void renderPlot(trackHandler* track);
    void renderResultingPlot(trackHandler* track);
    void renderTimeline(trackHandler* track);
    void renderMeasurementPlot(trackHandler* track);
    void updateData(trackHandler* track);
    void exportToCSV(const std::string& filepath, trackHandler* hTrack, int startNodeIdx, int endNodeIdx);

    bool hasResultingGraphsVisible() const;
    bool hasMeasurementGraphsVisible(trackHandler* track) const;
    int getTotalPlottedPoints() const;
    bool getAndClearSwitchToResultingTab() {
        bool val = switchToResultingTab;
        switchToResultingTab = false;
        return val;
    }

private:
    void sampleGraph(trackHandler* track, GraphType type);
    void sampleEditable(trackHandler* track);

    ResultingGraphProcessor graphProcessor;
    MeasurementGraphProcessor measurementProcessor;

    std::vector<GraphData> graphs;
    std::vector<EditableGraphData> editableGraphs;
    std::vector<double> sectionBoundaries;
    std::vector<std::pair<double, double>> gimbalLockRegions;
    std::vector<std::pair<double, double>> radiusLimitRegions;
    std::vector<std::pair<double, double>> forceLimitRegions;
    bool needsUpdate;
    bool playheadSnappingEnabled;
    bool doAutoFocus;
    bool autoScaleYEdit;
    bool autoScaleYResult;
    bool autoScaleYMeasure;
    bool switchToResultingTab = false;
    bool isPlayingPOV = false;
    float povAccumulator = 0.0f;
    bool playheadLocked = false;
    int lockedPOVPos = 0;
    subfunc* lockedTransition = nullptr;

    int lastSubplotRows = -1;
    double linkedXMin = 0.0;
    double linkedXMax = 10.0;
    section* lastActiveSection = nullptr;
    std::vector<char> lastVisible;

    subfunc* selectedSubfunc;
    double lastSelectedMinArg = -1.0;
    double lastSelectedMaxArg = -1.0;
};

#endif // GRAPHVIEW_H

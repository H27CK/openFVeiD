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

#ifndef LEFTPANEL_H
#define LEFTPANEL_H

#include "imgui.h"
#include <vector>
#include <string>

class trackHandler;
class section;
class Application;

class LeftPanel {
public:
    LeftPanel();
    ~LeftPanel();

    void render(Application* app);
    void renderEnvironmentTab();

    int activeTab; // 0 for Project, 1 for Track
    int selectedSectionIdx;

private:
    void renderProjectTab(Application* app);
    void renderTrackTab(trackHandler* activeTrack, Application* app);

    void renderTrackProperties(trackHandler* track, Application* app);
    void renderParametricStyleTab(trackHandler* track, Application* app);
    void renderTrackSmoothing(trackHandler* track, Application* app);
    void renderColorsTab(trackHandler* hTrack, Application* app);

    void renderSectionProperties(trackHandler* hTrack, section* sec, Application* app);

    // Sub-components
    void renderAnchorProperties(trackHandler* hTrack, section* sec, Application* app);
    void renderStraightProperties(trackHandler* hTrack, section* sec, Application* app);
    void renderCurvedProperties(trackHandler* hTrack, section* sec, Application* app);
    void renderForcedProperties(trackHandler* hTrack, section* sec, Application* app);
    void renderStaticProperties(trackHandler* hTrack, section* sec, Application* app);

    void syncAnchorNode(trackHandler* hTrack);

    int selectedSmoothingIdx;
};

#endif // LEFTPANEL_H

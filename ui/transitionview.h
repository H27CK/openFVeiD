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

#ifndef TRANSITIONVIEW_H
#define TRANSITIONVIEW_H

#include "imgui.h"
#include <string>

class trackHandler;
class subfunc;
class Application;

class TransitionView {
public:
    TransitionView();
    ~TransitionView();

    void render(trackHandler* hTrack, subfunc* selectedSubfunc, Application* app);

private:
    void renderSelectionList(trackHandler* hTrack, subfunc* sf, Application* app);
    void renderBasicProperties(trackHandler* hTrack, subfunc* sf, Application* app);
    void renderTypeSpecificProperties(trackHandler* hTrack, subfunc* sf, Application* app);
    void renderTimewarpProperties(trackHandler* hTrack, subfunc* sf, Application* app);
    void renderActions(trackHandler* hTrack, subfunc* sf, Application* app);
    void renderTargetSolver(trackHandler* hTrack, subfunc* sf, Application* app);

    std::string getLengthSuffix(subfunc* sf);
    std::string getChangeSuffix(subfunc* sf);

    // Target Solver UI State
    int solverTargetVar = 0;
    float solverTargetValue = 0.0f;
    int solverAdjustParam = 1; // Default to Amplitude/Change
    int solverMaxIterations = 40;
    int solverPrecisionDps = 5;
    std::string solverMessage = "";
    bool solverSuccess = false;
    bool solverHasMessage = false;
};

#endif // TRANSITIONVIEW_H

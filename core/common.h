#ifndef COMMON_H
#define COMMON_H

/*
#    FVD++, an advanced coaster design tool for NoLimits
#    Copyright (C) 2012-2015, Stephan "Lenny" Alt <alt.stephan@web.de>
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
#    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <string>
#include <cmath>
#include "imgui.h"
#include "imgui_internal.h"
#include "dummies.h"

namespace common {
std::string getResource(const char* file, bool fullpath = false);
std::string normalizeAssetPath(const std::string& path);
bool isPathFromOtherOS(const std::string& path);

inline bool ValueScroll(float* v, float step = 1.0f, float ctrlStep = 0.1f) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (ImGui::GetIO().MouseWheel != 0.0f) {
            float multiplier = gloParent->mOptions->scrollIncrement;
            if (ImGui::GetIO().KeyCtrl) {
                multiplier = gloParent->mOptions->scrollCtrlIncrement;
            } else if (ImGui::GetIO().KeyShift) {
                multiplier = gloParent->mOptions->scrollShiftIncrement;
            }
            *v += ImGui::GetIO().MouseWheel * step * multiplier;
            return true;
        }
    }
    return false;
}

inline bool ValueScrollInt(int* v, int step = 1, int ctrlStep = 1) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (ImGui::GetIO().MouseWheel != 0.0f) {
            float multiplier = gloParent->mOptions->scrollIncrement;
            if (ImGui::GetIO().KeyCtrl) {
                multiplier = gloParent->mOptions->scrollCtrlIncrement;
            } else if (ImGui::GetIO().KeyShift) {
                multiplier = gloParent->mOptions->scrollShiftIncrement;
            }
            *v += static_cast<int>(std::round(ImGui::GetIO().MouseWheel * step * multiplier));
            return true;
        }
    }
    return false;
}

inline double softLimit(double x, double minL, double maxL, double m = 0.2) {
    double range = maxL - minL;
    double activeMargin = m;
    if (activeMargin * 2.0 > range) {
        activeMargin = range * 0.49;
    }

    if (x > maxL - activeMargin) {
        if (x > maxL + activeMargin) {
            x = maxL;
        } else {
            double diff = x - (maxL + activeMargin);
            x = maxL - (diff * diff) / (4.0 * activeMargin);
        }
    }
    if (x < minL + activeMargin) {
        if (x < minL - activeMargin) {
            x = minL;
        } else {
            double diff = x - (minL - activeMargin);
            x = minL + (diff * diff) / (4.0 * activeMargin);
        }
    }
    return x;
}
} // namespace common

#endif // COMMON_H

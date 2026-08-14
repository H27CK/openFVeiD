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

#ifndef RESULTINGGRAPHPROCESSOR_H
#define RESULTINGGRAPHPROCESSOR_H

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "core/track.h"
#include "core/mnode.h"

struct ProcessedTrackData {
    std::vector<double> roll;
    std::vector<double> rollRate;
    std::vector<double> riderRollRate;
    std::vector<double> rollAccel;
    std::vector<double> forceNormal;
    std::vector<double> forceNormalChange;
    std::vector<double> forceLateral;
    std::vector<double> forceLateralChange;
    std::vector<double> pitchChange;
    std::vector<double> pitchChangeDeriv;
    std::vector<double> yawChange;
    std::vector<double> yawChangeDeriv;
    std::vector<double> xDistance;
    std::vector<double> xTime;
    std::vector<double> vel;
    std::vector<double> longitudinalAccel;
    std::vector<double> worldPitch;
    std::vector<double> worldPitchChange;
    std::vector<double> worldYawChange;
};

class ResultingGraphProcessor {
public:
    ResultingGraphProcessor() = default;
    ~ResultingGraphProcessor() = default;

    void update(trackHandler* track);

    const ProcessedTrackData& getData() const {
        return processedTrack;
    }

private:
    ProcessedTrackData processedTrack;
};

#endif // RESULTINGGRAPHPROCESSOR_H
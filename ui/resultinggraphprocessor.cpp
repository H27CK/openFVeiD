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

#include "resultinggraphprocessor.h"
#include "core/trackhandler.h"
#include "core/track.h"
#include "core/section.h"
#include "core/common.h"
#include "core/dummies.h"
#include <algorithm>
#include <cmath>

extern DummyGlobal* gloParent;

void ResultingGraphProcessor::update(trackHandler* track) {
    if (!track || !track->trackData) {
        return;
    }

    int totalPoints = track->trackData->getNumPoints();

    processedTrack.xDistance.resize(totalPoints + 1, 0.0);
    processedTrack.xTime.resize(totalPoints + 1, 0.0);
    processedTrack.vel.resize(totalPoints + 1, 0.0);
    processedTrack.longitudinalAccel.resize(totalPoints + 1, 0.0);
    processedTrack.worldPitch.resize(totalPoints + 1, 0.0);
    processedTrack.worldPitchChange.resize(totalPoints + 1, 0.0);
    processedTrack.worldYawChange.resize(totalPoints + 1, 0.0);

    processedTrack.roll.resize(totalPoints + 1, 0.0);
    processedTrack.rollRate.resize(totalPoints + 1, 0.0);
    processedTrack.riderRollRate.resize(totalPoints + 1, 0.0);
    processedTrack.rollAccel.resize(totalPoints + 1, 0.0);
    processedTrack.pitchChange.resize(totalPoints + 1, 0.0);
    processedTrack.pitchChangeDeriv.resize(totalPoints + 1, 0.0);
    processedTrack.yawChange.resize(totalPoints + 1, 0.0);
    processedTrack.yawChangeDeriv.resize(totalPoints + 1, 0.0);
    processedTrack.forceNormal.resize(totalPoints + 1, 0.0);
    processedTrack.forceNormalChange.resize(totalPoints + 1, 0.0);
    processedTrack.forceLateral.resize(totalPoints + 1, 0.0);
    processedTrack.forceLateralChange.resize(totalPoints + 1, 0.0);

    for (int j = 0; j <= totalPoints; ++j) {
        mnode* node = track->trackData->getPoint(j);
        if (!node)
            continue;

        // Pass-through raw values
        processedTrack.xDistance[j] = node->fTotalLength;
        processedTrack.vel[j] = node->fVel;
        processedTrack.worldPitch[j] = node->getPitch();
        processedTrack.xTime[j] = (double)j / F_HZ;
        processedTrack.roll[j] = node->fRoll;
        processedTrack.rollRate[j] = node->fRollSpeed;

        if (j > 0) {
            mnode* prev = track->trackData->getPoint(j - 1);
            processedTrack.longitudinalAccel[j] = (node->fVel - prev->fVel) * F_HZ / F_G;
        } else {
            processedTrack.longitudinalAccel[j] = 0.0;
        }

        if (j > 0) {
            mnode* prev = track->trackData->getPoint(j - 1);
            double deltaRoll = node->fRoll - prev->fRoll;
            while (deltaRoll < -180.0)
                deltaRoll += 360.0;
            while (deltaRoll > 180.0)
                deltaRoll -= 360.0;
            processedTrack.riderRollRate[j] = deltaRoll * F_HZ;
        } else {
            processedTrack.riderRollRate[j] = node->fRollSpeed;
        }

        processedTrack.forceNormal[j] = node->forceNormal;
        processedTrack.forceLateral[j] = node->forceLateral;
        processedTrack.worldPitchChange[j] = node->getPitchChange();
        processedTrack.worldYawChange[j] = node->getYawChange();

        if (j > 0) {
            mnode* prev = track->trackData->getPoint(j - 1);

            // Simplified Rider-Local Projection
            glm::dvec3 d_diff = node->vDir - prev->vDir;
            double p_rate = -glm::dot(d_diff, prev->vNorm) * 180.0 / F_PI * F_HZ;
            double y_rate = -glm::dot(d_diff, prev->vLat) * 180.0 / F_PI * F_HZ;
            processedTrack.pitchChange[j] = p_rate;
            processedTrack.yawChange[j] = y_rate;

            // Derivatives for other graphs (using legacy 20-step smoothing for consistency)
            mnode* prev20 = (j > 19) ? track->trackData->getPoint(j - 20) : prev;
            double diff = (j > 19) ? 20.0 : 1.0;

            processedTrack.rollAccel[j] = (node->fRollSpeed + node->fSmoothSpeed - (prev20->fRollSpeed + prev20->fSmoothSpeed)) / diff;
            processedTrack.forceNormalChange[j] = (node->forceNormal + node->smoothNormal - (prev20->forceNormal + prev20->smoothNormal)) / diff;
            processedTrack.forceLateralChange[j] = (node->forceLateral + node->smoothLateral - (prev20->forceLateral + prev20->smoothLateral)) / diff;

            // Derivatives for rider-local graphs (no smoothing)
            processedTrack.pitchChangeDeriv[j] = (p_rate - processedTrack.pitchChange[j - 1]) * F_HZ;
            processedTrack.yawChangeDeriv[j] = (y_rate - processedTrack.yawChange[j - 1]) * F_HZ;

        } else {
            processedTrack.pitchChange[j] = 0.0;
            processedTrack.yawChange[j] = 0.0;
            processedTrack.rollAccel[j] = 0.0;
            processedTrack.pitchChangeDeriv[j] = 0.0;
            processedTrack.yawChangeDeriv[j] = 0.0;
            processedTrack.forceNormalChange[j] = 0.0;
            processedTrack.forceLateralChange[j] = 0.0;
        }
    }
}

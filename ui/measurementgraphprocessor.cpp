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

#include "measurementgraphprocessor.h"
#include "resultinggraphprocessor.h"
#include "core/trackhandler.h"
#include "core/track.h"
#include "core/section.h"
#include "core/mnode.h"
#include "core/common.h"
#include "core/dummies.h"
#include <algorithm>
#include <cmath>

extern DummyGlobal* gloParent;

void MeasurementGraphProcessor::update(trackHandler* track, const ResultingGraphProcessor& resultingProcessor) {
    trainOffsetGraphs.clear();
    if (!track || !track->trackData) {
        return;
    }

    section* activeSec = track->trackData->activeSection;
    bool useDistance = (activeSec && activeSec->bArgument == 1);

    int numOffsets = track->trackData->trainOffsets.size();
    trainOffsetGraphs.resize(numOffsets);

    int totalPoints = track->trackData->getNumPoints();
    if (totalPoints <= 0)
        return;

    const auto& processedTrack = resultingProcessor.getData();

    track->trackData->maxNormalOffsetIdx = -1;
    track->trackData->minNormalOffsetIdx = -1;
    track->trackData->maxLateralOffsetIdx = -1;
    track->trackData->minLateralOffsetIdx = -1;
    double maxN = -1e9, minN = 1e9;
    double maxL = -1e9, minL = 1e9;

    double spacing_limit = gloParent ? (double)gloParent->mOptions->graphSpacingLimit : 0.1;

    for (int i = 0; i < numOffsets; ++i) {
        const auto& offset = track->trackData->trainOffsets[i];
        auto& gData = trainOffsetGraphs[i];

        double last_dist = -999.0;

        for (int j = 0; j <= totalPoints; ++j) {
            mnode* comNode = track->trackData->getPoint(j);
            if (!comNode)
                continue;

            double cur_dist = comNode->fTotalLength;
            // Uniform spatial down-sampling on-the-fly
            if (j == 0 || j == totalPoints || (cur_dist - last_dist >= spacing_limit)) {
                last_dist = cur_dist;

                double targetDist = comNode->fTotalLength + offset.offset.z;
                int searchIdx = track->trackData->getIndexFromDist(targetDist);

                mnode* targetNode = track->trackData->getPoint(searchIdx);
                if (!targetNode)
                    continue;

                double x = useDistance ? comNode->fTotalLength : ((double)j / F_HZ);

                // Actual position with full 3D offset
                glm::dvec3 truePos = targetNode->vPosHeart(track->trackData->fHeart) + targetNode->vLat * (double)offset.offset.x + targetNode->vNorm * (double)offset.offset.y;

                // Compute gravity at that point
                double g_norm = -glm::dot(glm::dvec3(0.0, 1.0, 0.0), targetNode->vNorm);
                double g_lat = -glm::dot(glm::dvec3(0.0, 1.0, 0.0), targetNode->vLat);

                // Get the raw dynamic force at the center heartline
                double dyn_norm = processedTrack.forceNormal[searchIdx] - g_norm;
                double dyn_lat = processedTrack.forceLateral[searchIdx] - g_lat;

                // Rescale dynamic forces based on true velocity ratio
                double v_ratio = 1.0;
                double v_ratio_sq = 1.0;
                if (targetNode->fVel > 0.01) {
                    v_ratio = comNode->fVel / targetNode->fVel;
                    v_ratio_sq = v_ratio * v_ratio;
                }

                // Calculate angular velocities (rad/s) and scale by velocity ratio
                double w_roll = (processedTrack.rollRate[searchIdx] * F_PI / 180.0) * v_ratio;
                double w_pitch = (processedTrack.pitchChange[searchIdx] * F_PI / 180.0) * v_ratio;
                double w_yaw = (processedTrack.yawChange[searchIdx] * F_PI / 180.0) * v_ratio;

                // Calculate angular accelerations (rad/s^2) and scale
                double alpha_roll = processedTrack.rollAccel[searchIdx] * F_HZ * F_PI / 180.0 * v_ratio_sq;
                double alpha_pitch = processedTrack.pitchChangeDeriv[searchIdx] * F_HZ * F_PI / 180.0 * v_ratio_sq;
                double alpha_yaw = processedTrack.yawChangeDeriv[searchIdx] * F_HZ * F_PI / 180.0 * v_ratio_sq;

                // Construct rotational vectors
                glm::dvec3 omega = w_roll * targetNode->vDir + w_pitch * targetNode->vLat - w_yaw * targetNode->vNorm;
                glm::dvec3 alpha = alpha_roll * targetNode->vDir + alpha_pitch * targetNode->vLat - alpha_yaw * targetNode->vNorm;

                // The physical offset from the track's center of rotation (heartline)
                glm::dvec3 r = targetNode->vLat * (double)offset.offset.x - targetNode->vNorm * (double)offset.offset.y;

                // Calculate resulting acceleration (a_centripetal + a_tangential) in m/s^2
                glm::dvec3 a_cen = glm::cross(omega, glm::cross(omega, r));
                glm::dvec3 a_tan = glm::cross(alpha, r);
                glm::dvec3 a_extra = a_cen + a_tan;

                // Convert to G-Force felt by rider
                glm::dvec3 felt_extra_force = a_extra / 9.80665;

                double extra_norm = -glm::dot(felt_extra_force, targetNode->vNorm);
                double extra_lat = -glm::dot(felt_extra_force, targetNode->vLat);

                double adj_norm = g_norm + dyn_norm * v_ratio_sq + extra_norm;
                double adj_lat = g_lat + dyn_lat * v_ratio_sq + extra_lat;

                if (adj_norm > maxN) {
                    maxN = adj_norm;
                    track->trackData->maxNormalOffsetIdx = i;
                }
                if (adj_norm < minN) {
                    minN = adj_norm;
                    track->trackData->minNormalOffsetIdx = i;
                }
                if (adj_lat > maxL) {
                    maxL = adj_lat;
                    track->trackData->maxLateralOffsetIdx = i;
                }
                if (adj_lat < minL) {
                    minL = adj_lat;
                    track->trackData->minLateralOffsetIdx = i;
                }

                gData.x.push_back(x);
                gData.yNormal.push_back(adj_norm);
                gData.yLateral.push_back(adj_lat);
            }
        }
    }
}
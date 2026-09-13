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

#include "ui/targetsolver.h"
#include "trackhandler.h"
#include "track.h"
#include "section.h"
#include "function.h"
#include "subfunction.h"
#include "dummies.h"
#include "core/application.h"
#include "lenassert.h"
#include <cmath>
#include <algorithm>

static double evaluateValue(trackHandler* hTrack, subfunc* sf, AdjustParameter adjustParam, TargetVariable targetVar, double x, double targetValue) {
    if (adjustParam == AdjustParameter::Length) {
        sf->parent->changeLength(x, sf->parent->getSubfuncNumber(sf));
    } else {
        sf->update(sf->minArgument, sf->maxArgument, x);
    }

    // Trigger track physics integration updates up to this section
    hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
    hTrack->trackData->processPendingUpdates();

    section* sec = sf->parent->secParent;
    if (!sec || sec->lNodes.empty()) {
        return 0.0;
    }

    mnode targetNode;
    // sec->bArgument: TIME is 0, DISTANCE is 1
    if (sec->bArgument == 0) {
        int index = (int)(sf->maxArgument * 1000.0 + 0.5);
        if (index < 0)
            index = 0;
        if (index >= (int)sec->lNodes.size()) {
            index = (int)sec->lNodes.size() - 1;
        }
        targetNode = sec->lNodes[index];
    } else {
        double absDist = sec->lNodes.front().fTotalLength + sf->maxArgument;
        targetNode = sec->sampleAtDistance(absDist);
    }

    double actualValue = 0.0;
    switch (targetVar) {
    case TargetVariable::Roll:
        actualValue = targetNode.fRoll;
        break;
    case TargetVariable::Pitch:
        actualValue = targetNode.getPitch();
        break;
    case TargetVariable::Yaw:
        actualValue = targetNode.getDirection();
        break;
    case TargetVariable::Speed:
        actualValue = targetNode.fVel;
        break;
    case TargetVariable::RollRate:
        actualValue = targetNode.fRollSpeed;
        break;
    case TargetVariable::PitchRate:
        actualValue = targetNode.getPitchChange();
        break;
    case TargetVariable::YawRate:
        actualValue = targetNode.getYawChange();
        break;
    }

    return actualValue - targetValue;
}

bool TargetSolver::Solve(trackHandler* hTrack, subfunc* sf, Application* app,
                         TargetVariable targetVar, double targetValue, AdjustParameter adjustParam,
                         int maxIterations, double tolerance,
                         std::string& outMessage) {
    if (!hTrack || !sf || !sf->parent || !sf->parent->secParent) {
        outMessage = "Invalid selection or track state.";
        return false;
    }

    // Save initial state for rollback or reference
    double initialLength = sf->maxArgument - sf->minArgument;
    double initialAmplitude = sf->symArg;
    double initialVal = (adjustParam == AdjustParameter::Length) ? initialLength : initialAmplitude;

    // Numerical root-finding via Secant Method
    double x0 = initialVal;
    // Step slightly to calculate the secant line
    double x1 = x0 + (std::abs(x0) < 0.1 ? 0.05 : x0 * 0.01);

    // Guard against negative/tiny lengths
    if (adjustParam == AdjustParameter::Length) {
        if (x1 < 0.05)
            x1 = 0.05;
    }

    double f0 = evaluateValue(hTrack, sf, adjustParam, targetVar, x0, targetValue);
    double f1 = evaluateValue(hTrack, sf, adjustParam, targetVar, x1, targetValue);

    int iter = 0;
    bool converged = false;

    while (std::abs(f1) > tolerance && iter < maxIterations) {
        double denom = f1 - f0;
        if (std::abs(denom) < 1e-12) {
            break;
        }

        double nextX = x1 - f1 * (x1 - x0) / denom;

        // Apply clamping boundary for length
        if (adjustParam == AdjustParameter::Length) {
            if (nextX < 0.05) {
                nextX = 0.05;
            }
        }

        x0 = x1;
        f0 = f1;
        x1 = nextX;
        f1 = evaluateValue(hTrack, sf, adjustParam, targetVar, x1, targetValue);
        iter++;
    }

    if (std::abs(f1) <= tolerance) {
        converged = true;
    }

    if (converged) {
        // Trigger one final complete update to finalize and let ImGui render properly
        hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
        hTrack->trackData->processPendingUpdates();

        // Push to global undo handler so this solver step is completely reversible
        app->pushUndo();

        outMessage = "Solved successfully in " + std::to_string(iter) + " iterations!";
        return true;
    } else {
        // Rollback to original pristine state on failure
        if (adjustParam == AdjustParameter::Length) {
            sf->parent->changeLength(initialLength, sf->parent->getSubfuncNumber(sf));
        } else {
            sf->update(sf->minArgument, sf->maxArgument, initialAmplitude);
        }
        hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
        hTrack->trackData->processPendingUpdates();

        outMessage = "Solver failed to converge after " + std::to_string(iter) + " iterations.";
        return false;
    }
}

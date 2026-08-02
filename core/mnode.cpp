/*
#    FVD++, an advanced coaster design tool for NoLimits
#    Copyright (C) 2012-2015, Stephan "Lenny" Alt <alt.stephan@web.de>
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
#    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "mnode.h"
#include "exportfuncs.h"
#include "lenassert.h"
#include "dummies.h"
#include <cmath>
#include <limits>

using namespace std;

mnode::mnode() {}

mnode::mnode(glm::dvec3 getPos, glm::dvec3 getDir, double getRoll, double getVel,
             double getNForce, double getLateral) {
    this->vPos = getPos;
    this->vDir = getDir;
    this->vDir = glm::normalize(vDir);
    this->fRoll = getRoll;
    this->fVel = getVel;
    this->fEnergy = 0.0;
    this->forceNormal = getNForce;
    this->forceLateral = getLateral;
    this->fDistFromLast = 0.0;
    this->fHeartDistFromLast = 0.0;
    this->fAngleFromLast = 0.0;
    this->fTrackAngleFromLast = 0.0;
    this->fDirFromLast = 0.0;
    this->fPitchFromLast = 0.0;
    this->fYawFromLast = 0.0;
    this->fTotalLength = 0.0;
    this->fTotalHeartLength = 0.0;
    this->fSmoothSpeed = 0.0;
    this->smoothNormal = 0.0;
    this->smoothLateral = 0.0;

    if (this->vDir.y == 1.0) {
        this->vLat =
            glm::dvec3(glm::angleAxis(TO_RAD(getRoll), glm::dvec3(0.0, -1.0, 0.0)) *
                       glm::dvec4(1.0, 0.0, 0.0, 0.0));
    } else {
        this->vLat = glm::dvec3(-this->vDir.z, 0.0, this->vDir.x);
    }

    this->vLat.y = tan(fRoll * F_PI / 180.0) * sqrt(this->vLat.x * this->vLat.x +
                                                    this->vLat.z * this->vLat.z);
    this->vLat = glm::normalize(vLat);
    this->fRollSpeed = 0.0;
}

void mnode::setRoll(double dRoll) {
    vLat = glm::normalize(glm::angleAxis(TO_RAD(-dRoll), vDir) * vLat);
    this->updateRoll();
    return;
}

void mnode::updateRoll() {
    this->updateNorm();
    fRoll = glm::atan(vLat.y, -vNorm.y) * 180.0 / F_PI;
    return;
}

void mnode::saveNode(ostream& file) {
    /*writeVec3(&file, glm::vec3(vPos));
    writeVec3(&file, glm::vec3(vDir));*/
    glm::vec3 vLatF = glm::vec3(vLat);
    writeVec3(&file, vLatF);
    float fVelF = (float)fVel;
    writeBytes(&file, (const char*)&fVelF, sizeof(float));
}

void mnode::changePitch(double dAngle, bool inverted) {
    glm::dvec3 rotateAround;
    lenAssert(fabs(vLat.y) < 1.9);
    rotateAround = glm::normalize(glm::cross(glm::dvec3(0.0, vNorm.y, 0.0), vDir));
    if (inverted) {
        rotateAround *= -1.0;
    }
    vDir = glm::normalize(glm::angleAxis(TO_RAD(dAngle), rotateAround) * vDir);
    vLat = glm::normalize(glm::angleAxis(TO_RAD(dAngle), rotateAround) * vLat);
    updateNorm();
}

void mnode::changeYaw(double dAngle) {
    vDir = glm::normalize(
        glm::angleAxis(TO_RAD(dAngle), glm::dvec3(0.0, 1.0, 0.0)) * vDir);
    vLat = glm::normalize(
        glm::angleAxis(TO_RAD(dAngle), glm::dvec3(0.0, 1.0, 0.0)) * vLat);
    this->updateNorm();
}

glm::dvec3 mnode::vLatHeart(double fHeart) {
    if (gloParent && gloParent->mOptions && gloParent->mOptions->useLegacyHeartline) {
        double estimated;
        double estDistFromLast = 0.7 * fHeartDistFromLast + 0.3 * fDistFromLast;

        if (fAngleFromLast < 0.001) {
            estimated = fHeartDistFromLast;
        } else {
            estimated = fVel / F_HZ;
        }
        double fRollSpeedPerMeter =
            estDistFromLast > 0.0 ? (fRollSpeed + fSmoothSpeed) / F_HZ / estimated
                                  : 0.0;
        return glm::normalize(glm::normalize(vLat) -
                              glm::normalize(vDir) * (double)(fRollSpeedPerMeter *
                                                              F_PI * fHeart / 180.0));
    }
    return glm::normalize(glm::cross(vNorm, vDirHeart(fHeart)));
}

glm::dvec3 mnode::vDirHeart(double fHeart) {
    if (gloParent && gloParent->mOptions && gloParent->mOptions->useLegacyHeartline) {
        double estimated;
        if (fAngleFromLast < 0.001) {
            estimated = fHeartDistFromLast;
        } else {
            estimated = fVel / F_HZ;
        }
        double fRollSpeedPerMeter =
            fHeartDistFromLast > 0.0 ? (fRollSpeed + fSmoothSpeed) / F_HZ / estimated
                                     : 0.0;
        if (fRollSpeedPerMeter != fRollSpeedPerMeter)
            fRollSpeedPerMeter = 0.0;
        return glm::normalize(
            vDir + vLat * (double)(fRollSpeedPerMeter * F_PI * fHeart / 180.0));
    }

    double estimated;
    if (fAngleFromLast < 0.001) {
        estimated = fHeartDistFromLast;
    } else {
        estimated = fVel / F_HZ;
    }

    // Roll rate per meter (omega)
    double fRollSpeedPerMeter =
        fHeartDistFromLast > 0.0 ? (fRollSpeed + fSmoothSpeed) / F_HZ / estimated
                                 : 0.0;
    if (fRollSpeedPerMeter != fRollSpeedPerMeter)
        fRollSpeedPerMeter = 0.0;

    // Pitch rate per meter (vertical curvature, kappa_y)
    double fPitchSpeedPerMeter =
        estimated > 0.0 ? fPitchFromLast / estimated : 0.0;
    if (fPitchSpeedPerMeter != fPitchSpeedPerMeter)
        fPitchSpeedPerMeter = 0.0;

    // Convert both rotation rates to radians per meter multiplied by the heartline height
    double rollCorr = fRollSpeedPerMeter * F_PI * fHeart / 180.0;
    double pitchCorr = fPitchSpeedPerMeter * F_PI * fHeart / 180.0;

    // Mathematically exact tangent: C'(s) = (1 + h * kappa_y) * vDir + (h * omega) * vLat
    // Since vNorm points downward, the offset is -fHeart, making the vertical correction (1.0 + pitchCorr)
    return glm::normalize(vDir * (1.0 + pitchCorr) + vLat * rollCorr);
}

void mnode::calcSmoothForces() {
    glm::dvec3 forceVec;
    double temp = cos(fabs(getPitch()) * F_PI / 180.0);
    if (fabs(fAngleFromLast) < std::numeric_limits<double>::epsilon()) {
        forceVec = glm::dvec3(0.0, 1.0, 0.0);
    } else {
        double normalDAngle = F_PI / 180.0 *
                              (-fPitchFromLast * cos(fRoll * F_PI / 180.0) -
                               temp * fYawFromLast * sin(fRoll * F_PI / 180.0));
        double lateralDAngle = F_PI / 180.0 *
                               (fPitchFromLast * sin(fRoll * F_PI / 180.0) -
                                temp * fYawFromLast * cos(fRoll * F_PI / 180.0));
        forceVec = glm::dvec3(0.0, 1.0, 0.0) +
                   lateralDAngle * fVel * F_HZ / F_G * vLat +
                   normalDAngle * fHeartDistFromLast * F_HZ * F_HZ / F_G * vNorm;
    }
    smoothNormal = -glm::dot(forceVec, glm::normalize(vNorm)) - forceNormal;
    smoothLateral = -glm::dot(forceVec, glm::normalize(vLat)) - forceLateral;
}

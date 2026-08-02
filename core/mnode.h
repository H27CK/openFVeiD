#ifndef MNODE_H
#define MNODE_H

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

#include "lenassert.h"
#include <vector>
#include <fstream>
#include <sstream>

#define F_HZ (1000.0)

class mnode {
public:
    mnode();
    mnode(glm::dvec3 getPos, glm::dvec3 getDir, double getRoll, double getVel,
          double getNForce, double getLateral);
    void setRoll(double dRoll);
    void updateRoll();
    void updateNorm() {
        vNorm = glm::cross(vDir, vLat);
    }
    void changePitch(double dAngle, bool inverted);
    void changeYaw(double dAngle);
    double getPitchChange() {
        return fPitchFromLast * F_HZ;
    }
    double getYawChange() {
        return fYawFromLast * F_HZ;
    }
    double fPosHeartx(double fHeart) {
        return vPos.x + vNorm.x * fHeart;
    }
    double fPosHearty(double fHeart) {
        return vPos.y + vNorm.y * fHeart;
    }
    double fPosHeartz(double fHeart) {
        return vPos.z + vNorm.z * fHeart;
    }
    glm::dvec3 vLatHeart(double fHeart);
    glm::dvec3 vDirHeart(double fHeart);
    glm::dvec3 vPosHeart(double fHeart) {
        return vPos + fHeart * vNorm;
    }

    glm::dvec3 vRelPos(double y, double x, double z = 0.0) {
        return vPos - y * vNorm + x * vLatHeart(-y) + z * vDirHeart(-y);
    }

    double getPitch() {
        return glm::atan(vDir.y, glm::sqrt(vDir.x * vDir.x + vDir.z * vDir.z)) *
               180 / F_PI;
    }
    double getDirection() {
        return glm::atan(-vDir.x, -vDir.z) * 180 / F_PI;
    }

    void saveNode(std::ostream& file);

    void calcSmoothForces();

    glm::dvec3 vPos;
    glm::dvec3 vDir;
    glm::dvec3 vLat;
    glm::dvec3 vNorm;
    double fRoll;
    double fVel;
    double fEnergy;
    double forceNormal;
    double forceLateral;
    double smoothNormal;
    double smoothLateral;
    double fDistFromLast;
    double fHeartDistFromLast;
    double fAngleFromLast;
    double fTrackAngleFromLast;
    double fDirFromLast;
    double fPitchFromLast;
    double fYawFromLast;
    double fRollSpeed;
    double fSmoothSpeed;
    double fFlexion() {
        return fDistFromLast <= 0.0 ? 0.0 : fTrackAngleFromLast / fDistFromLast;
    }
    double fTotalLength;
    double fTotalHeartLength;
    bool nearGimbalLock = false;
};

#endif // MNODE_H

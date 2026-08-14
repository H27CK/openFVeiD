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

#include "seccurved.h"
#include "exportfuncs.h"
#include "dummies.h"
#include "mnode.h"
#include <cmath>
#include <iostream>
#include <algorithm>

seccurved::seccurved(track* getParent, mnode* first, float getAngle,
                     float getRadius)
    : section(getParent, curved, first) {
    fAngle = getAngle;
    fRadius = getRadius;
    fLeadIn = fAngle / 3.f > 10.f ? 10.f : fAngle / 3.f;
    fLeadOut = fAngle / 3.f > 10.f ? 10.f : fAngle / 3.f;
    fDirection = 90.0;
    length = 0.0;
    bOrientation = 0;
    bArgument = TIME;
    bSpeed = 0;
    fVel = 10;
    rollFunc->setMaxArgument(fAngle);
}

void seccurved::changecurve(float newAngle, float newRadius,
                            float newDirection) {
    fAngle = newAngle;
    fRadius = newRadius;
    fDirection = newDirection;
    length = 0.0;
    updateSection();
}

int seccurved::updateSection(int node) {
    chaseCurves();
    bool stalled = false;
    length = 0.0;
    int numNodes = 1;
    float fRiddenAngle = 0.0;
    float artificialRoll = 0.0;

    fAngle = getMaxArgument();

    if (lNodes.size() > 1) {
        lNodes.erase(lNodes.begin() + 1, lNodes.end());
    }
    if (lAngles.size() > 1) {
        lAngles.erase(lAngles.begin() + 1, lAngles.end());
    }

    int sizediff = (int)lNodes.size() - (int)lAngles.size();
    for (int i = 0; i <= sizediff; ++i) {
        lAngles.push_back(0.f);
    }
    lAngles[0] = 0.f;
    lNodes[0].updateNorm();

    float diff = lNodes[0].fRollSpeed; // - rollFunc->funcList.at(0)]-startValue;
    if (bOrientation == 1) {
        diff += glm::dot(lNodes[0].vDir, glm::dvec3(0.0, 1.0, 0.0)) *
                lNodes[0].getYawChange();
    }
    rollFunc->funcList.at(0)->translateValues(diff);
    rollFunc->translateValues(rollFunc->funcList.at(0));

    int leadOutNodeIdx = -1;
    float myLeadOut = 0.f;

    while (fRiddenAngle < fAngle - std::numeric_limits<float>::epsilon()) {
        float deltaAngle, fTrans;

        mnode* prevNode = &lNodes[numNodes - 1];

        deltaAngle = prevNode->fVel / fRadius / F_HZ * 180 / F_PI;

        if (fLeadIn > 0.f &&
            (fTrans = (prevNode->fTotalLength - lNodes[0].fTotalLength) /
                      (1.997f / F_HZ * prevNode->fVel / deltaAngle * fLeadIn)) <=
                1.f) {
            deltaAngle *= fTrans * fTrans * (3 + fTrans * (-2));
        }

        if (leadOutNodeIdx == -1 && fRiddenAngle > fAngle - fLeadOut) {
            leadOutNodeIdx = numNodes - 1;
            myLeadOut = fAngle - fRiddenAngle;
        }
        if (leadOutNodeIdx != -1 && fLeadOut > 0.f) {
            if ((fTrans = 1.f - (prevNode->fTotalLength - lNodes[leadOutNodeIdx].fTotalLength) /
                                    (1.997f / F_HZ * prevNode->fVel / deltaAngle *
                                     myLeadOut)) >= 0.f) {
                deltaAngle *= fTrans * fTrans * (3 + fTrans * (-2));
            } else {
                break;
            }
        }

        lNodes.push_back(*prevNode);

        mnode* curNode = &lNodes[numNodes];
        prevNode = &lNodes[numNodes - 1]; // in case vector gets copied

        fRiddenAngle += deltaAngle;
        lAngles.push_back(fRiddenAngle);

        curNode->updateNorm();

        float fPureDirection = fDirection - artificialRoll; //- curNode->fRoll;

        curNode->vDir =
            glm::angleAxis(TO_RAD(deltaAngle),
                           glm::cos(-fPureDirection * F_PI / 180) *
                                   prevNode->vLat +
                               glm::sin(-fPureDirection * F_PI / 180) *
                                   prevNode->vNorm) *
            prevNode->vDir;
        curNode->vLat =
            glm::angleAxis(TO_RAD(deltaAngle),
                           glm::cos(-fPureDirection * F_PI / 180) *
                                   prevNode->vLat +
                               glm::sin(-fPureDirection * F_PI / 180) *
                                   prevNode->vNorm) *
            prevNode->vLat;
        curNode->vDir = glm::normalize(curNode->vDir);
        curNode->vLat = glm::normalize(curNode->vLat);

        curNode->updateNorm();

        curNode->vPos += curNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         prevNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         (prevNode->vPosHeart(parent->fHeart) -
                          curNode->vPosHeart(parent->fHeart));

        curNode->setRoll(rollFunc->getValue(fRiddenAngle) / F_HZ);
        curNode->fRollSpeed = rollFunc->getValue(fRiddenAngle);
        artificialRoll += rollFunc->getValue(fRiddenAngle) / F_HZ;

        if (bOrientation == EULER) {
            calcDirFromLast(numNodes);
            lNodes[numNodes].setRoll(
                glm::dot(lNodes[numNodes].vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                lNodes[numNodes].fYawFromLast);
            artificialRoll +=
                glm::dot(lNodes[numNodes].vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                lNodes[numNodes].fYawFromLast;
            curNode->fRollSpeed +=
                glm::dot(lNodes[numNodes].vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                lNodes[numNodes].fYawFromLast * F_HZ;
        }

        curNode->updateNorm();

        if (bSpeed) {
            curNode->fEnergy -= (curNode->fVel * curNode->fVel * curNode->fVel /
                                 F_HZ * parent->fResistance);
            double energyValue = curNode->fEnergy -
                                 F_G * (curNode->vPosHeart(parent->fHeart * 0.9f).y +
                                        curNode->fTotalLength * parent->fFriction);
            double minSpeed = (double)gloParent->mOptions->stallSpeed;
            if (energyValue <= 0.0) {
                curNode->fVel = minSpeed;
                stalled = true;
            } else {
                curNode->fVel = sqrt(2.f * energyValue);
                if (curNode->fVel < minSpeed) {
                    curNode->fVel = minSpeed;
                    stalled = true;
                }
            }
        } else {
            if (this->fAccel == 0.0) {
                curNode->fVel = this->fVel;
            } else {
                double minSpeed = (double)gloParent->mOptions->stallSpeed;
                curNode->fVel = std::max(minSpeed, prevNode->fVel + this->fAccel / F_HZ);
            }
            curNode->fEnergy = 0.5 * curNode->fVel * curNode->fVel +
                               F_G * (curNode->vPosHeart(parent->fHeart * 0.9f).y +
                                      curNode->fTotalLength * parent->fFriction);
        }

        curNode->updateRoll();

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength += curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength += curNode->fHeartDistFromLast;

        calcDirFromLast(numNodes);

        float temp = cos(fabs(lNodes[numNodes].getPitch()) * F_PI / 180.f);
        float forceAngle =
            sqrt(temp * temp * curNode->fYawFromLast * curNode->fYawFromLast +
                 curNode->fPitchFromLast * curNode->fPitchFromLast); // deltaAngle;
        curNode->fAngleFromLast = forceAngle;

        glm::dvec3 forceVec;
        if (fabs(curNode->fAngleFromLast) < std::numeric_limits<float>::epsilon()) {
            forceVec = glm::dvec3(0.0, 1.0, 0.0);
        } else {
            float normalDAngle =
                F_PI / 180.f *
                (-curNode->fPitchFromLast * cos(curNode->fRoll * F_PI / 180.) -
                 temp * curNode->fYawFromLast * sin(curNode->fRoll * F_PI / 180.));
            float lateralDAngle =
                F_PI / 180.f *
                (curNode->fPitchFromLast * sin(curNode->fRoll * F_PI / 180.) -
                 temp * curNode->fYawFromLast * cos(curNode->fRoll * F_PI / 180.));

            forceVec = glm::dvec3(0.0, 1.0, 0.0) +
                       lateralDAngle * curNode->fVel * F_HZ / F_G * curNode->vLat +
                       normalDAngle * curNode->fHeartDistFromLast * F_HZ * F_HZ /
                           F_G * curNode->vNorm;
        }

        curNode->forceNormal = -glm::dot(forceVec, glm::normalize(curNode->vNorm));
        curNode->forceLateral = -glm::dot(forceVec, glm::normalize(curNode->vLat));

        numNodes++;
    }

    if (fLeadOut > 0.0001f) {
        lNodes.back().fAngleFromLast = 0.f;
        lNodes.back().fPitchFromLast = 0.f;
        lNodes.back().fYawFromLast = 0.f;
    }
    if (!lNodes.empty())
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    else
        length = 0;
    return 0;
}

double seccurved::getMaxArgument() {
    return rollFunc->getMaxArgument();
}

void seccurved::saveSection(std::ostream& file) {
    file << "CUR";
    writeBytes(&file, (const char*)&bSpeed, sizeof(bool));

    int namelength = sName.length();
    std::string name = sName;

    writeBytes(&file, (const char*)&namelength, sizeof(int));
    file << name;

    float tempVel = (float)fVel;
    float tempAngle = (float)fAngle;
    float tempRadius = (float)fRadius;
    float tempDirection = (float)fDirection;
    float tempLeadIn = (float)fLeadIn;
    float tempLeadOut = (float)fLeadOut;
    float tempAccel = (float)fAccel;

    writeBytes(&file, (const char*)&tempVel, sizeof(float));
    writeBytes(&file, (const char*)&tempAngle, sizeof(float));
    writeBytes(&file, (const char*)&tempRadius, sizeof(float));
    writeBytes(&file, (const char*)&tempDirection, sizeof(float));
    writeBytes(&file, (const char*)&tempLeadIn, sizeof(float));
    writeBytes(&file, (const char*)&tempLeadOut, sizeof(float));
    writeBytes(&file, (const char*)&bOrientation, sizeof(bool));
    writeBytes(&file, (const char*)&tempAccel, sizeof(float));
    rollFunc->saveFunction(file);
}

void seccurved::loadSection(std::istream& file) {
    bSpeed = readBool(&file);

    int namelength = readInt(&file);
    sName = readString(&file, namelength);

    fVel = std::max(0.01, (double)readFloat(&file));
    fAngle = std::max(0.1, (double)readFloat(&file));
    fRadius = std::max(0.1, (double)readFloat(&file));
    fDirection = std::clamp((double)readFloat(&file), -180.0, 180.0);
    fLeadIn = std::max(0.0, (double)readFloat(&file));
    fLeadOut = std::max(0.0, (double)readFloat(&file));
    bOrientation = readBool(&file);

    // Safely check if there are bytes left to load fAccel
    if (file.peek() != EOF) {
        fAccel = (double)readFloat(&file);
    } else {
        fAccel = 0.0;
    }
    rollFunc->loadFunction(file);
}

bool seccurved::isInFunction(int index, subfunc* func) {
    if (func == NULL)
        return false;
    float angle = lAngles[index];
    if (angle >= func->minArgument && angle <= func->maxArgument) {
        return true;
    }
    return false;
}

bool seccurved::isLockable(func* _func) {
    (void)_func;
    return false;
}

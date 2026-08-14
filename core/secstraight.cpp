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

#include "secstraight.h"
#include "exportfuncs.h"
#include "dummies.h"
#include "mnode.h"
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace std;

secstraight::secstraight(track* getParent, mnode* first, float getlength)
    : section(getParent, straight, first) {
    this->fHLength = getlength;
    this->bArgument = TIME;
    this->bOrientation = QUATERNION;
    bSpeed = 0;
    fVel = 10;
}

void secstraight::changelength(float newlength) {
    this->fHLength = newlength;
    this->updateSection();
}

int secstraight::updateSection(int node) {
    chaseCurves();
    bool stalled = false;
    int numNodes = 1;
    this->length = 0;
    fHLength = getMaxArgument();

    if (lNodes.size() > 1) {
        lNodes.erase(lNodes.begin() + 1, lNodes.end());
    }

    lNodes[0].updateNorm();

    float diff = lNodes[0].fRollSpeed; // - rollFunc->funcList.at(0)]-startValue;
    rollFunc->funcList.front()->translateValues(diff);
    rollFunc->translateValues(rollFunc->funcList.front());

    bool lastNode = false;

    float fCurLength = 0.0f;

    while (fCurLength < this->fHLength - std::numeric_limits<float>::epsilon() &&
           !lastNode) {
        lNodes.push_back(lNodes.back());

        float dTime;
        mnode* prevNode = &lNodes[numNodes - 1];
        mnode* curNode = &lNodes[numNodes];

        if (curNode->fVel < 1.0) {
            curNode->fVel = 1.0;
            stalled = true;
        }
        if (curNode->fVel / F_HZ < this->fHLength - fCurLength) {
            dTime = F_HZ;
        } else {
            lastNode = true;
            dTime = (curNode->fVel + std::numeric_limits<float>::epsilon()) /
                    (this->fHLength - fCurLength);
        }

        curNode->vPos += curNode->vDir * (curNode->fVel / dTime);

        fCurLength += curNode->fVel / dTime;

        curNode->setRoll(
            rollFunc->getValue(fCurLength) /
            dTime); // rollFunc->getValue((i+1)/10.0) - rollFunc->getValue(i/10.0));

        curNode->forceNormal = -curNode->vNorm.y;
        curNode->forceLateral = -curNode->vLat.y;

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength += curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength += curNode->fHeartDistFromLast;

        curNode->fRollSpeed = rollFunc->getValue(fCurLength);

        calcDirFromLast(numNodes);
        curNode->fAngleFromLast = 0.0;
        curNode->fDirFromLast = 0.0;
        curNode->fYawFromLast = 0.0;
        curNode->fPitchFromLast = 0.0;
        if (fabs(lNodes[numNodes].fRollSpeed) < 0.001) {
            curNode->fTrackAngleFromLast = 0.0;
        }

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

        this->length += curNode->fDistFromLast;
        ++numNodes;
    }

    while ((int)lNodes.size() > numNodes) {
        lNodes.pop_back();
    }

    if (!lNodes.empty())
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    else
        length = 0;

    this->isStalled = stalled;
    return 0;
}

double secstraight::getMaxArgument() {
    return rollFunc->getMaxArgument();
}

void secstraight::saveSection(std::ostream& file) {
    file << "STR";
    writeBytes(&file, (const char*)&bSpeed, sizeof(bool));

    int namelength = (int)sName.length();
    std::string name = sName;

    writeBytes(&file, (const char*)&namelength, sizeof(int));
    file << name;

    float tempVel = (float)fVel;
    float tempLength = (float)fHLength;
    float tempAccel = (float)fAccel;
    writeBytes(&file, (const char*)&tempVel, sizeof(float));
    writeBytes(&file, (const char*)&tempLength, sizeof(float));
    writeBytes(&file, (const char*)&tempAccel, sizeof(float));
    rollFunc->saveFunction(file);
}

void secstraight::loadSection(std::istream& file) {
    bSpeed = readBool(&file);

    int namelength = readInt(&file);
    sName = readString(&file, namelength);

    fVel = std::max(0.01, (double)readFloat(&file));
    fHLength = std::max(0.1, (double)readFloat(&file));

    // Safely check if there are bytes left to load fAccel
    if (file.peek() != EOF) {
        fAccel = (double)readFloat(&file);
    } else {
        fAccel = 0.0;
    }
    rollFunc->loadFunction(file);
}

bool secstraight::isInFunction(int index, subfunc* func) {
    if (func == NULL)
        return false;
    if (index >= (int)lNodes.size())
        return false;
    float dist = lNodes[index].fTotalHeartLength - lNodes[0].fTotalHeartLength;
    if (dist >= func->minArgument && dist <= func->maxArgument) {
        return true;
    }
    return false;
}

bool secstraight::isLockable(func* _func) {
    (void)_func;
    return false;
}

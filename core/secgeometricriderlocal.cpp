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

#include "secgeometricriderlocal.h"
#include "exportfuncs.h"
#include "dummies.h"
#include "lenassert.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>

secgeometricriderlocal::~secgeometricriderlocal() {
    delete normForce;
    delete latForce;
}

secgeometricriderlocal::secgeometricriderlocal(track* getParent, mnode* first, float gettime)
    : section(getParent, geometricriderlocal, first) {
    this->iTime = (int)(gettime + 0.5);
    this->length = 0.0;
    rollFunc->changeLength(1.f, 0);

    double firstPitchChange = 0.0;
    double firstYawChange = 0.0;

    if (getParent && !getParent->lSections.empty()) {
        section* prevSec = getParent->lSections.back();
        if (prevSec && prevSec->lNodes.size() >= 2) {
            mnode* prevNode = &prevSec->lNodes.at(prevSec->lNodes.size() - 2);
            glm::dvec3 diff = first->vDir - prevNode->vDir;
            firstPitchChange = -glm::dot(diff, prevNode->vNorm) * 180.0 / F_PI * F_HZ;
            firstYawChange = -glm::dot(diff, prevNode->vLat) * 180.0 / F_PI * F_HZ;
        }
    }

    normForce = new func(0, 1, firstPitchChange, firstPitchChange, this, funcPitch);
    latForce = new func(0, 1, firstYawChange, firstYawChange, this, funcYaw);

    this->bOrientation = 0;
    this->bArgument = TIME;
    bSpeed = 1;
    fVel = 10;
    fAccel = 0;
    sName = "Unnamed";
    lNodes.reserve(1000);
    lNodes.push_back(*first);
    updateSection();
}

int secgeometricriderlocal::updateSection(int node) {
    chaseCurves();
    bool stalled = false;
    if (rollFunc->lockedFunc() != -1) {
        if (fabs(rollFunc->funcList.back()->symArg) > 0.00001f &&
            rollFunc->funcList.back()->minArgument * F_HZ < node)
            node = (int)(F_HZ * rollFunc->funcList.back()->minArgument - 1.5f);
    }
    if (normForce->lockedFunc() != -1) {
        if (fabs(normForce->funcList.back()->symArg) > 0.00001f &&
            normForce->funcList.back()->minArgument * F_HZ < node)
            node = (int)(F_HZ * normForce->funcList.back()->minArgument - 1.5f);
    }
    if (latForce->lockedFunc() != -1) {
        if (fabs(latForce->funcList.back()->symArg) > 0.00001f &&
            latForce->funcList.back()->minArgument * F_HZ < node)
            node = (int)(F_HZ * latForce->funcList.back()->minArgument - 1.5f);
    }

    if (bArgument == DISTANCE) {
        return updateDistanceSection(node);
    }

    node = node > (int)lNodes.size() - 2 ? (int)lNodes.size() - 2 : node;
    node = node < 0 ? 0 : node;

    int numNodes = (int)(getMaxArgument() * F_HZ + 0.5);
    iTime = numNodes;

    if (node >= (int)lNodes.size() - 1 && node > 0) {
        node = (int)lNodes.size() - 2;
    }

    if (lNodes.size() > 1 &&
        !this->parent->lSections.empty() &&
        this->parent->lSections.at(this->parent->lSections.size() - 1) != this) {
        lNodes.pop_back(); // disjoint this section from the next one
    }

    if (node == 0) {
        lNodes[0].updateNorm();

        double firstPitchChange = 0.0;
        double firstYawChange = 0.0;

        if (parent && !parent->lSections.empty()) {
            section* prevSec = nullptr;
            int myIdx = -1;
            for (int k = 0; k < (int)parent->lSections.size(); ++k) {
                if (parent->lSections.at(k) == this) {
                    myIdx = k;
                    break;
                }
            }
            if (myIdx > 0) {
                prevSec = parent->lSections.at(myIdx - 1);
            } else if (myIdx == -1 && !parent->lSections.empty()) {
                prevSec = parent->lSections.back();
            }
            if (prevSec && prevSec->lNodes.size() >= 2) {
                mnode* prevNode = &prevSec->lNodes.at(prevSec->lNodes.size() - 2);
                glm::dvec3 diff = lNodes[0].vDir - prevNode->vDir;
                firstPitchChange = -glm::dot(diff, prevNode->vNorm) * 180.0 / F_PI * F_HZ;
                firstYawChange = -glm::dot(diff, prevNode->vLat) * 180.0 / F_PI * F_HZ;
            }
        }

        lenAssert(firstPitchChange == firstPitchChange);
        normForce->funcList.front()->translateValues(firstPitchChange);
        normForce->translateValues(normForce->funcList.front());

        lenAssert(firstYawChange == firstYawChange);
        latForce->funcList.front()->translateValues(firstYawChange);
        latForce->translateValues(latForce->funcList.front());

        double firstRollSpeed = lNodes[0].fRollSpeed;
        rollFunc->funcList.front()->translateValues(firstRollSpeed);
        rollFunc->translateValues(rollFunc->funcList.front());
    }

    int i;
    for (i = node; i < numNodes; i++) {
        if (i >= (int)lNodes.size() - 1) {
            lNodes.push_back(lNodes[i]);
        }

        mnode* prevNode = &lNodes[i];
        mnode* curNode = &lNodes[i + 1];

        curNode->vPos = prevNode->vPos;
        curNode->vDir = prevNode->vDir;
        curNode->vLat = prevNode->vLat;
        curNode->vNorm = prevNode->vNorm;
        curNode->fVel = prevNode->fVel;
        curNode->fEnergy = prevNode->fEnergy;

        float pitchChange = normForce->getValue((float)(i + 1) / F_HZ) / F_HZ;
        float yawChange = latForce->getValue((float)(i + 1) / F_HZ) / F_HZ;

        // Apply Rider-Local pitch and yaw rotations
        if (fabs(pitchChange) > 1e-9) {
            glm::dquat pitchRot = glm::angleAxis(TO_RAD(pitchChange), curNode->vLat);
            curNode->vDir = glm::normalize(pitchRot * curNode->vDir);
            curNode->vNorm = glm::normalize(pitchRot * curNode->vNorm);
        }

        if (fabs(yawChange) > 1e-9) {
            glm::dquat yawRot = glm::angleAxis(TO_RAD(-yawChange), curNode->vNorm);
            curNode->vDir = glm::normalize(yawRot * curNode->vDir);
            curNode->vLat = glm::normalize(yawRot * curNode->vLat);
        }

        // Keep normal update clean
        curNode->updateNorm();

        // Integrate Position
        curNode->vPos += curNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         prevNode->vDir * (prevNode->fVel / (2.f * F_HZ)) +
                         (prevNode->vPosHeart(parent->fHeart) -
                          curNode->vPosHeart(parent->fHeart));

        curNode->updateNorm();

        // Apply local banking change
        curNode->setRoll(rollFunc->getValue((float)(i + 1) / F_HZ) / F_HZ);
        curNode->updateNorm();

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength =
            prevNode->fTotalHeartLength + curNode->fHeartDistFromLast;
        curNode->fRollSpeed = rollFunc->getValue((float)(i + 1) / F_HZ);

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

        calcDirFromLast(i + 1);
        float temp = cos(fabs(curNode->getPitch()) * F_PI / 180.f);
        float forceAngle =
            sqrt(temp * temp * curNode->fYawFromLast * curNode->fYawFromLast +
                 curNode->fPitchFromLast * curNode->fPitchFromLast);
        curNode->fAngleFromLast = forceAngle;

        // Calculate forces
        glm::dvec3 forceVec;
        double deltaAngle = sqrt(pitchChange * pitchChange + yawChange * yawChange);
        if (fabs(deltaAngle) < std::numeric_limits<float>::epsilon()) {
            forceVec = glm::dvec3(0.0, 1.0, 0.0);
        } else {
            // Project the rate of change onto the local frame for forces
            float normalDAngle = F_PI / 180.f * -pitchChange;
            float lateralDAngle = F_PI / 180.f * -yawChange;

            forceVec = glm::dvec3(0.0, 1.0, 0.0) +
                       lateralDAngle * curNode->fVel * F_HZ / F_G * curNode->vLat +
                       normalDAngle * curNode->fHeartDistFromLast * F_HZ * F_HZ /
                           F_G * curNode->vNorm;
        }
        curNode->forceNormal = -glm::dot(forceVec, glm::normalize(curNode->vNorm));
        curNode->forceLateral = -glm::dot(forceVec, glm::normalize(curNode->vLat));
    }
    if (lNodes.size() > (size_t)(1 + i)) {
        lNodes.erase(lNodes.begin() + 1 + i, lNodes.end());
    }
    if (!lNodes.empty()) {
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    } else
        length = 0;
    this->isStalled = stalled;
    return node;
}

int secgeometricriderlocal::updateDistanceSection(int node) {
    bool stalled = false;
    node = node < 0 ? 0 : node;

    int i = 0;
    this->length = 0.f;
    float hDist = 0.f;
    while (length < (float)node / F_HZ && i + 1 < (int)lNodes.size()) {
        hDist += lNodes[++i].fHeartDistFromLast;
        length += lNodes[i].fDistFromLast;
    }

    if (i >= (int)lNodes.size() - 1 && i > 0) {
        i = (int)lNodes.size() - 2;
    }

    if (lNodes.size() > 1 &&
        !this->parent->lSections.empty() &&
        this->parent->lSections.at(this->parent->lSections.size() - 1) != this) {
        lNodes.pop_back(); // disjoint this section from the next one
    }

    if (i == 0) {
        lNodes[(0)].updateNorm();

        double firstPitchChange = 0.0;
        double firstYawChange = 0.0;

        if (parent && !parent->lSections.empty()) {
            section* prevSec = nullptr;
            int myIdx = -1;
            for (int k = 0; k < (int)parent->lSections.size(); ++k) {
                if (parent->lSections.at(k) == this) {
                    myIdx = k;
                    break;
                }
            }
            if (myIdx > 0) {
                prevSec = parent->lSections.at(myIdx - 1);
            } else if (myIdx == -1 && !parent->lSections.empty()) {
                prevSec = parent->lSections.back();
            }
            if (prevSec && prevSec->lNodes.size() >= 2) {
                mnode* prevNode = &prevSec->lNodes.at(prevSec->lNodes.size() - 2);
                glm::dvec3 diff = lNodes[0].vDir - prevNode->vDir;
                firstPitchChange = -glm::dot(diff, prevNode->vNorm) * 180.0 / F_PI * F_HZ;
                firstYawChange = -glm::dot(diff, prevNode->vLat) * 180.0 / F_PI * F_HZ;
            }
        }

        double diff = firstPitchChange / lNodes[0].fVel;
        lenAssert(diff == diff);
        normForce->funcList.front()->translateValues(diff);
        normForce->translateValues(normForce->funcList.front());

        diff = firstYawChange / lNodes[0].fVel;
        lenAssert(diff == diff);
        latForce->funcList.front()->translateValues(diff);
        latForce->translateValues(latForce->funcList.front());

        diff = lNodes[0].fRollSpeed / lNodes[0].fVel;
        rollFunc->funcList.front()->translateValues(diff);
        rollFunc->translateValues(rollFunc->funcList.front());
    }

    int returnval = i;
    float end = this->getMaxArgument();

    while (length < end) {
        if (i >= (int)lNodes.size() - 1) {
            lNodes.push_back(lNodes[i]);
        }

        mnode* prevNode = &lNodes[i];
        mnode* curNode = &lNodes[i + 1];

        curNode->vPos = prevNode->vPos;
        curNode->vDir = prevNode->vDir;
        curNode->vLat = prevNode->vLat;
        curNode->vNorm = prevNode->vNorm;
        curNode->fVel = prevNode->fVel;
        curNode->fEnergy = prevNode->fEnergy;

        float pitchChange = normForce->getValue(length + curNode->fVel / F_HZ) *
                            (curNode->fVel / F_HZ);
        float yawChange = latForce->getValue(length + curNode->fVel / F_HZ) *
                          (curNode->fVel / F_HZ);

        // Apply Rider-Local pitch and yaw rotations
        if (fabs(pitchChange) > 1e-9) {
            glm::dquat pitchRot = glm::angleAxis(TO_RAD(pitchChange), curNode->vLat);
            curNode->vDir = glm::normalize(pitchRot * curNode->vDir);
            curNode->vNorm = glm::normalize(pitchRot * curNode->vNorm);
        }

        if (fabs(yawChange) > 1e-9) {
            glm::dquat yawRot = glm::angleAxis(TO_RAD(-yawChange), curNode->vNorm);
            curNode->vDir = glm::normalize(yawRot * curNode->vDir);
            curNode->vLat = glm::normalize(yawRot * curNode->vLat);
        }

        // Keep normal update clean
        curNode->updateNorm();

        // Integrate Position
        curNode->vPos += curNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         prevNode->vDir * (prevNode->fVel / (2.f * F_HZ)) +
                         (prevNode->vPosHeart(parent->fHeart) -
                          curNode->vPosHeart(parent->fHeart));

        curNode->updateNorm();

        // Apply local banking change
        curNode->setRoll(rollFunc->getValue(length + curNode->fVel / F_HZ) *
                         (curNode->fVel / F_HZ));
        curNode->updateNorm();

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength =
            prevNode->fTotalHeartLength + curNode->fHeartDistFromLast;
        curNode->fRollSpeed =
            rollFunc->getValue(length + curNode->fVel / F_HZ) * curNode->fVel;

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

        calcDirFromLast(i + 1);
        float temp = cos(fabs(curNode->getPitch()) * F_PI / 180.f);
        float forceAngle =
            sqrt(temp * temp * curNode->fYawFromLast * curNode->fYawFromLast +
                 curNode->fPitchFromLast * curNode->fPitchFromLast);
        curNode->fAngleFromLast = forceAngle;

        // Calculate forces
        glm::dvec3 forceVec;
        double deltaAngle = sqrt(pitchChange * pitchChange + yawChange * yawChange);
        if (fabs(deltaAngle) < std::numeric_limits<float>::epsilon()) {
            forceVec = glm::dvec3(0.0, 1.0, 0.0);
        } else {
            // Project the rate of change onto the local frame for forces
            float normalDAngle = F_PI / 180.f * -pitchChange;
            float lateralDAngle = F_PI / 180.f * -yawChange;

            forceVec = glm::dvec3(0.0, 1.0, 0.0) +
                       lateralDAngle * curNode->fVel * F_HZ / F_G * curNode->vLat +
                       normalDAngle * curNode->fHeartDistFromLast * F_HZ * F_HZ /
                           F_G * curNode->vNorm;
        }
        curNode->forceNormal = -glm::dot(forceVec, glm::normalize(curNode->vNorm));
        curNode->forceLateral = -glm::dot(forceVec, glm::normalize(curNode->vLat));

        length = curNode->fTotalLength - lNodes.front().fTotalLength;
        i++;
    }

    if (lNodes.size() > (size_t)(1 + i)) {
        lNodes.erase(lNodes.begin() + 1 + i, lNodes.end());
    }
    if (!lNodes.empty()) {
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    } else
        length = 0;
    this->isStalled = stalled;
    return returnval;
}

double secgeometricriderlocal::getMaxArgument() {
    double min = std::numeric_limits<double>::max();
    if (rollFunc->lockedFunc() == -1) {
        min = rollFunc->getMaxArgument();
    }
    if (normForce->lockedFunc() == -1) {
        double m = normForce->getMaxArgument();
        min = m < min ? m : min;
    }
    if (latForce->lockedFunc() == -1) {
        double m = latForce->getMaxArgument();
        min = m < min ? m : min;
    }
    return min;
}

void secgeometricriderlocal::saveSection(std::ostream& file) {
    file << "GRL";
    writeBytes(&file, (const char*)&bSpeed, sizeof(bool));

    int namelength = (int)sName.length();
    std::string name = sName;

    writeBytes(&file, (const char*)&namelength, sizeof(int));
    file << name;

    float tempVel = (float)fVel;
    float tempAccel = (float)fAccel;
    writeBytes(&file, (const char*)&tempVel, sizeof(float));
    writeBytes(&file, (const char*)&iTime, sizeof(int));
    writeBytes(&file, (const char*)&bOrientation, sizeof(bool));
    writeBytes(&file, (const char*)&bArgument, sizeof(bool));
    writeBytes(&file, (const char*)&tempAccel, sizeof(float));
    rollFunc->saveFunction(file);
    normForce->saveFunction(file);
    latForce->saveFunction(file);
}

void secgeometricriderlocal::loadSection(std::istream& file) {
    bSpeed = readBool(&file);

    int namelength = readInt(&file);
    sName = readString(&file, namelength);

    fVel = std::max(0.01, (double)readFloat(&file));
    iTime = readInt(&file);
    bOrientation = readBool(&file);
    bArgument = readBool(&file);

    if (file.peek() != EOF) {
        fAccel = (double)readFloat(&file);
    } else {
        fAccel = 0.0;
    }
    rollFunc->loadFunction(file);
    normForce->loadFunction(file);
    latForce->loadFunction(file);
}

bool secgeometricriderlocal::isInFunction(int index, subfunc* func) {
    if (func == NULL)
        return false;
    if (bArgument == TIME) {
        if (func->parent == normForce) {
            return (func->minArgument * F_HZ <= index) &&
                   (func->maxArgument * F_HZ >= index);
        } else if (func->parent == latForce) {
            return (func->minArgument * F_HZ <= index) &&
                   (func->maxArgument * F_HZ >= index);
        } else if (func->parent == rollFunc) {
            return (func->minArgument * F_HZ <= index) &&
                   (func->maxArgument * F_HZ >= index);
        }
    } else {
        if (index >= (int)lNodes.size())
            return false;
        double arg = lNodes[index].fTotalLength - lNodes[0].fTotalLength;
        return (func->minArgument <= arg) && (func->maxArgument >= arg);
    }
    return false;
}

bool secgeometricriderlocal::isLockable(func* _func) {
    if (_func == normForce || _func == latForce || _func == rollFunc)
        return true;
    return false;
}

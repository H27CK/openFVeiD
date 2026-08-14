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

#include "secgeometric.h"
#include "common.h"
#include "exportfuncs.h"
#include "dummies.h"
#include <algorithm>
#include <iostream>
#include <cmath>

secgeometric::~secgeometric() {
    delete normForce;
    delete latForce;
}

secgeometric::secgeometric(track* getParent, mnode* first, float gettime)
    : section(getParent, geometric, first) {
    this->iTime = (int)(gettime + 0.5);
    this->length = 0.0;
    rollFunc->changeLength(1.f, 0);
    float deltaPitch = lNodes[0].getPitchChange();
    float deltaYaw = lNodes[0].getYawChange();
    normForce = new func(0, 1, deltaPitch, deltaPitch, this, funcPitch);
    latForce = new func(0, 1, deltaYaw, deltaYaw, this, funcYaw);

    this->bOrientation = EULER;
    this->bArgument = TIME;
    bSpeed = 1;
    fVel = 10;
}

int secgeometric::updateSection(int node) {
    chaseCurves();
    bool stalled = false;
    bool restricted = false;
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
        this->parent->lSections.at(this->parent->lSections.size() - 1) != this) {
        lNodes.pop_back(); // disjoint this section from the next one
    }

    if (node == 0) {
        lNodes[0].updateNorm();

        float diff =
            lNodes[0].getPitchChange(); // - normForce->funcList.at(0)->startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        normForce->funcList.front()->translateValues(diff);
        normForce->translateValues(normForce->funcList.front());

        diff = lNodes[0].getYawChange(); // - latForce->funcList.at(0)->startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        latForce->funcList.front()->translateValues(diff);
        latForce->translateValues(latForce->funcList.front());

        diff = lNodes[0].fRollSpeed; // - rollFunc->funcList.at(0)->startValue;
        if (bOrientation == 1) {
            diff += glm::dot(lNodes[0].vDir, glm::dvec3(0.0, 1.0, 0.0)) *
                    lNodes[0].getYawChange();
        }
        rollFunc->funcList.front()->translateValues(diff);
        rollFunc->translateValues(rollFunc->funcList.front());
    }

    float artificialRoll = lNodes[0].fRoll;
    for (int i = 0; i < node; ++i) {
        if (bOrientation == 0) {
            artificialRoll -=
                glm::dot(lNodes[i + 1].vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                latForce->getValue((float)(i + 1) / F_HZ) / F_HZ;
        }
        artificialRoll += rollFunc->getValue((float)(i + 1) / F_HZ) / F_HZ;
        while (artificialRoll > 180.f) {
            artificialRoll -= 360.f;
        }
        while (artificialRoll < -180.f) {
            artificialRoll += 360.f;
        }
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

        double requestedPitchForce = normForce->getValue((float)(i + 1) / F_HZ);
        double requestedYawForce = latForce->getValue((float)(i + 1) / F_HZ);

        if (parent->enableForceLimits) {
            if (requestedPitchForce > parent->fMaxPosNormal || requestedPitchForce < parent->fMaxNegNormal || requestedYawForce > parent->fMaxLateral || requestedYawForce < parent->fMinLateral) {
                restricted = true;
            }
        }

        float pitchChange = requestedPitchForce / F_HZ;
        float yawChange = requestedYawForce / F_HZ;
        int sign = 1;
        if (fabs(artificialRoll) >= 90.f) {
            sign = -1;
        }

        curNode->changePitch(pitchChange, sign == -1);
        curNode->changeYaw(yawChange);

        float pureYawChange =
            (1.f - fabs(glm::dot(curNode->vDir, glm::dvec3(0.0, 1.0, 0.0)))) *
            yawChange;
        float pureRollChange =
            glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) * yawChange * F_HZ;
        float deltaAngle =
            sqrt(pitchChange * pitchChange + pureYawChange * pureYawChange);

        curNode->setRoll(-pureRollChange / F_HZ);
        artificialRoll -= pureRollChange / F_HZ;

        curNode->vPos += curNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         prevNode->vDir * (prevNode->fVel / (2.f * F_HZ)) +
                         (prevNode->vPosHeart(parent->fHeart) -
                          curNode->vPosHeart(parent->fHeart));

        curNode->updateNorm();

        curNode->setRoll(
            rollFunc->getValue((float)(i + 1) / F_HZ) /
            F_HZ); // rollFunc->getValue((float)(i+1)/numNodes*fAngle));
                   // //360./numNodes*(i+1));

        if (bOrientation == EULER ||
            rollFunc->getSubfunc((float)(i + 1) / F_HZ)->degree == tozero) {
            curNode->setRoll(+pureRollChange / F_HZ);
            artificialRoll += pureRollChange / F_HZ;
        }

        artificialRoll += rollFunc->getValue((float)(i + 1) / F_HZ) / F_HZ;
        while (artificialRoll > 180.f) {
            artificialRoll -= 360.f;
        }
        while (artificialRoll < -180.f) {
            artificialRoll += 360.f;
        }
        curNode->updateNorm();

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength =
            prevNode->fTotalHeartLength + curNode->fHeartDistFromLast;
        curNode->fRollSpeed = rollFunc->getValue((float)(i + 1) / F_HZ);

        if (bOrientation == EULER ||
            rollFunc->getSubfunc((float)(i + 1) / F_HZ)->degree == tozero) {
            curNode->fRollSpeed += pureRollChange;
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

        calcDirFromLast(i + 1);

        if (parent->enforceMinRadius && parent->minRadius > 0.0f) {
            double dotProd = glm::dot(curNode->vDir, prevNode->vDir);
            double deltaThetaRad = acos(glm::clamp(dotProd, -1.0, 1.0));
            double ds = curNode->fDistFromLast;
            if (deltaThetaRad > 1e-7) {
                double physicalRadius = ds / deltaThetaRad;
                if (physicalRadius < parent->minRadius) {
                    restricted = true;
                }
            }
        }

        float temp = cos(fabs(curNode->getPitch()) * F_PI / 180.f);
        float forceAngle =
            sqrt(temp * temp * curNode->fYawFromLast * curNode->fYawFromLast +
                 curNode->fPitchFromLast * curNode->fPitchFromLast); // deltaAngle;
        curNode->fAngleFromLast = forceAngle;

        glm::dvec3 forceVec;
        if (fabs(deltaAngle) < std::numeric_limits<float>::epsilon()) {
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
    }
    if (lNodes.size() > (size_t)(1 + i)) {
        lNodes.erase(lNodes.begin() + 1 + i, lNodes.end());
    }
    if (!lNodes.empty()) {
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    } else
        length = 0;
    this->isStalled = stalled;
    this->isRestricted = restricted;
    return node;
}

int secgeometric::updateDistanceSection(int node) {
    bool stalled = false;
    bool restricted = false;
    node = node < 0 ? 0 : node;

    int i = 0;
    this->length = 0.f;
    float hDist = 0.f;
    float artificialRoll = lNodes[(0)].fRoll;
    while (length < (float)node / F_HZ && i + 1 < (int)lNodes.size()) {
        hDist += lNodes[++i].fHeartDistFromLast;
        length += lNodes[i].fDistFromLast;

        if (bOrientation == 0) {
            artificialRoll -= glm::dot(lNodes[i].vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                              latForce->getValue(length + lNodes[i].fVel / F_HZ) *
                              lNodes[i].fVel / F_HZ;
        }

        artificialRoll += rollFunc->getValue(length + lNodes[i].fVel / F_HZ) *
                          (lNodes[i].fVel / F_HZ);
        while (artificialRoll > 180.f) {
            artificialRoll -= 360.f;
        }
        while (artificialRoll < -180.f) {
            artificialRoll += 360.f;
        }
    }

    if (i >= (int)lNodes.size() - 1 && i > 0) {
        i = (int)lNodes.size() - 2;
    }

    if (lNodes.size() > 1 &&
        this->parent->lSections.at(this->parent->lSections.size() - 1) != this) {
        lNodes.pop_back(); // disjoint this section from the next one
    }

    if (i == 0) {
        lNodes[(0)].updateNorm();

        float diff = lNodes[0].getPitchChange() /
                     lNodes[0].fVel; // - normForce->funcList.at(0)->startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        normForce->funcList.front()->translateValues(diff);
        normForce->translateValues(normForce->funcList.front());

        diff = lNodes[0].getYawChange() /
               lNodes[0].fVel; // - latForce->funcList.at(0)->startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        latForce->funcList.front()->translateValues(diff);
        latForce->translateValues(latForce->funcList.front());

        diff = lNodes[0].fRollSpeed /
               lNodes[0].fVel; // - rollFunc->funcList.at(0)->startValue;
        if (bOrientation == 1) {
            diff += glm::dot(lNodes[0].vDir, glm::dvec3(0.0, 1.0, 0.0)) *
                    lNodes[0].getYawChange() / lNodes[0].fVel;
        }
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

        double requestedPitchForce = normForce->getValue(length + curNode->fVel / F_HZ);
        double requestedYawForce = latForce->getValue(length + curNode->fVel / F_HZ);

        if (parent->enableForceLimits) {
            if (requestedPitchForce > parent->fMaxPosNormal || requestedPitchForce < parent->fMaxNegNormal || requestedYawForce > parent->fMaxLateral || requestedYawForce < parent->fMinLateral) {
                restricted = true;
            }
        }

        float pitchChange = requestedPitchForce * (curNode->fVel / F_HZ);
        float yawChange = requestedYawForce * (curNode->fVel / F_HZ);
        int sign = 1;
        if (fabs(artificialRoll) >= 90.f) {
            sign = -1;
        }

        curNode->changePitch(pitchChange, sign == -1);
        curNode->changeYaw(yawChange);

        float pureYawChange =
            (1.f - fabs(glm::dot(curNode->vDir, glm::dvec3(0.0, 1.0, 0.0)))) *
            yawChange;
        float pureRollChange =
            glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) * yawChange * F_HZ;
        float deltaAngle =
            sqrt(pitchChange * pitchChange + pureYawChange * pureYawChange);

        curNode->setRoll(-pureRollChange / F_HZ);
        artificialRoll -= pureRollChange / F_HZ;

        curNode->vPos += curNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         prevNode->vDir * (prevNode->fVel / (2.f * F_HZ)) +
                         (prevNode->vPosHeart(parent->fHeart) -
                          curNode->vPosHeart(parent->fHeart));

        curNode->updateNorm();

        curNode->setRoll(
            rollFunc->getValue(length + curNode->fVel / F_HZ) *
            (curNode->fVel /
             F_HZ)); // rollFunc->getValue((float)(i+1)/numNodes*fAngle));
                     // //360./numNodes*(i+1));

        if (bOrientation == EULER) {
            curNode->setRoll(pureRollChange / F_HZ);
            artificialRoll += pureRollChange / F_HZ;
        }

        artificialRoll += rollFunc->getValue(length + curNode->fVel / F_HZ) *
                          (curNode->fVel / F_HZ);
        while (artificialRoll > 180.f) {
            artificialRoll -= 360.f;
        }
        while (artificialRoll < -180.f) {
            artificialRoll += 360.f;
        }

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength =
            prevNode->fTotalHeartLength + curNode->fHeartDistFromLast;
        curNode->fRollSpeed =
            rollFunc->getValue(length + curNode->fVel / F_HZ) * curNode->fVel;

        if (bOrientation == 1) {
            curNode->fRollSpeed += pureRollChange;
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

        calcDirFromLast(i + 1);

        if (parent->enforceMinRadius && parent->minRadius > 0.0f) {
            double dotProd = glm::dot(curNode->vDir, prevNode->vDir);
            double deltaThetaRad = acos(glm::clamp(dotProd, -1.0, 1.0));
            double ds = curNode->fDistFromLast;
            if (deltaThetaRad > 1e-7) {
                double physicalRadius = ds / deltaThetaRad;
                if (physicalRadius < parent->minRadius) {
                    restricted = true;
                }
            }
        }

        float temp = cos(fabs(curNode->getPitch()) * F_PI / 180.f);
        float forceAngle =
            sqrt(temp * temp * curNode->fYawFromLast * curNode->fYawFromLast +
                 curNode->fPitchFromLast * curNode->fPitchFromLast); // deltaAngle;
        curNode->fAngleFromLast = forceAngle;

        glm::dvec3 forceVec;
        if (fabs(deltaAngle) < std::numeric_limits<float>::epsilon()) {
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

        this->length += curNode->fDistFromLast;
        ++i;
    }
    if (lNodes.size() > (size_t)(1 + i)) {
        lNodes.erase(lNodes.begin() + 1 + i, lNodes.end());
    }
    if (!lNodes.empty()) {
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    } else
        length = 0;
    this->isStalled = stalled;
    this->isRestricted = restricted;
    return node;
}

double secgeometric::getMaxArgument() {
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

void secgeometric::saveSection(std::ostream& file) {
    file << "GEO";
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

void secgeometric::loadSection(std::istream& file) {
    bSpeed = readBool(&file);

    int namelength = readInt(&file);
    sName = readString(&file, namelength);

    fVel = std::max(0.01, (double)readFloat(&file));
    iTime = readInt(&file);
    bOrientation = readBool(&file);
    bArgument = readBool(&file);

    // Safely check if there are bytes left to load fAccel
    if (file.peek() != EOF) {
        fAccel = (double)readFloat(&file);
    } else {
        fAccel = 0.0;
    }
    rollFunc->loadFunction(file);
    normForce->loadFunction(file);
    latForce->loadFunction(file);
}

bool secgeometric::isInFunction(int index, subfunc* func) {
    if (func == NULL)
        return false;
    if (bArgument == DISTANCE) {
        if (index >= (int)lNodes.size() || lNodes.size() <= 1)
            return false;
        double h_offset = lNodes[index].fTotalHeartLength - lNodes[0].fTotalHeartLength;
        double h_total = lNodes.back().fTotalHeartLength - lNodes[0].fTotalHeartLength;
        double r_total = this->length;
        double dist_projected = h_offset;
        if (h_total > 1e-6) {
            dist_projected = h_offset * (r_total / h_total);
        }
        if (dist_projected >= func->minArgument && dist_projected <= func->maxArgument) {
            return true;
        }
        return false;
    } else if ((float)index / F_HZ >= func->minArgument &&
               (float)index / F_HZ <= func->maxArgument) {
        return true;
    }
    return false;
}

bool secgeometric::isLockable(func* _func) {
    if (_func == rollFunc) {
        if (normForce->lockedFunc() != -1 && latForce->lockedFunc() != -1)
            return false;
    } else if (_func == normForce) {
        if (rollFunc->lockedFunc() != -1 && latForce->lockedFunc() != -1)
            return false;
    } else if (_func == latForce) {
        if (rollFunc->lockedFunc() != -1 && normForce->lockedFunc() != -1)
            return false;
    } else {
        lenAssert(0 && "no such func");
        return false;
    }
    return true;
}

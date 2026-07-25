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

#include "secforced.h"
#include "common.h"
#include "exportfuncs.h"
#include "mnode.h"
#include <algorithm>
#include "dummies.h"
#include <iostream>
#include <cmath>

secforced::~secforced() {
    delete normForce;
    delete latForce;
}

secforced::secforced(track* getParent, mnode* first, float gettime)
    : section(getParent, forced, first) {
    this->iTime = (int)(gettime + 0.5);
    this->length = 0.0;
    rollFunc->changeLength(1.f, 0);
    normForce = new func(0, 1, this->lNodes[0].forceNormal,
                         this->lNodes[0].forceNormal, this, funcNormal);
    latForce = new func(0, 1, this->lNodes[0].forceLateral,
                        this->lNodes[0].forceLateral, this, funcLateral);

    this->bOrientation = QUATERNION;
    this->bArgument = TIME;
    bSpeed = 1;
    fVel = 10;
}

int secforced::updateSection(int node) {
    bool restricted = false;
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

    if (node >= (int)lNodes.size() - 1 && node > 0)
        node = (int)lNodes.size() - 2;

    if (lNodes.size() > 1 &&
        this->parent->lSections.at(this->parent->lSections.size() - 1) != this) {
        lNodes.pop_back(); // disjoint this section from the next one
    }

    if (node == 0) {
        lNodes[0].updateNorm();

        float diff =
            lNodes[0].forceNormal; // - normForce->funcList.at(0)]-startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        normForce->funcList.front()->translateValues(diff);
        normForce->translateValues(normForce->funcList.front());

        diff = lNodes[0].forceLateral; // - latForce->funcList.at(0)]-startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        latForce->funcList.front()->translateValues(diff);
        latForce->translateValues(latForce->funcList.front());

        diff = lNodes[0].fRollSpeed; // - rollFunc->funcList.at(0)]-startValue;
        if (bOrientation == 1) {
            diff += glm::dot(lNodes[0].vDir, glm::dvec3(0.0, 1.0, 0.0)) *
                    lNodes[0].getYawChange();
        }
        rollFunc->funcList.front()->translateValues(diff);
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
        curNode->fVel = prevNode->fVel;
        curNode->fEnergy = prevNode->fEnergy;

        glm::dvec3 forceVec =
            -normForce->getValue((float)(i + 1) / F_HZ) * prevNode->vNorm -
            latForce->getValue((float)(i + 1) / F_HZ) * prevNode->vLat -
            glm::dvec3(0.0, 1.0, 0.0);

        float nForce = -glm::dot(forceVec, glm::normalize(prevNode->vNorm)) * F_G;
        float lForce = -glm::dot(forceVec, glm::normalize(prevNode->vLat)) * F_G;

        float estVel = fabs(prevNode->fHeartDistFromLast) <
                               std::numeric_limits<float>::epsilon()
                           ? prevNode->fVel
                           : prevNode->fHeartDistFromLast * F_HZ;

        if (parent->enableForceLimits) {
            double nfG = nForce / F_G;
            double lfG = lForce / F_G;
            if (nfG > parent->fMaxPosNormal || nfG < parent->fMaxNegNormal || lfG > parent->fMaxLateral || lfG < parent->fMinLateral) {
                restricted = true;
            }
        }

        curNode->forceNormal = nForce / F_G;
        curNode->forceLateral = lForce / F_G;

        curNode->vDir = glm::normalize(
            glm::angleAxis(nForce / F_HZ / estVel, prevNode->vLat) *
            glm::angleAxis(-lForce / prevNode->fVel / F_HZ, prevNode->vNorm) *
            prevNode->vDir);
        curNode->vLat = glm::normalize(
            glm::angleAxis(-lForce / prevNode->fVel / F_HZ, prevNode->vNorm) *
            prevNode->vLat);

        curNode->updateNorm();

        curNode->vPos += curNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         prevNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         (prevNode->vPosHeart(parent->fHeart) -
                          curNode->vPosHeart(parent->fHeart));

        curNode->fRollSpeed = 0.f;
        curNode->setRoll(rollFunc->getValue((float)(i + 1) / F_HZ) /
                         F_HZ); // - rollFunc->getValue(i/1000.f));
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

        if (bOrientation == EULER ||
            rollFunc->getSubfunc((float)(i + 1) / F_HZ)->degree == tozero) {
            curNode->setRoll(glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                             curNode->fYawFromLast);
            curNode->fRollSpeed +=
                glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                curNode->fYawFromLast * F_HZ;
        }

        curNode->updateNorm();

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength =
            prevNode->fTotalHeartLength + curNode->fHeartDistFromLast;
        curNode->fRollSpeed +=
            rollFunc->getValue((float)(i + 1) / F_HZ); // /1000.f/curNode->fDistFromLast;

        calcDirFromLast(i + 1);
        float temp = cos(fabs(curNode->getPitch()) * F_PI / 180.f);
        float forceAngle =
            sqrt(temp * temp * curNode->fYawFromLast * curNode->fYawFromLast +
                 curNode->fPitchFromLast * curNode->fPitchFromLast); // deltaAngle;
        curNode->fAngleFromLast = forceAngle;

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
    }
    if (lNodes.size() > (size_t)(1 + i)) {
        lNodes.erase(lNodes.begin() + 1 + i, lNodes.end());
    }
    if (!lNodes.empty()) {
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    } else {
        length = 0;
    }
    this->isRestricted = restricted;
    this->isStalled = stalled;
    return node;
}

int secforced::updateDistanceSection(int node) {
    bool restricted = false;
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
        this->parent->lSections.at(this->parent->lSections.size() - 1) != this) {
        lNodes.pop_back();
    }

    if (i == 0) {
        lNodes[0].updateNorm();

        float diff =
            lNodes[0].forceNormal; // - normForce->funcList.at(0)]-startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        normForce->funcList.front()->translateValues(diff);
        normForce->translateValues(normForce->funcList.front());

        diff = lNodes[0].forceLateral; // - latForce->funcList.at(0)]-startValue;
        lenAssert(diff == diff);
        if (diff != diff) {
            lNodes.push_back(lNodes[0]);
            return node;
        }
        latForce->funcList.front()->translateValues(diff);
        latForce->translateValues(latForce->funcList.front());

        diff = lNodes[0].fRollSpeed /
               lNodes[0].fVel; // - rollFunc->funcList.at(0)]-startValue;
        if (bOrientation == 1) {
            diff += glm::dot(lNodes[0].vDir, glm::dvec3(0.0, 1.0, 0.0)) *
                    lNodes[0].getYawChange() / lNodes[0].fVel;
        }
        rollFunc->funcList.front()->translateValues(diff);
        rollFunc->translateValues(rollFunc->funcList.front());
    }

    int retval = i;
    float end = this->getMaxArgument();

    while (length < end) {
        if (i >= (int)lNodes.size() - 1) {
            lNodes.push_back(lNodes[i]);
        }

        mnode* prevNode = &lNodes[i];
        mnode* curNode = &lNodes[i + 1];

        // protect against 0 velocity, and ensuing assertion failures
        if (std::isnan(prevNode->fVel)) {
            prevNode->fVel = std::numeric_limits<float>::epsilon();
            break;
        }

        curNode->vPos = prevNode->vPos;
        curNode->fVel = prevNode->fVel;
        curNode->fEnergy = prevNode->fEnergy;

        glm::dvec3 forceVec =
            -normForce->getValue(length + prevNode->fVel / F_HZ) * prevNode->vNorm -
            latForce->getValue(length + prevNode->fVel / F_HZ) * prevNode->vLat -
            glm::dvec3(0.0, 1.0, 0.0);

        float nForce = -glm::dot(forceVec, glm::normalize(prevNode->vNorm)) * F_G;
        float lForce = -glm::dot(forceVec, glm::normalize(prevNode->vLat)) * F_G;

        float estVel = fabs(prevNode->fHeartDistFromLast) <
                               std::numeric_limits<float>::epsilon()
                           ? prevNode->fVel
                           : prevNode->fHeartDistFromLast * F_HZ;

        if (parent->enableForceLimits) {
            double nfG = nForce / F_G;
            double lfG = lForce / F_G;
            if (nfG > parent->fMaxPosNormal || nfG < parent->fMaxNegNormal || lfG > parent->fMaxLateral || lfG < parent->fMinLateral) {
                restricted = true;
            }
        }

        curNode->forceNormal = nForce / F_G;
        curNode->forceLateral = lForce / F_G;

        curNode->vDir = glm::normalize(
            glm::angleAxis(nForce / F_HZ / estVel, prevNode->vLat) *
            glm::angleAxis(-lForce / prevNode->fVel / F_HZ, prevNode->vNorm) *
            prevNode->vDir);
        curNode->vLat = glm::normalize(
            glm::angleAxis(-lForce / prevNode->fVel / F_HZ, prevNode->vNorm) *
            prevNode->vLat);

        curNode->updateNorm();

        curNode->vPos += curNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         prevNode->vDir * (curNode->fVel / (2.f * F_HZ)) +
                         (prevNode->vPosHeart(parent->fHeart) -
                          curNode->vPosHeart(parent->fHeart));

        curNode->setRoll(
            rollFunc->getValue(length + curNode->fVel / F_HZ) *
            (curNode->fVel / F_HZ)); // - rollFunc->getValue(i/1000.f));

        curNode->fRollSpeed = 0.f;
        curNode->setRoll(rollFunc->getValue(length + prevNode->fVel / F_HZ) /
                         F_HZ); // - rollFunc->getValue(i/1000.f));
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

        if (bOrientation == EULER) {
            curNode->setRoll(glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                             curNode->fYawFromLast);
            curNode->fRollSpeed +=
                glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                curNode->fYawFromLast * F_HZ;
        }

        curNode->updateNorm();

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength =
            prevNode->fTotalHeartLength + curNode->fHeartDistFromLast;
        curNode->fRollSpeed += rollFunc->getValue(length + curNode->fVel / F_HZ) *
                               curNode->fVel; // /1000.f/curNode->fDistFromLast;

        calcDirFromLast(i + 1);
        float temp = cos(fabs(curNode->getPitch()) * F_PI / 180.f);
        float forceAngle =
            sqrt(temp * temp * curNode->fYawFromLast * curNode->fYawFromLast +
                 curNode->fPitchFromLast * curNode->fPitchFromLast); // deltaAngle;
        curNode->fAngleFromLast = forceAngle;

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

        this->length += curNode->fDistFromLast;
        ++i;
    }
    if (lNodes.size() > (size_t)(1 + i)) {
        lNodes.erase(lNodes.begin() + 1 + i, lNodes.end());
    }
    if (!lNodes.empty()) {
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    } else {
        length = 0;
    }
    this->isRestricted = restricted;
    this->isStalled = stalled;
    return retval;
}

double secforced::getMaxArgument() {
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

void secforced::saveSection(std::ostream& file) {
    file << "FRC";
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

void secforced::loadSection(std::istream& file) {
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

bool secforced::isInFunction(int index, subfunc* func) {
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

bool secforced::isLockable(func* _func) {
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

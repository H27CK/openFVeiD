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

#include "secbezier.h"
#include "exportfuncs.h"
#include "dummies.h"
#include <iostream>
#include <cmath>

secbezier::secbezier(track* getParent, mnode* first)
    : section(getParent, bezier, first) {
    this->bOrientation = QUATERNION;
    this->bArgument = TIME;
    bSpeed = 0;
    fVel = 10;
    fSmoothing = 0.0f;
}

secbezier::~secbezier() {
    for (int i = 0; i < bezList.size(); ++i) {
        delete bezList[i];
    }
}

int secbezier::updateSection(int node) {
    bool stalled = false;
    (void)node;
    std::vector<float> tList;
    if (lNodes.size() > 1) {
        lNodes.erase(lNodes.begin() + 1, lNodes.end());
    }
    lNodes[0].updateNorm();

    int cur = 0, lastcur = 0;
    double t = 0.0;

    mnode *curNode = &lNodes[0], *prevNode = NULL;

    // Pre-calculate relaxed points dynamically without modifying bezList permanently
    std::vector<glm::dvec3> relP1(bezList.size());
    std::vector<glm::dvec3> relKp1(bezList.size());
    std::vector<glm::dvec3> relKp2(bezList.size());

    for (size_t b = 0; b < bezList.size(); ++b) {
        if (b == 0 || b == bezList.size() - 1 || fSmoothing <= 0.001f) {
            relP1[b] = bezList[b]->P1;
            relKp1[b] = bezList[b]->Kp1;
            relKp2[b] = bezList[b]->Kp2;
        } else {
            glm::dvec3 relaxedP = bezList[b]->P1 * (1.0 - (double)fSmoothing) +
                                  (bezList[b - 1]->P1 + bezList[b + 1]->P1) * 0.5 * (double)fSmoothing;
            glm::dvec3 diff = relaxedP - bezList[b]->P1;
            relP1[b] = relaxedP;
            relKp1[b] = bezList[b]->Kp1 + diff;
            relKp2[b] = bezList[b]->Kp2 + diff;
        }
    }

    for (int b = 0; b < (int)bezList.size() - 1; ++b) {
        while (t < 1.f) {
            tList.push_back(t);

            int bnext = (b + 1) % bezList.size();
            double t1 = 1.0 - t;
            if (cur >= lNodes.size()) {
                lNodes.push_back(lNodes.back());
            }
            prevNode = &lNodes[glm::max(cur - 1, 0)];
            curNode = &lNodes[cur];
            curNode->fEnergy = prevNode->fEnergy;
            curNode->vPos = t1 * t1 * t1 * relP1[b] +
                            3.0 * t1 * t1 * t * relKp2[b] +
                            3.0 * t1 * t * t * relKp1[bnext] +
                            t * t * t * relP1[bnext];

            curNode->fRoll = t1 * bezList[b]->fvdRoll + t * bezList[bnext]->fvdRoll;
            curNode->fRoll *= 180.0 / F_PI;

            glm::dvec3 diff1 = relKp2[b] - relP1[b];
            glm::dvec3 diff2 = relKp1[bnext] - relKp2[b];
            glm::dvec3 diff3 = relP1[bnext] - relKp1[bnext];

            curNode->vDir = t1 * t1 * diff1 + 2.0 * t1 * t * diff2 + t * t * diff3;

            double lengthDir = glm::length(curNode->vDir);
            if (lengthDir < 1e-6)
                lengthDir = 1e-6;

            curNode->vDir = glm::normalize(curNode->vDir);

            curNode->vLat.x = -curNode->vDir.z;
            curNode->vLat.y = 0.0;
            curNode->vLat.z = curNode->vDir.x;

            if (glm::length(curNode->vLat) < std::numeric_limits<double>::epsilon()) {
                curNode->vLat =
                    glm::normalize(glm::cross(curNode->vNorm, curNode->vDir));
            }

            curNode->setRoll(0.0); // curNode->fRoll);

            if (cur) {
                curNode->fHeartDistFromLast =
                    glm::distance(curNode->vPos, lNodes[cur - 1].vPos);
                curNode->fTotalHeartLength += curNode->fHeartDistFromLast;
                // curNode->fVel = curNode->fHeartDistFromLast*F_HZ;
                curNode->fDistFromLast =
                    glm::distance(curNode->vPosHeart(parent->fHeart),
                                  prevNode->vPosHeart(parent->fHeart));
                curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
            }

            double vel = this->fVel;
            if (this->bSpeed) {
                curNode->fEnergy -= (curNode->fVel * curNode->fVel * curNode->fVel /
                                     F_HZ * parent->fResistance);
                double energyValue = curNode->fEnergy -
                                     F_G * (curNode->vPosHeart(parent->fHeart * 0.9f).y +
                                            curNode->fTotalLength * parent->fFriction);
                double minSpeed = (double)gloParent->mOptions->stallSpeed;
                if (energyValue <= 0.0) {
                    vel = minSpeed;
                    stalled = true;
                } else {
                    vel = sqrt(2.f * energyValue);
                    if (vel < minSpeed) {
                        vel = minSpeed;
                        stalled = true;
                    }
                }
            } else {
                if (this->fAccel == 0.0) {
                    if (bezList[b]->fVel > 0.1 && bezList[b]->fVel != 20.0) {
                        vel = bezList[b]->fVel;
                    } else {
                        vel = this->fVel;
                    }
                } else {
                    double minSpeed = (double)gloParent->mOptions->stallSpeed;
                    vel = std::max(minSpeed, prevNode->fVel + this->fAccel / F_HZ);
                }
                curNode->fEnergy = 0.5 * vel * vel +
                                   F_G * (curNode->vPosHeart(parent->fHeart * 0.9f).y +
                                          curNode->fTotalLength * parent->fFriction);
            }
            curNode->fVel = vel;
            // Use 3000 Hz equivalent sampling for Bezier sections (vs standard 1000 Hz F_HZ).
            // This higher density improves geometric fidelity of parametric splines and
            // ensures physics/energy stability through tight curves or high speeds.
            double step = 1.0 / (3000.0 * lengthDir / vel);
            if (std::isnan(step) || step < 1e-4) {
                step = 1e-4; // Absolute minimum spatial-equivalent step to prevent OOM
            }
            t += step;
            ++cur;
        }
        t -= 1.f;
        bezList[b]->length =
            lNodes[cur - 1].fTotalHeartLength - lNodes[lastcur].fTotalHeartLength;
        bezList[b]->numNodes = cur - 1 - lastcur;
        lastcur = cur - 1;
    }

    if (!tList.size())
        return 0;

    int b = 0;
    float correction = 0;

    bezList[0]->fvdRoll = bezList[0]->roll;

    for (int i = 0; i < lNodes.size(); ++i) {
        calcDirFromLast(i); // now get roll change you need to get from a to b
        if (i && tList[i] < tList[i - 1]) {
            ++b;
            if (bezList[b]->relRoll) {
                bezList[b]->fvdRoll = bezList[b - 1]->fvdRoll +
                                      correction * F_PI / 180.f + bezList[b]->roll;
                bezList[b - 1]->ptf = bezList[b]->roll;
            } else {
                bezList[b]->fvdRoll = bezList[b]->roll;
                bezList[b - 1]->ptf = bezList[b]->fvdRoll - bezList[b - 1]->fvdRoll -
                                      correction * F_PI / 180.f;
                if (fabs(bezList[b - 1]->ptf) > F_PI) {
                    bezList[b - 1]->ptf +=
                        bezList[b - 1]->ptf > 0.f ? -2.f * F_PI : 2.f * F_PI;
                }
            }
            correction = 0.f;
        }
        correction -= glm::dot(lNodes[i].vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                      lNodes[i].fYawFromLast;

        float fRoll = bezList[b]->fvdRoll * 180.f / F_PI;

        lNodes[i].setRoll(fRoll + correction);
    }

    b = 0;
    int bNext = 1;
    float startVal = 0.f, endVal = 0.f, area = 0.f, a1 = 0.f, b1 = 0.f, c1 = 0.f;
    float value = 0.f;
    for (int i = 0; i < lNodes.size(); ++i) {
        float tNext;
        if (i == lNodes.size() - 1 || tList[i + 1] < tList[i]) {
            tNext = 1.f;
        } else {
            tNext = tList[i + 1];
        }

        if (i && tList[i] < tList[i - 1]) {
            ++b;
            bNext = (b + 1) % bezList.size();
            value = 0.f;

            startVal = endVal;

            if (bezList[bNext]->contRoll) {
                endVal = (bezList[b]->length * bezList[b]->ptf +
                          bezList[bNext]->length * bezList[bNext]->ptf) /
                         (bezList[b]->length +
                          bezList[bNext]->length); //*(tNext - tList[i]);
            } else {
                endVal = 0.f;
            }
            area = bezList[b]->ptf;

            a1 = 3.f * startVal + 3.f * endVal - 6.f * area;
            b1 = 6.f * area - 4.f * startVal - 2.f * endVal;
            c1 = startVal;
        } else if (!i) {
            startVal = 0.f;
            value = 0.f;
            if (bezList.size() > 1 && bezList[1]->contRoll) {
                endVal = (bezList[b]->length * bezList[b]->ptf +
                          bezList[bNext]->length * bezList[bNext]->ptf) /
                         (bezList[b]->length +
                          bezList[bNext]->length); //*(tNext - tList[i]);
            } else {
                endVal = 0.f;
            }
            area = bezList[0]->ptf;

            a1 = 3.f * startVal + 3.f * endVal - 6.f * area;
            b1 = 6.f * area - 4.f * startVal - 2.f * endVal;
            c1 = startVal;
        }
        value += (c1 + tList[i] * (b1 + a1 * tList[i])) * 180.f / F_PI *
                 (tNext - tList[i]);

        lNodes[i].setRoll(value);

        // lNodes[i].vPos = lNodes[i].vPosHeart(-parent->fHeart);

        if (i) {
            lNodes[i].fDistFromLast =
                glm::distance(lNodes[i].vPosHeart(parent->fHeart),
                              lNodes[i - 1].vPosHeart(parent->fHeart));
            lNodes[i].fTotalLength =
                lNodes[i - 1].fTotalLength + lNodes[i].fDistFromLast;
            lNodes[i].fHeartDistFromLast =
                glm::distance(lNodes[i].vPos, lNodes[i - 1].vPos);
            lNodes[i].fTotalHeartLength += lNodes[i].fHeartDistFromLast;

            double deltaRoll = lNodes[i].fRoll - lNodes[i - 1].fRoll;
            if (deltaRoll > 180.0)
                deltaRoll -= 360.0;
            else if (deltaRoll < -180.0)
                deltaRoll += 360.0;

            if (lNodes[i].fHeartDistFromLast > 1e-9) {
                lNodes[i].fRollSpeed =
                    (deltaRoll / lNodes[i].fHeartDistFromLast) * lNodes[i].fVel;
            } else {
                lNodes[i].fRollSpeed = 0.0;
            }
        }

        /*lNodes[i].fRollSpeed *= -1;
        lNodes[i].vDir = lNodes[i].vDirHeart(parent->fHeart);
        lNodes[i].vLat = lNodes[i].vLatHeart(parent->fHeart);
        lNodes[i].fRollSpeed *= -1;*/

        calcDirFromLast(i);
        float temp = cos(fabs(lNodes[i].getPitch()) * F_PI / 180.f);
        float forceAngle = sqrt(
            temp * temp * lNodes[i].fYawFromLast * lNodes[i].fYawFromLast +
            lNodes[i].fPitchFromLast * lNodes[i].fPitchFromLast); // deltaAngle;
        lNodes[i].fAngleFromLast = forceAngle;

        glm::dvec3 forceVec;
        if (fabs(forceAngle) < std::numeric_limits<float>::epsilon()) {
            forceVec = glm::dvec3(0.0, 1.0, 0.0);
        } else {
            // forceVec = glm::dvec3(0.0, 1.0, 0.0) +
            // (float)((lNodes[i].fHeartDistFromLast*1000.f*lNodes[i].fAngleFromLast*F_PI/180.f)
            // / (9.80665*0.001f)) *
            // glm::normalize(glm::vec3(glm::rotate(lNodes[i].fDirFromLast,
            // -lNodes[i].vDir)*glm::vec4(-lNodes[i].vNorm, 0.f)));
            float normalDAngle =
                F_PI / 180.f *
                (-lNodes[i].fPitchFromLast * cos(lNodes[i].fRoll * F_PI / 180.) -
                 temp * lNodes[i].fYawFromLast * sin(lNodes[i].fRoll * F_PI / 180.));
            float lateralDAngle =
                F_PI / 180.f *
                (lNodes[i].fPitchFromLast * sin(lNodes[i].fRoll * F_PI / 180.) -
                 temp * lNodes[i].fYawFromLast * cos(lNodes[i].fRoll * F_PI / 180.));

            forceVec = glm::dvec3(0.0, 1.0, 0.0) +
                       lateralDAngle * lNodes[i].fVel * F_HZ / F_G * lNodes[i].vLat +
                       normalDAngle * lNodes[i].fHeartDistFromLast * F_HZ * F_HZ /
                           F_G * lNodes[i].vNorm;
        }
        lNodes[i].forceNormal =
            -glm::dot(forceVec, glm::normalize(lNodes[i].vNorm));
        lNodes[i].forceLateral =
            -glm::dot(forceVec, glm::normalize(lNodes[i].vLat));
    }
    if (!lNodes.empty())
        length = lNodes.back().fTotalLength - lNodes.front().fTotalLength;
    else
        length = 0;
    this->isStalled = stalled;
    return 0;
}

void secbezier::saveSection(std::ostream& file) {
    file << "BEZ";
    int namelength = (int)sName.length();
    std::string name = sName;

    writeBytes(&file, (const char*)&namelength, sizeof(int));
    file << name;

    int bezcount = bezList.size();
    writeBytes(&file, (const char*)&bezcount, sizeof(int));
    for (int i = 0; i < bezcount; ++i) {
        glm::vec3 tempP1 = bezList[i]->P1;
        glm::vec3 tempKp1 = bezList[i]->Kp1;
        glm::vec3 tempKp2 = bezList[i]->Kp2;
        float tempRoll = (float)bezList[i]->roll;

        writeVec3(&file, tempP1);
        writeVec3(&file, tempKp1);
        writeVec3(&file, tempKp2);
        writeBytes(&file, (const char*)&bezList[i]->contRoll, sizeof(bool));
        writeBytes(&file, (const char*)&bezList[i]->relRoll, sizeof(bool));
        writeBytes(&file, (const char*)&tempRoll, sizeof(float));
    }

    int supcount = supList.size();
    writeBytes(&file, (const char*)&supcount, sizeof(int));
    for (int i = 0; i < supcount; ++i) {
        glm::vec3 tempSup = supList[i];
        writeVec3(&file, tempSup);
    }

    writeBytes(&file, (const char*)&fSmoothing, sizeof(float));
}

void secbezier::loadSection(std::istream& file) {
    int namelength = readInt(&file);
    sName = readString(&file, namelength);

    int bezcount = readInt(&file);
    for (int i = 0; i < bezcount; ++i) {
        bezList.push_back(new bezier_t);
        bezList[i]->P1 = readVec3(&file);
        bezList[i]->Kp1 = readVec3(&file);
        bezList[i]->Kp2 = readVec3(&file);
        bezList[i]->contRoll = readBool(&file);
        bezList[i]->relRoll = readBool(&file);
        bezList[i]->roll = readFloat(&file);
        bezList[i]->fVel = 0;
    }

    int supcount = readInt(&file);
    for (int i = 0; i < supcount; ++i) {
        supList.push_back(readVec3(&file));
    }

    fSmoothing = readFloat(&file);
}

double secbezier::getMaxArgument() {
    return 0.0;
}

bool secbezier::isLockable(func* _func) {
    (void)_func;
    return false;
}

bool secbezier::isInFunction(int index, subfunc* func) {
    (void)index;
    (void)func;
    return false;
}

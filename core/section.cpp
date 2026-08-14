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

#include "section.h"
#include "dummies.h"
#include "exportfuncs.h"
#include <cmath>
#include <algorithm>

#include "track.h"
#include <iostream>

#define RELTHRESH 1.0f

using namespace std;

section::section(track* getParent, enum secType _type, mnode* first) {
    sName = "Unnamed";
    fAccel = 0.0;
    lNodes.reserve(1000);
    lNodes.push_back(*first);
    parent = getParent;
    normForce = NULL;
    latForce = NULL;
    rollFunc = new func(0, 10, 0, 0, this, funcRoll);
    type = _type;
}

mnode section::sampleAtDistance(double dist) {
    if (lNodes.empty())
        return mnode();
    if (dist <= lNodes.front().fTotalLength)
        return lNodes.front();
    if (dist >= lNodes.back().fTotalLength)
        return lNodes.back();

    auto it = std::lower_bound(lNodes.begin(), lNodes.end(), dist,
                               [](const mnode& n, double d) {
                                   return n.fTotalLength < d;
                               });

    if (it == lNodes.begin())
        return *it;
    if (it == lNodes.end())
        return lNodes.back();

    const mnode& n1 = *it;
    const mnode& n0 = *(it - 1);

    double s0 = n0.fTotalLength;
    double s1 = n1.fTotalLength;
    double h = s1 - s0;
    if (h <= 1e-9)
        return n0;
    double t = (dist - s0) / h;

    mnode res;
    res.fTotalLength = dist;

    // Cubic Hermite Interpolation for Position
    double t2 = t * t;
    double t3 = t2 * t;
    res.vPos = (2.0 * t3 - 3.0 * t2 + 1.0) * n0.vPos +
               h * (t3 - 2.0 * t2 + t) * n0.vDir +
               (-2.0 * t3 + 3.0 * t2) * n1.vPos + h * (t3 - t2) * n1.vDir;

    // Lerp + Orthonormalize for Vectors
    res.vDir = glm::normalize(glm::mix(n0.vDir, n1.vDir, t));
    res.vLat = glm::normalize(glm::mix(n0.vLat, n1.vLat, t));
    res.vLat = glm::normalize(res.vLat - glm::dot(res.vLat, res.vDir) * res.vDir);
    res.updateNorm();

    // Lerp for scalars
    res.fRoll = glm::mix(n0.fRoll, n1.fRoll, t);
    res.fVel = glm::mix(n0.fVel, n1.fVel, t);
    res.fEnergy = glm::mix(n0.fEnergy, n1.fEnergy, t);
    res.forceNormal = glm::mix(n0.forceNormal, n1.forceNormal, t);
    res.forceLateral = glm::mix(n0.forceLateral, n1.forceLateral, t);
    res.smoothNormal = glm::mix(n0.smoothNormal, n1.smoothNormal, t);
    res.smoothLateral = glm::mix(n0.smoothLateral, n1.smoothLateral, t);

    // deltas
    res.fDistFromLast = h * t;
    res.fHeartDistFromLast = glm::mix(n0.fHeartDistFromLast, n1.fHeartDistFromLast, t);
    res.fTotalHeartLength = glm::mix(n0.fTotalHeartLength, n1.fTotalHeartLength, t);
    res.fAngleFromLast = glm::mix(n0.fAngleFromLast, n1.fAngleFromLast, t);
    res.fTrackAngleFromLast =
        glm::mix(n0.fTrackAngleFromLast, n1.fTrackAngleFromLast, t);
    res.fDirFromLast = glm::mix(n0.fDirFromLast, n1.fDirFromLast, t);
    res.fPitchFromLast = glm::mix(n0.fPitchFromLast, n1.fPitchFromLast, t);
    res.fYawFromLast = glm::mix(n0.fYawFromLast, n1.fYawFromLast, t);
    res.fRollSpeed = glm::mix(n0.fRollSpeed, n1.fRollSpeed, t);
    res.fSmoothSpeed = glm::mix(n0.fSmoothSpeed, n1.fSmoothSpeed, t);

    return res;
}

section::~section() {
    if (lNodes.size() > 2) {
        lNodes.erase(lNodes.begin() + 1, lNodes.end() - 1);
    }
    if (rollFunc) {
        delete rollFunc;
    }
}

int section::exportSection(std::ostream* file, mnode* anchor, double mPerNode,
                           double fHeart, glm::dvec3& vHeartLat, glm::dvec3& Norm,
                           double fThreshold) {
    (void)vHeartLat;
    (void)Norm;

    int count = 1;
    int lasti = 0;

    lNodes[0].updateNorm();

    double fCounter = 0.0;

    int numNodes = (int)(this->length / mPerNode);

    for (int i = 1; i < (int)lNodes.size(); i++) {
        lNodes[i].updateNorm();
        fCounter += glm::distance(lNodes[i].vPosHeart(fHeart),
                                  lNodes[i - 1].vPosHeart(fHeart));

        if (i == (int)this->lNodes.size() - 1 || fCounter > this->length / numNodes) {

#define SCALING 3.0

            glm::dvec3 V(anchor->vDir.x, 0, anchor->vDir.z);
            glm::dvec4 KP1, P1, KP2;
            double temp = glm::length(V);
            glm::dmat4 anchorBase;
            if (anchor->vDir.z != 0 || anchor->vDir.x != 0) {
                anchorBase =
                    glm::dmat4(glm::transpose(glm::dmat4(
                                   -anchor->vDir.z / temp, 0.0, anchor->vDir.x / temp,
                                   0.0, 0.0, 1.0, 0.0, 0.0, -anchor->vDir.x / temp, 0.0,
                                   -anchor->vDir.z / temp, 0.0, 0.0, 0.0, 0.0, 1.0)) *
                               glm::dmat4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
                                          0.0, 1.0, 0.0, -anchor->fPosHeartx(fHeart),
                                          -anchor->fPosHearty(fHeart),
                                          -anchor->fPosHeartz(fHeart), 1.0));
            } else {
                anchorBase =
                    glm::dmat4(glm::transpose(glm::dmat4(
                                   -anchor->vNorm.z / temp, 0.0, anchor->vNorm.x / temp,
                                   0.0, 0.0, 1.0, 0.0, 0.0, -anchor->vNorm.x / temp, 0.0,
                                   -anchor->vNorm.z / temp, 0.0, 0.0, 0.0, 0.0, 1.0)) *
                               glm::dmat4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0,
                                          0.0, 1.0, 0.0, -anchor->fPosHeartx(fHeart),
                                          -anchor->fPosHearty(fHeart),
                                          -anchor->fPosHeartz(fHeart), 1.0));
            }

            // float fRollSpeedPerMeter = lNodes[(lasti)->fDistFromLast > 0.f ?
            // lNodes[(lasti)].fRollSpeed/F_HZ/lNodes.at(lasti)]-fDistFromLast : 0.f;

            KP1 = anchorBase *
                  (glm::dvec4(glm::dvec3(lNodes[lasti].vPosHeart(fHeart) +
                                         fThreshold / SCALING *
                                             lNodes[lasti].vDirHeart(fHeart)),
                              1.0));

            float fval;
            fval = (float)KP1.x;
            writeBytes(file, (const char*)&fval, 4);
            fval = (float)KP1.y;
            writeBytes(file, (const char*)&fval, 4);
            fval = (float)KP1.z;
            writeBytes(file, (const char*)&fval, 4);

            // fRollSpeedPerMeter = lNodes[(i)->fDistFromLast > 0.f ?
            // lNodes[(i)].fRollSpeed/F_HZ/lNodes.at(i)]-fDistFromLast : 0.f;
            KP2 = anchorBase * (glm::dvec4(glm::dvec3(lNodes[i].vPosHeart(fHeart) -
                                                      fThreshold / SCALING *
                                                          lNodes[i].vDirHeart(fHeart)),
                                           1.0));

            fval = (float)KP2.x;
            writeBytes(file, (const char*)&fval, 4);
            fval = (float)KP2.y;
            writeBytes(file, (const char*)&fval, 4);
            fval = (float)KP2.z;
            writeBytes(file, (const char*)&fval, 4);

            P1 =
                anchorBase * (glm::dvec4(glm::dvec3(lNodes[i].vPosHeart(fHeart)), 1.0));

            fval = (float)P1.x;
            writeBytes(file, (const char*)&fval, 4);
            fval = (float)P1.y;
            writeBytes(file, (const char*)&fval, 4);
            fval = (float)P1.z;
            writeBytes(file, (const char*)&fval, 4);

            if (fabs(V.y) < fThreshold) {
                temp = glm::atan(lNodes[i].vLatHeart(fHeart).y, -lNodes[i].vNorm.y);
            } else {
                glm::dvec3 rotateAxis = glm::cross(lNodes[lasti].vDirHeart(fHeart),
                                                   lNodes[i].vDirHeart(fHeart));
                glm::dvec3 rotated =
                    glm::dvec3(glm::rotate(glm::angle(lNodes[lasti].vDirHeart(fHeart),
                                                      lNodes[i].vDirHeart(fHeart)),
                                           rotateAxis) *
                               glm::dvec4(lNodes[lasti].vLatHeart(fHeart), 0.0));
                temp = glm::angle(rotated, lNodes[i].vLatHeart(fHeart)) * F_PI / 180.0;
                if (temp != temp) {
                    temp = 0.0;
                }
            }

            float fTemp = (float)temp;
            writeBytes(file, (const char*)&fTemp, 4);

            char cTemp;

            cTemp = (char)0xFF;
            writeBytes(file, &cTemp, 1); // CONT ROLL
            cTemp = fabs(V.y) < fThreshold ? (char)0x00 : (char)0xff;
            writeBytes(file, &cTemp, 1); // REL ROLL
            cTemp = (char)0x00;
            writeBytes(file, &cTemp, 1); // equalDistanceCP
            writeNulls(file, 7);

            count++;
            fThreshold -= this->length / numNodes;
            lasti = i;
        }
    }
    return count - 1;
}

void section::fillPointList(std::vector<glm::dvec4>& List, std::vector<glm::dvec3>& Normals,
                            mnode* anchor, double mPerNode, double fThreshold) {
    lNodes[0].updateNorm();

    double fCounter = 0.0;

    int numNodes = (int)(this->length / mPerNode);

    for (int i = 1; i < (int)lNodes.size(); i++) {
        lNodes[i].updateNorm();
        fCounter += glm::distance(lNodes[i].vPosHeart(parent->fHeart),
                                  lNodes[i - 1].vPosHeart(parent->fHeart));
        glm::dvec3 V(anchor->vDir.x, 0, anchor->vDir.z), Norm;
        glm::dvec4 P1;
        double temp = glm::length(V);
        glm::dmat4 anchorBase;
        if (anchor->vDir.z != 0 || anchor->vDir.x != 0) {
            anchorBase = glm::dmat4(
                glm::transpose(
                    glm::dmat4(-anchor->vDir.z / temp, 0.0, anchor->vDir.x / temp, 0.0,
                               0.0, 1.0, 0.0, 0.0, -anchor->vDir.x / temp, 0.0,
                               -anchor->vDir.z / temp, 0.0, 0.0, 0.0, 0.0, 1.0)) *
                glm::dmat4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                           -anchor->fPosHeartx(parent->fHeart), -anchor->fPosHearty(parent->fHeart),
                           -anchor->fPosHeartz(parent->fHeart), 1.0));
        } else {
            anchorBase = glm::dmat4(
                glm::transpose(
                    glm::dmat4(-anchor->vNorm.z / temp, 0.0, anchor->vNorm.x / temp,
                               0.0, 0.0, 1.0, 0.0, 0.0, -anchor->vNorm.x / temp, 0.0,
                               -anchor->vNorm.z / temp, 0.0, 0.0, 0.0, 0.0, 1.0)) *
                glm::dmat4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                           -anchor->fPosHeartx(parent->fHeart), -anchor->fPosHearty(parent->fHeart),
                           -anchor->fPosHeartz(parent->fHeart), 1.0));
        }

        if (i == (int)this->lNodes.size() - 1 || fCounter > this->length / numNodes) {
            P1 =
                anchorBase * (glm::dvec4(glm::dvec3(lNodes[i].vPosHeart(parent->fHeart)), 1.0));
            Norm = glm::dvec3(anchorBase * (glm::dvec4(lNodes[i].vNorm, 0.0)));
            List.push_back(glm::dvec4(glm::dvec3(P1), lNodes[i].fRoll * F_PI / 180.0));
            Normals.push_back(Norm);
            fCounter -= this->length / numNodes;
        }
    }
}

void section::iFillPointList(std::vector<int>& List, double mPerNode) {
    lNodes[0].updateNorm();

    double fCounter = 0.0;

    int numNodes = (int)(this->length / mPerNode);
    int nodeCount = 0;
    if (this->type == straight) {
        List.push_back(parent->getNumPoints(this) + lNodes.size() - 2);
        return;
    }

    for (int i = 1; i < (int)lNodes.size(); i++) {
        lNodes[i].updateNorm();
        fCounter +=
            lNodes[i].fDistFromLast; // glm::distance(lNodes[(i)->fPosHeart(fHeart),
                                     // lNodes.at(i-1)]-fPosHeart(fHeart));
        if (i == (int)this->lNodes.size() - 1 || fCounter > this->length / numNodes) {
            List.push_back(parent->getNumPoints(this) + i - 1);
            fCounter -= this->length / numNodes;
            ++nodeCount;
        }
    }
    if (List.size() > 1 &&
        (lNodes[List.back() - parent->getNumPoints(this) + 1].fTotalLength -
             lNodes[List[List.size() - 2] - parent->getNumPoints(this) + 1]
                 .fTotalLength <
         mPerNode / 2.0)) {
        List.erase(List.begin() + List.size() - 2);
    }
}

void section::fillNodeList(std::vector<mnode>& List, double mPerNode) {
    if (lNodes.size() < 2)
        return;

    if (this->type == straight) {
        List.push_back(lNodes.back());
        return;
    }

    double startLen = lNodes.front().fTotalLength;
    int numPoints = (int)(this->length / mPerNode);
    if (numPoints < 1)
        numPoints = 1;
    double step = this->length / (double)numPoints;

    for (int i = 1; i <= numPoints; ++i) {
        List.push_back(sampleAtDistance(startLen + i * step));
    }
}

void section::fFillPointList(std::vector<int>& List, double mPerNode) {
    lNodes[0].updateNorm();

    double fCounter = 0.0;

    int numNodes = (int)(this->length / mPerNode);
    if (numNodes < 2) {
        numNodes = 2;
    }
    int nodeCount = 0;
    if (this->type == straight && this->rollFunc->funcList.size() == 1 &&
        this->rollFunc->funcList[0]->symArg == 0.0) {
        if (List.size()) {
            List.back() *= -1;
        }
        List.push_back((parent->getNumPoints(this) + (int)lNodes.size() - 2) * -1);
        return;
    }

    for (int i = 1; i < (int)lNodes.size(); i++) {
        lNodes[i].updateNorm();
        fCounter += lNodes[i].fDistFromLast;
        if (i == (int)this->lNodes.size() - 1 || fCounter > this->length / numNodes) {
            List.push_back(parent->getNumPoints(this) + i - 1);
            fCounter -= this->length / numNodes;
            ++nodeCount;
        }
    }
    if (List.size() > 1 &&
        (lNodes[List.back() - parent->getNumPoints(this) + 1].fTotalLength -
             lNodes[List[List.size() - 2] - parent->getNumPoints(this) + 1]
                 .fTotalLength <
         mPerNode / 2.0)) {
        List.erase(List.begin() + List.size() - 2);
    }
}

void section::fFillNodeList(std::vector<ExportNode>& List, double mPerNode) {
    if (lNodes.size() < 2)
        return;

    int numPoints = (int)(this->length / mPerNode);
    if (numPoints < 2)
        numPoints = 2;

    if (this->type == straight && this->rollFunc->funcList.size() == 1 &&
        this->rollFunc->funcList[0]->symArg == 0.0) {
        if (!List.empty()) {
            List.back().isBoundary = true;
        }
        ExportNode en;
        en.node = lNodes.back();
        en.isBoundary = true;
        List.push_back(en);
        return;
    }

    double startLen = lNodes.front().fTotalLength;
    double step = this->length / (double)numPoints;

    for (int i = 1; i <= numPoints; ++i) {
        ExportNode en;
        en.node = sampleAtDistance(startLen + i * step);
        en.isBoundary = false;
        List.push_back(en);
    }
}

void section::calcDirFromLast(int i) {
    lenAssert(i >= 0 || i < lNodes.size());
    if (i == 0 || i >= lNodes.size()) {
        return;
    }
    glm::dvec3 diff = lNodes[i].vDir - lNodes[i - 1].vDir;
    if (glm::length(diff) <= std::numeric_limits<double>::epsilon()) {
        lNodes[i].fDirFromLast = 0.0;
        lNodes[i].fPitchFromLast = 0.0;
        lNodes[i].fYawFromLast = 0.0;
    } else {
        double y = -glm::dot(diff, lNodes[i - 1].vNorm);
        double x = -glm::dot(diff, lNodes[i - 1].vLat);
        double angle = glm::atan(x, y) * 180.0 / F_PI;
        lNodes[i].fDirFromLast = angle;
        double p_prev = lNodes[i - 1].getPitch() * F_PI / 180.0;
        double y_prev = lNodes[i - 1].getDirection() * F_PI / 180.0;

        glm::dvec3 e_p_prev(std::sin(p_prev) * std::sin(y_prev), std::cos(p_prev), std::sin(p_prev) * std::cos(y_prev));
        glm::dvec3 e_y_prev(-std::cos(y_prev), 0.0, std::sin(y_prev));

        double dp = glm::dot(lNodes[i].vDir, e_p_prev);
        double dy = glm::dot(lNodes[i].vDir, e_y_prev);

        lNodes[i].fPitchFromLast = dp * 180.0 / F_PI;

        double cos_p = std::cos(p_prev);
        if (std::abs(cos_p) > 0.001) {
            lNodes[i].fYawFromLast = (dy / cos_p) * 180.0 / F_PI;
        } else {
            lNodes[i].fYawFromLast = 0.0;
        }

        lNodes[i].fDirFromLast =
            glm::atan(lNodes[i].fYawFromLast, lNodes[i].fPitchFromLast) * 180.0 /
                F_PI -
            lNodes[i].fRoll;
    }

    glm::dvec3 curDirHeart = lNodes[i].vDirHeart(parent->fHeart);
    glm::dvec3 prevDirHeart = lNodes[i - 1].vDirHeart(parent->fHeart);
    double fTrackPitchFromLast =
        180.0 / F_PI * (asin(curDirHeart.y) - asin(prevDirHeart.y));
    double fTrackYawFromLast = 180.0 / F_PI *
                               (glm::atan(-curDirHeart.x, -curDirHeart.z) -
                                glm::atan(-prevDirHeart.x, -prevDirHeart.z));
    double temp = cos(fabs(asin(curDirHeart.y)));
    lNodes[i].fTrackAngleFromLast =
        sqrt(temp * temp * fTrackYawFromLast * fTrackYawFromLast +
             fTrackPitchFromLast * fTrackPitchFromLast);
    if (lNodes[i].fYawFromLast > 270.0) {
        lNodes[i].fYawFromLast -= 360.0;
    } else if (lNodes[i].fYawFromLast < -270.0) {
        lNodes[i].fYawFromLast += 360.0;
    }
    return;
}

bool section::setLocked(eFunctype func, int _id, bool _locked) {
    switch (func) {
    case funcRoll:
        if (!_locked) {
            return rollFunc->unlock(_id);
        } else if (normForce == NULL && latForce == NULL) {
            return false;
        } else if (normForce->lockedFunc() > -1 && latForce->lockedFunc() > -1) {
            return false;
        } else {
            return rollFunc->lock(_id);
        }
        break;
    case funcNormal:
        if (!_locked) {
            return normForce->unlock(_id);
        } else if (rollFunc->lockedFunc() > -1 && latForce->lockedFunc() > -1) {
            return false;
        } else {
            return normForce->lock(_id);
        }
        break;
    case funcLateral:
        if (!_locked) {
            return latForce->unlock(_id);
        } else if (rollFunc->lockedFunc() > -1 && normForce->lockedFunc() > -1) {
            return false;
        } else {
            return latForce->lock(_id);
        }
        break;
    case funcPitch:
        if (!_locked) {
            return normForce->unlock(_id);
        } else if (rollFunc->lockedFunc() > -1 && latForce->lockedFunc() > -1) {
            return false;
        } else {
            return normForce->lock(_id);
        }
        break;
    case funcYaw:
        if (!_locked) {
            return latForce->unlock(_id);
        } else if (rollFunc->lockedFunc() > -1 && normForce->lockedFunc() > -1) {
            return false;
        } else {
            return latForce->lock(_id);
        }
        break;
    default:
        lenAssert(0 && "setActive()");
        break;
    }
    return false;
}

double section::getSpeed() {
    if (bSpeed) {
        return lNodes.back().fVel;
    } else {
        return fVel;
    }
}

void section::chaseCurves() {
    if (!gloParent || !gloParent->mOptions || !gloParent->mOptions->enableCurveChasing || !gloParent->selectedFunc)
        return;

    func* activeFunc = gloParent->selectedFunc->parent;
    if (!activeFunc || activeFunc->secParent != this)
        return;

    // Only chase if we have at least two functions to align
    int activeCount = 0;
    if (rollFunc)
        activeCount++;
    if (normForce)
        activeCount++;
    if (latForce)
        activeCount++;
    if (activeCount < 2)
        return;

    double activeLength = activeFunc->getMaxArgument();
    func* secondaryFuncs[2];
    int count = 0;
    if (rollFunc && rollFunc != activeFunc)
        secondaryFuncs[count++] = rollFunc;
    if (normForce && normForce != activeFunc)
        secondaryFuncs[count++] = normForce;
    if (latForce && latForce != activeFunc)
        secondaryFuncs[count++] = latForce;

    for (int f = 0; f < count; ++f) {
        func* secF = secondaryFuncs[f];
        if (secF->lockedFunc() == -1) { // Only chase if the secondary function has no dynamic lock!
            double secLength = secF->getMaxArgument();
            if (activeLength > secLength + 1e-5) {
                double diff = activeLength - secLength;
                int lastIdx = (int)secF->funcList.size() - 1;
                subfunc* lastSeg = secF->funcList[lastIdx];

                // Smart check: constant hold or transition
                if (std::abs(lastSeg->symArg) < 1e-5) {
                    // Case I: Stretch existing constant hold
                    double currentLen = lastSeg->maxArgument - lastSeg->minArgument;
                    secF->changeLength(currentLen + diff, lastIdx);
                } else {
                    // Case II: Append a new constant hold
                    secF->appendSubFunction(diff, lastIdx);
                }
            } else if (activeLength < secLength - 1e-5) {
                // --- CASE B: Active curve is SHORTER -> Truncate/Shrink ---
                double targetLen = activeLength;

                // 1. Delete any trailing segments that start completely after the target length
                while (secF->funcList.size() > 1 && secF->funcList.back()->minArgument >= targetLen - 1e-5) {
                    secF->removeSubFunction((int)secF->funcList.size() - 1);
                }

                // 2. Shrink the remaining final segment so it ends exactly at the target length
                int lastIdx = (int)secF->funcList.size() - 1;
                subfunc* lastSeg = secF->funcList[lastIdx];
                double newSegLen = targetLen - lastSeg->minArgument;
                if (newSegLen < 1e-5) {
                    newSegLen = 1e-5; // safety clamp
                }
                secF->changeLength(newSegLen, lastIdx);
            }
        }
    }
}

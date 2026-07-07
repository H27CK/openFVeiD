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

#include "track.h"
#include <chrono>
#include "exportfuncs.h"
#include <algorithm>
#include "smoothhandler.h"
#include "trackhandler.h"
#include "trackmesh.h"
#include "logger.h"
#include "customstyle.h"
#include "imgui.h"
#include <sstream>
#include <filesystem>

#define RELTHRESH 0.98f

using namespace std;

void writeChunkHeader(std::ostream& out, const char* tag, uint8_t version, uint32_t length) {
    out.write(tag, 4);
    out.write((const char*)&version, 1);
    out.write((const char*)&length, 4);
}

ChunkHeader readChunkHeader(std::istream& in) {
    ChunkHeader h;
    in.read(h.tag, 4);
    in.read((char*)&h.version, 1);
    in.read((char*)&h.length, 4);
    return h;
}

// These will be properly refactored in Phase 3
struct DummyGlobalTrack {
    struct Options {
        int meshQuality = 1;
    }* mOptions = new Options();
}* gloParentTrack = new DummyGlobalTrack();

track::track() {}

track::track(trackHandler* _parent, glm::dvec3 startPos, double startYaw,
             double heartLine) {
    this->anchorNode = new mnode(glm::dvec3(0.0, 0.0, 0.0), glm::dvec3(0, 0, -1),
                                 0.0, 10.0, 1.0, 0.0);
    this->startPos = startPos;
    this->startYaw = startYaw;
    this->startPitch = 0.0;
    povPos = glm::vec2(0, 0);
    mParent = _parent;
    anchorNode->updateNorm();
    anchorNode->fEnergy = 0.5 * anchorNode->fVel * anchorNode->fVel +
                          F_G * anchorNode->fPosHearty(0.9 * heartLine);
    this->fHeart = heartLine;
    fFriction = 0.03;
    fResistance = 2e-5;
    hasChanged = true;
    graphChanged = true;
    drawHeartline = 0;
    mOptions = (void*)gloParentTrack->mOptions;
    activeSection = NULL;

    smoothList.push_back(new smoothHandler(this, -1));

    smoothedUntil = 0;
    style = parametric;

    // Default 1m gauge rails
    ParametricExtrusion railL;
    railL.shape = ExtrusionShape::Cylindrical;
    railL.size = glm::vec2(0.14f);
    railL.offset = glm::vec2(-0.5f, 0.0f);
    customExtrusions.push_back(railL);

    ParametricExtrusion railR;
    railR.shape = ExtrusionShape::Cylindrical;
    railR.size = glm::vec2(0.14f);
    railR.offset = glm::vec2(0.5f, 0.0f);
    customExtrusions.push_back(railR);

    // testing backtrace
}

track::~track() {
    for (auto sec : lSections) {
        delete sec;
    }
    lSections.clear();

    for (auto smooth : smoothList) {
        delete smooth;
    }
    smoothList.clear();

    delete anchorNode;
}

void track::removeSection(int index) {
    if (lSections.size() <= (size_t)index)
        return;

    LOG_INFO("Removing section at index %d (name: %s)", index, lSections[index]->sName.c_str());

    delete smoothList[index + 1];
    smoothList.erase(smoothList.begin() + index + 1);

    if ((size_t)index == lSections.size() - 1) {
        delete this->lSections.at(index);
        this->lSections.erase(lSections.begin() + index);
        if (lSections.size() != 0) {
            if (index > 0)
                activeSection = lSections.at(index - 1);
            else
                activeSection = lSections.at(0);
        } else {
            activeSection = NULL;
        }

        mParent->mMesh->buildMeshes(getNumPoints() - 50 < 0 ? 0
                                                            : getNumPoints() - 50);

    } else {
        lSections.at(index + 1)->lNodes.insert(lSections.at(index + 1)->lNodes.begin(), lSections.at(index)->lNodes.at(0));
        delete this->lSections.at(index);
        this->lSections.erase(lSections.begin() + index);
        activeSection = lSections.at(index);

        updateTrack(index, 0); // lSections[index]->lNodes.size()-2);
    }
}

void track::removeSection(section* fromSection) {
    int i = 0;
    if (lSections.size() == 0)
        return;
    for (; i < (int)lSections.size(); ++i) {
        if (lSections.at(i) == fromSection)
            break;
    }
    removeSection(i);
}

void track::removeSmooth(int fromNode) {
    if (smoothedUntil == fromNode)
        return;
    if (fromNode < 0)
        fromNode = 0;
    smoothedUntil = fromNode;
    mnode *prevNode, *curNode = NULL;
    double temp = 0.0;
    for (int i = 0; i < (int)lSections.size(); ++i) {
        section* curSection = lSections[i];
        if (fromNode >= (int)curSection->lNodes.size() &&
            curSection->lNodes.size() > 1) {
            fromNode -= curSection->lNodes.size() - 1;
            continue;
        }
        if (fromNode != 0)
            curNode = &curSection->lNodes[fromNode - 1];
        else if (i != 0)
            curNode = &lSections[i - 1]->lNodes.back();
        else
            curNode = this->anchorNode;
        for (int j = fromNode; j < (int)curSection->lNodes.size(); ++j) {
            prevNode = curNode;
            curNode = &curSection->lNodes[j];
            if (fabs(curNode->fSmoothSpeed) > 0.0) {
                temp -= curNode->fSmoothSpeed;
                curNode->setRoll(temp / F_HZ);
                curNode->smoothNormal = 0.0;
                curNode->smoothLateral = 0.0;
                curNode->fSmoothSpeed = 0.0;
                curNode->fDistFromLast = glm::distance(curNode->vPosHeart(fHeart),
                                                       prevNode->vPosHeart(fHeart));
                curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
            }
        }
        fromNode = 1;
    }
}

void track::applySmooth(int fromNode) {
    if (fromNode < 0)
        fromNode = 0;
    if (smoothedUntil != fromNode) {
        std::cerr << "Smoothing state unstable!" << std::endl;
        return;
    }
    mnode *prevNode, *curNode = NULL;
    smoothedUntil = getNumPoints();
    double temp = 0.0;
    for (int i = 0; i < (int)lSections.size(); ++i) {
        section* curSection = lSections[i];
        if (fromNode >= (int)curSection->lNodes.size() &&
            curSection->lNodes.size() > 1) {
            fromNode -= curSection->lNodes.size() - 1;
            continue;
        }
        if (fromNode != 0)
            curNode = &curSection->lNodes[fromNode - 1];
        else if (i != 0)
            curNode = &lSections[i - 1]->lNodes.back();
        else
            curNode = this->anchorNode;
        for (int j = fromNode; j < (int)curSection->lNodes.size(); ++j) {
            prevNode = curNode;
            curNode = &curSection->lNodes[j];
            if (fabs(curNode->fSmoothSpeed) > 0.0) {
                temp += curNode->fSmoothSpeed;
                curNode->setRoll(temp / F_HZ);
                curNode->calcSmoothForces();
                curNode->fDistFromLast = glm::distance(curNode->vPosHeart(fHeart),
                                                       prevNode->vPosHeart(fHeart));
                curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;
            }
        }
        fromNode = 1;
    }
}

void track::updateTrack(int index, int iNode) {
    auto startTime = std::chrono::high_resolution_clock::now();
    if (index < 0)
        index = 0;
    if (lSections.size() <= (size_t)index) {
        hasChanged = true;
        if (index == 0 && mParent->mMesh != NULL)
            mParent->mMesh->buildMeshes(0);
        graphChanged = true;
        return; // for savety
    }

    this->isAnyNodeNearGimbalLock = false;
    const double gimbalThreshold = sin(glm::radians(89.5));

    bool useSmoothing = false;

    int nodeAt =
        (lSections[index]->type == straight || lSections[index]->type == curved)
            ? 0
            : iNode;
    for (int i = 0; i < index; ++i) {
        nodeAt += lSections[i]->lNodes.size() - 1;
    }

    for (int i = 0; i < (int)smoothList.size(); ++i) {
        smoothHandler* cur = smoothList[i];
        if (cur->active == false)
            continue;

        cur->update();
        if (cur->getTo() > nodeAt) {
            useSmoothing = true;
            if (cur->getFrom() < nodeAt) {
                nodeAt = cur->getFrom();
                i = -1;
            }
        }
    }

    if (useSmoothing) {
        removeSmooth(nodeAt);
    }

    if (index == 0 && iNode == 0 && !lSections.empty()) {
        lSections.at(0)->lNodes.at(0) = *anchorNode;
    }

    int updateFrom = lSections.at(index)->updateSection(iNode);
    for (int i = index + 1; i < (int)lSections.size(); i++) {
        lSections.at(i)->lNodes.insert(lSections.at(i)->lNodes.begin(),
                                       lSections.at(i - 1)->lNodes[lSections.at(i - 1)->lNodes.size() - 1]);
        lSections.at(i)->updateSection(0);
    }

    // Check for gimbal lock on all updated nodes (only standard geometric sections can trigger it)
    for (int i = index; i < (int)lSections.size(); ++i) {
        bool canGimbalLock = (lSections[i]->type == geometric);
        for (auto& node : lSections[i]->lNodes) {
            if (canGimbalLock && std::abs(node.vDir.y) > gimbalThreshold) {
                node.nearGimbalLock = true;
                this->isAnyNodeNearGimbalLock = true;
            } else {
                node.nearGimbalLock = false;
            }
        }
    }

    /*if (useSmoothing && smoother && smoother->active())
      smoother->applyRollSmooth(nodeAt);*/

    nodeAt = nodeAt > getNumPoints(lSections[index]) + updateFrom
                 ? getNumPoints(lSections[index]) + updateFrom
                 : nodeAt;

    if (mParent->mMesh != NULL)
        mParent->mMesh->buildMeshes(nodeAt);

    hasChanged = true;
    graphChanged = true;

    auto endTime = std::chrono::high_resolution_clock::now();
    this->lastUpdateTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
}

void track::updateTrack(section* fromSection, int iNode) {
    int i = 0;
    if (lSections.size() == 0)
        return;
    for (; i < (int)lSections.size(); ++i) {
        if (lSections.at(i) == fromSection)
            break;
    }
    updateTrack(i, iNode);
}

void track::newSection(enum secType type, int index, bool deferUpdate) {
    LOG_INFO("Adding new section of type %d at index %d", (int)type, index);
    mnode* startNode;
    if (!lSections.empty()) {
        section* temp;
        if (index == -1) {
            temp = lSections.back();
            startNode = &temp->lNodes.back();
        } else if (index == 0) {
            startNode = anchorNode;
            lSections.at(0)->lNodes.erase(lSections.at(0)->lNodes.begin());
        } else {
            temp = lSections.at(index - 1);
            startNode = &temp->lNodes.back();
            if (lSections.size() > (size_t)index) {
                lSections.at(index)->lNodes.erase(lSections.at(index)->lNodes.begin());
            }
        }
    } else {
        startNode = anchorNode;
    }

    section* newSection;
    switch (type) {
    case 1:
        newSection = new secstraight(this, startNode, 10);
        break;
    case 2:
        newSection = new seccurved(this, startNode, 90, 15);
        break;
    case 3:
        newSection = new secforced(this, startNode, 1000);
        break;
    case 4:
        newSection = new secgeometric(this, startNode, 1000);
        break;
    case 5:
        newSection = new secbezier(this, startNode);
        break;
    case 6:
        newSection = new secnlcsv(this, startNode);
        break;
    case 7:
        newSection = new secgeometricriderlocal(this, startNode, 1000);
        break;
    default:
        newSection = NULL;
        std::cerr << "Wrong Section type defined!" << std::endl;
    }
    activeSection = newSection;
    if (newSection == NULL) {
        std::cerr << "Failed to create section of type " << (int)type << std::endl;
        return;
    }
    if (index == -1) {
        lSections.push_back(newSection);
        if (!deferUpdate)
            newSection->updateSection();

        smoothList.insert(smoothList.begin() + lSections.size(),
                          new smoothHandler(this, lSections.size() - 1));
    } else if (index == 0) {
        lSections.insert(lSections.begin(), newSection);
        if (!deferUpdate)
            newSection->updateSection();

        if (lSections.size() > 1) {
            lSections.at(1)->lNodes.insert(lSections.at(1)->lNodes.begin(),
                                           newSection->lNodes.back());
        }
        smoothList.insert(smoothList.begin() + 1, new smoothHandler(this, 0));
    } else {
        lSections.insert(lSections.begin() + index, newSection);
        if (!deferUpdate)
            newSection->updateSection();
        if (lSections.size() > (size_t)index + 1) {
            lSections.at(index + 1)->lNodes.insert(lSections.at(index + 1)->lNodes.begin(),
                                                   newSection->lNodes.back());
        }
        smoothList.insert(smoothList.begin() + index + 1, new smoothHandler(this, index));
    }
    hasChanged = true;
}

void track::clearTrack() {
    for (auto sec : lSections) {
        delete sec;
    }
    lSections.clear();

    // Reset anchor node to default local state
    anchorNode->vPos = glm::dvec3(0.0);
    anchorNode->vDir = glm::dvec3(0.0, 0.0, -1.0);
    anchorNode->vLat = glm::dvec3(1.0, 0.0, 0.0);
    anchorNode->fRoll = 0.0;
    anchorNode->fVel = 10.0;
    anchorNode->forceNormal = 1.0;
    anchorNode->forceLateral = 0.0;
    anchorNode->updateNorm();

    // Preserve the first smoothHandler (the global one)
    if (smoothList.size() > 1) {
        for (size_t i = 1; i < smoothList.size(); ++i) {
            delete smoothList[i];
        }
        smoothList.erase(smoothList.begin() + 1, smoothList.end());
    }

    activeSection = nullptr;
    smoothedUntil = 0;
}

int track::exportTrack(fstream* file, double mPerNode, int fromIndex,
                       int toIndex, double fRollThresh) {
    std::vector<mnode> exportNodes;
    mnode anchor = lSections.at(fromIndex)->lNodes[0];
    for (int i = fromIndex; i <= toIndex; ++i) {
        lSections.at(i)->fillNodeList(exportNodes, mPerNode);
    }

    mnode lastP = anchor;
    std::vector<bezier_t*> bezList;

    for (int i = 0; i < (int)exportNodes.size(); ++i) {
        mnode* curP = &exportNodes[i];

        curP->exportNode(bezList, &lastP, NULL, &anchor, fHeart, fRollThresh);

        lastP = *curP;
    }

    if (exportNodes.size() <= 1) {
        return 0;
    }

    size_t size = (exportNodes.size() - 1);
    double* a = new double[size];
    double* b = new double[size];
    double* c = new double[size];
    glm::dvec3* d = new glm::dvec3[size]();

    for (size_t i = 0; i < size; ++i) {
        if (i == 0) {
            b[i] = 2.0;
            a[i] = 0.0;
            c[i] = 1.0;
            d[i] = bezList[i]->P1 + 2.0 * bezList[i + 1]->P1;
        } else if (i == size - 1) {
            b[i] = 7.0;
            a[i] = 2.0;
            c[i] = 0.0;
            d[i] = 8.0 * bezList[i]->P1 + bezList[i + 1]->P1;
        } else {
            a[i] = 1.0;
            b[i] = 4.0;
            c[i] = 1.0;
            d[i] = 4.0 * bezList[i]->P1 + 2.0 * bezList[i + 1]->P1;
        }
    }

    // solve that shit

    c[0] = c[0] / b[0];
    d[0] = d[0] / b[0];

    for (size_t i = 1; i < size; ++i) {
        double m = 1.0 / (b[i] - a[i] * c[i - 1]);
        c[i] = c[i] * m;
        d[i] = m * (d[i] - a[i] * d[i - 1]);
    }

    for (size_t i = size - 1; i-- > 0;) {
        d[i] = d[i] - c[i] * d[i + 1];
        bezList[i + 1]->Kp1 = d[i];
        bezList[i + 1]->Kp2 = (bezList[i]->P1 - bezList[i]->Kp1) + bezList[i]->P1;
    }

    bezList.back()->Kp1 = 0.5 * (bezList.back()->P1 + bezList[size - 2]->Kp2);
    bezList.back()->Kp2 =
        (bezList.back()->P1 - bezList.back()->Kp1) + bezList.back()->P1;

    writeToExportFile(file, bezList);

    delete[] a;
    delete[] b;
    delete[] c;
    delete[] d;

    return exportNodes.size();
}

int track::exportTrack2(fstream* file, double mPerNode, int fromIndex,
                        int toIndex, double fRollThresh) {
    std::vector<mnode> exportNodes;
    mnode* anchor = &lSections.at(fromIndex)->lNodes[0];
    glm::dvec3 anchorPos = anchor->vPosHeart(fHeart);
    for (int i = fromIndex; i <= toIndex; ++i) {
        lSections.at(i)->fillNodeList(exportNodes, mPerNode);
    }

    glm::dvec3 KP1_this, P, KP2_this;

    KP2_this = anchor->vDirHeart(fHeart) *
               glm::distance(anchor->vPosHeart(fHeart),
                             exportNodes[0].vPosHeart(fHeart)) /
               3.0;

    float fval;
    fval = (float)KP2_this.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP2_this.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP2_this.z;
    writeBytes(file, (const char*)&fval, 4);

    glm::dvec3 startPoint =
        exportNodes[0].vPosHeart(fHeart) +
        (KP2_this + anchorPos - exportNodes[0].vPosHeart(fHeart)) /
            2.0 * 3.0;

    P = glm::dvec3((1 / 6.0) *
                   (startPoint +
                    4.0 * exportNodes[0].vPosHeart(fHeart) +
                    exportNodes[1].vPosHeart(fHeart))) -
        anchorPos;
    KP1_this = glm::dvec3((1 / 3.0) *
                          (startPoint +
                           2.0 * exportNodes[0].vPosHeart(fHeart))) -
               anchorPos;
    KP2_this = glm::dvec3((1 / 3.0) *
                          (2.0 * exportNodes[0].vPosHeart(fHeart) +
                           exportNodes[1].vPosHeart(fHeart))) -
               anchorPos;

    fval = (float)KP1_this.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP1_this.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP1_this.z;
    writeBytes(file, (const char*)&fval, 4);

    fval = (float)P.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)P.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)P.z;
    writeBytes(file, (const char*)&fval, 4);

    glm::dvec3 V = glm::normalize(P - KP1_this);

    glm::dvec3 vHeartLat =
        glm::normalize(glm::cross(exportNodes[0].vNorm, V));
    float temp = (float)glm::atan(vHeartLat.y, -exportNodes[0].vNorm.y);
    if (fabs(V.y) > fRollThresh) {
        glm::dvec3 vLastLat = anchor->vLatHeart(fHeart);

        glm::dvec3 rotateAxis = glm::cross(anchor->vDirHeart(fHeart), V);
        glm::dvec3 rotated = glm::dvec3(
            glm::rotate(glm::angle(anchor->vDirHeart(fHeart), V), rotateAxis) *
            glm::dvec4(vLastLat, 0.0));
        temp = (float)(glm::angle(rotated, vHeartLat) * F_PI / 180.0);
        if (glm::dot(glm::cross(rotated, vHeartLat), V) > 0) {
            temp *= -1.0f;
        }
        if (temp != temp) {
            temp = 0.0f;
        }
    }
    writeBytes(file, (const char*)&(temp), 4);

    char cTemp;

    cTemp = (char)0xFF;
    writeBytes(file, &cTemp, 1); // CONT ROLL
    cTemp = (char)0x00;
    if (fabs(V.y) > fRollThresh) {
        cTemp = (char)0xFF;
    }
    writeBytes(file, &cTemp, 1); // equalDistanceCP
    cTemp = (char)0x00;
    writeBytes(file, &cTemp, 1); // REL ROLL
    writeNulls(file, 7);         // were 5

    int i = 0;
    for (i = 1; i < (int)exportNodes.size() - 2; ++i) {
        fval = (float)KP2_this.x;
        writeBytes(file, (const char*)&fval, 4);
        fval = (float)KP2_this.y;
        writeBytes(file, (const char*)&fval, 4);
        fval = (float)KP2_this.z;
        writeBytes(file, (const char*)&fval, 4);

        P = glm::dvec3((1 / 6.0) *
                       (exportNodes[i - 1].vPosHeart(fHeart) +
                        4.0 * exportNodes[i].vPosHeart(fHeart) +
                        exportNodes[i + 1].vPosHeart(fHeart))) -
            anchorPos;
        KP1_this = glm::dvec3((1 / 3.0) *
                              (exportNodes[i - 1].vPosHeart(fHeart) +
                               2.0 * exportNodes[i].vPosHeart(fHeart))) -
                   anchorPos;
        KP2_this = glm::dvec3((1 / 3.0) *
                              (2.0 * exportNodes[i].vPosHeart(fHeart) +
                               exportNodes[i + 1].vPosHeart(fHeart))) -
                   anchorPos;

        fval = (float)KP1_this.x;
        writeBytes(file, (const char*)&fval, 4);
        fval = (float)KP1_this.y;
        writeBytes(file, (const char*)&fval, 4);
        fval = (float)KP1_this.z;
        writeBytes(file, (const char*)&fval, 4);

        fval = (float)P.x;
        writeBytes(file, (const char*)&fval, 4);
        fval = (float)P.y;
        writeBytes(file, (const char*)&fval, 4);
        fval = (float)P.z;
        writeBytes(file, (const char*)&fval, 4);

        glm::dvec3 V = glm::normalize(P - KP1_this);

        glm::dvec3 vHeartLat =
            glm::normalize(glm::cross(exportNodes[i].vNorm, V));
        float temp = (float)glm::atan(vHeartLat.y, -exportNodes[i].vNorm.y);
        if (fabs(V.y) > fRollThresh) {
            glm::dvec3 vLastLat = exportNodes[i - 1].vLatHeart(fHeart);

            glm::dvec3 rotateAxis =
                glm::cross(exportNodes[i - 1].vDirHeart(fHeart), V);
            glm::dvec3 rotated = glm::dvec3(
                glm::rotate(
                    glm::angle(exportNodes[i - 1].vDirHeart(fHeart), V),
                    rotateAxis) *
                glm::dvec4(vLastLat, 0.0));
            temp = (float)(glm::angle(rotated, vHeartLat) * F_PI / 180.0);
            if (glm::dot(glm::cross(rotated, vHeartLat), V) > 0) {
                temp *= -1.0f;
            }
            if (temp != temp) {
                temp = 0.0f;
            }
        }
        writeBytes(file, (const char*)&(temp), 4);

        char cTemp;

        cTemp = (char)0xFF;
        writeBytes(file, &cTemp, 1); // CONT ROLL
        cTemp = (char)0x00;
        if (fabs(V.y) > fRollThresh) {
            cTemp = (char)0xFF;
        }
        writeBytes(file, &cTemp, 1); // equalDistanceCP
        cTemp = (char)0x00;
        writeBytes(file, &cTemp, 1); // REL ROLL
        writeNulls(file, 7);         // were 5
    }

    fval = (float)KP2_this.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP2_this.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP2_this.z;
    writeBytes(file, (const char*)&fval, 4);

    glm::dvec3 endPoint =
        exportNodes.back().vPosHeart(fHeart) -
        exportNodes.back().vDirHeart(fHeart) *
            glm::distance(exportNodes.back()
                              .vPosHeart(fHeart),
                          exportNodes[exportNodes.size() - 2]
                              .vPosHeart(fHeart));

    P = glm::dvec3((1 / 6.0) *
                   (exportNodes[i - 1].vPosHeart(fHeart) +
                    4.0 * endPoint +
                    exportNodes[i + 1].vPosHeart(fHeart))) -
        anchorPos;
    KP1_this =
        glm::dvec3((1 / 3.0) * (exportNodes[i - 1].vPosHeart(fHeart) +
                                2.0 * endPoint)) -
        anchorPos;
    KP2_this = glm::dvec3((1 / 3.0) *
                          (2.0 * endPoint +
                           exportNodes[i + 1].vPosHeart(fHeart))) -
               anchorPos;

    fval = (float)KP1_this.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP1_this.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP1_this.z;
    writeBytes(file, (const char*)&fval, 4);

    fval = (float)P.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)P.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)P.z;
    writeBytes(file, (const char*)&fval, 4);

    V = glm::normalize(P - KP1_this);

    vHeartLat = glm::normalize(glm::cross(exportNodes[i].vNorm, V));
    temp = (float)glm::atan(vHeartLat.y, -exportNodes[i].vNorm.y);
    if (fabs(V.y) > fRollThresh) {
        glm::dvec3 vLastLat = exportNodes[i - 1].vLatHeart(fHeart);

        glm::dvec3 rotateAxis =
            glm::cross(exportNodes[i - 1].vDirHeart(fHeart), V);
        glm::dvec3 rotated = glm::dvec3(
            glm::rotate(
                glm::angle(exportNodes[i - 1].vDirHeart(fHeart), V),
                rotateAxis) *
            glm::dvec4(vLastLat, 0.0));
        temp = (float)(glm::angle(rotated, vHeartLat) * F_PI / 180.0);
        if (glm::dot(glm::cross(rotated, vHeartLat), V) > 0) {
            temp *= -1.0f;
        }
        if (temp != temp) {
            temp = 0.0f;
        }
    }
    writeBytes(file, (const char*)&(temp), 4);

    cTemp = (char)0xFF;
    writeBytes(file, &cTemp, 1); // CONT ROLL
    cTemp = (char)0x00;
    if (fabs(V.y) > fRollThresh) {
        cTemp = (char)0xFF;
    }
    writeBytes(file, &cTemp, 1); // equalDistanceCP
    cTemp = (char)0x00;
    writeBytes(file, &cTemp, 1); // REL ROLL
    writeNulls(file, 7);         // were 5

    ++i;

    fval = (float)KP2_this.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP2_this.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP2_this.z;
    writeBytes(file, (const char*)&fval, 4);

    P = exportNodes.back().vPosHeart(fHeart) -
        anchorPos;
    KP1_this =
        P - exportNodes.back().vDirHeart(fHeart) *
                glm::distance(exportNodes.back()
                                  .vPosHeart(fHeart),
                              exportNodes[exportNodes.size() - 2]
                                  .vPosHeart(fHeart)) /
                3.0;

    fval = (float)KP1_this.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP1_this.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)KP1_this.z;
    writeBytes(file, (const char*)&fval, 4);

    fval = (float)P.x;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)P.y;
    writeBytes(file, (const char*)&fval, 4);
    fval = (float)P.z;
    writeBytes(file, (const char*)&fval, 4);

    V = glm::normalize(P - KP1_this);

    vHeartLat = glm::normalize(glm::cross(exportNodes[i].vNorm, V));
    temp = (float)glm::atan(vHeartLat.y, -exportNodes[i].vNorm.y);
    if (fabs(V.y) > fRollThresh) {
        glm::dvec3 vLastLat = exportNodes[i - 1].vLatHeart(fHeart);

        glm::dvec3 rotateAxis =
            glm::cross(exportNodes[i - 1].vDirHeart(fHeart), V);
        glm::dvec3 rotated = glm::dvec3(
            glm::rotate(
                glm::angle(exportNodes[i - 1].vDirHeart(fHeart), V),
                rotateAxis) *
            glm::dvec4(vLastLat, 0.0));
        temp = (float)(glm::angle(rotated, vHeartLat) * F_PI / 180.0);
        if (glm::dot(glm::cross(rotated, vHeartLat), V) > 0) {
            temp *= -1.0f;
        }
        if (temp != temp) {
            temp = 0.0f;
        }
    }
    writeBytes(file, (const char*)&(temp), 4);

    cTemp = (char)0xFF;
    writeBytes(file, &cTemp, 1); // CONT ROLL
    cTemp = (char)0x00;
    if (fabs(V.y) > fRollThresh) {
        cTemp = (char)0xFF;
    }
    writeBytes(file, &cTemp, 1); // equalDistanceCP
    cTemp = (char)0x00;
    writeBytes(file, &cTemp, 1); // REL ROLL
    writeNulls(file, 7);         // were 5

    return exportNodes.size();
}

// spline export
int track::exportTrack3(fstream* file, double mPerNode, int fromIndex,
                        int toIndex, double fRollThresh) {
    std::vector<mnode> exportNodes;
    mnode* anchor = &lSections.at(fromIndex)->lNodes[0];
    for (int i = fromIndex; i <= toIndex; ++i) {
        lSections.at(i)->fillNodeList(exportNodes, mPerNode);
    }

    mnode *lastP = anchor, *curP = &exportNodes[0];
    std::vector<bezier_t*> bezList;

    bezList.push_back(new bezier_t);
    bezList[0]->P1 = glm::dvec3(0.0, 0.0, 0.0);

    for (int i = 0; i < (int)exportNodes.size(); ++i) {
        curP = &exportNodes[i];

        curP->exportNode(bezList, lastP, NULL, anchor, fHeart, fRollThresh);

        lastP = curP;
    }

    size_t size = bezList.size();
    std::vector<double> a(size);
    std::vector<double> b(size);
    std::vector<double> c(size);
    std::vector<glm::dvec3> d(size);

    for (size_t i = 0; i < size; ++i) {
        d[i] = bezList[i]->P1;
        if (i == 0) {
            a[i] = 0.0;
            b[i] = 1.0;
            c[i] = 0.0;
        } else if (i == size - 1) {
            a[i] = 0.0;
            b[i] = 1.0;
            c[i] = 0.0;
        } else {
            a[i] = 1.0 / 6.0;
            b[i] = 4.0 / 6.0;
            c[i] = 1.0 / 6.0;
        }
    }

    // solve that shit

    c[0] = c[0] / b[0];
    d[0] = d[0] / b[0];

    for (size_t i = 1; i < size; ++i) {
        double m = 1.0 / (b[i] - a[i] * c[i - 1]);
        c[i] = c[i] * m;
        d[i] = m * (d[i] - a[i] * d[i - 1]);
    }

    for (size_t i = size - 1; i-- > 0;) {
        d[i] = d[i] - c[i] * d[i + 1];
    }

    int i;
    for (i = 1; i < (int)bezList.size(); ++i) {
        bezList[i]->Kp1 = 1.0 / 3.0 * (d[i] + 2.0 * d[i - 1]);
        bezList[i]->Kp2 = 1.0 / 3.0 * (2.0 * d[i] + d[i - 1]);
        bezList[i - 1]->P1 = 0.5 * (bezList[i - 1]->Kp2 + bezList[i]->Kp1);
    }
    delete bezList[0];

    bezList.erase(bezList.begin());

    writeToExportFile(file, bezList);

    for (int i = 0; i < (int)bezList.size(); ++i) {
        delete bezList[i];
    }

    return exportNodes.size();
}

// tangent export
int track::exportTrack4(fstream* file, double mPerNode, int fromIndex,
                        int toIndex, double fRollThresh) {
    std::vector<mnode> exportNodes;
    mnode* anchor = &lSections.at(fromIndex)->lNodes[0];
    for (int i = fromIndex; i <= toIndex; ++i) {
        lSections.at(i)->fillNodeList(exportNodes, mPerNode);
    }

    mnode *last = anchor, *current = &exportNodes[0],
          *mid = NULL;
    std::vector<bezier_t*> bezList;

    for (int i = 0; i < (int)exportNodes.size(); ++i) {
        current = &exportNodes[i];

        mid = NULL;

        current->exportNode(bezList, last, mid, anchor, fHeart, fRollThresh);

        last = current;
    }

    writeToExportFile(file, bezList);

    return exportNodes.size();
}

void track::exportNL2Track(FILE* file, double mPerNode, int fromIndex,
                           int toIndex) {
    std::vector<ExportNode> exportNodes;
    mnode* anchor = &lSections.at(fromIndex)->lNodes[0];
    ExportNode first;
    first.node = lSections.at(fromIndex)->lNodes[0];
    first.isBoundary = false;
    exportNodes.push_back(first);
    for (int i = fromIndex; i <= toIndex; ++i) {
        lSections.at(i)->fFillNodeList(exportNodes, mPerNode);
    }

    size_t size = exportNodes.size();
    std::vector<double> a(size);
    std::vector<double> b(size);
    std::vector<double> c(size);
    std::vector<glm::dvec3> d(size);

    for (size_t i = 0; i < size; ++i) {
        mnode* curNode = &exportNodes[i].node;
        d[i] = curNode->vPos;
        if (i == 0 || i == size - 1 || exportNodes[i].isBoundary) {
            a[i] = 0.0;
            b[i] = 1.0;
            c[i] = 0.0;
        } else {
            a[i] = 1.0 / 6.0;
            b[i] = 4.0 / 6.0;
            c[i] = 1.0 / 6.0;
        }
    }

    // tridiagonal solver
    c[0] = c[0] / b[0];
    d[0] = d[0] / b[0];

    for (size_t i = 1; i < size; ++i) {
        double m = 1.0 / (b[i] - a[i] * c[i - 1]);
        c[i] = c[i] * m;
        d[i] = m * (d[i] - a[i] * d[i - 1]);
    }

    for (size_t i = size - 1; i-- > 0;) {
        d[i] = d[i] - c[i] * d[i + 1];
    }

    // resolve strictness
    std::vector<glm::dvec4> e;
    e.push_back(glm::dvec4(d[0], 1.0));
    for (size_t i = 1; i < size - 1; ++i) {
        if (lSections[0]->type != 6) {
            int strict = 0;
            if (exportNodes[i - 1].isBoundary || (i == 1 && fromIndex == 0)) {
                strict |= 1;
            }
            if (exportNodes[i].isBoundary) {
                strict |= 2;
            }
            if (exportNodes[i + 1].isBoundary || (i == size - 2 && exportNodes.size() > 1 && exportNodes.back().node.fTotalLength == lSections[0]->lNodes[0].fTotalLength)) {
                strict |= 4;
            }

            if (strict == 1) {
                glm::dvec3 dir = exportNodes[i - 1].node.vDir;
                glm::dvec3 nP = exportNodes[i + 1].node.vPos;
                double a = glm::length(nP - d[i - 1]);
                double cosa = glm::dot(glm::normalize(nP - d[i - 1]), dir);
                e.push_back(glm::dvec4(d[i - 1] + dir * a / (2.0 * cosa), 0.0));
            } else if (strict == 2) {
                // bad export status
            } else if (strict == 3) {
                e.push_back(glm::dvec4(d[i], 1.0));
            } else if (strict == 4) {
                glm::dvec3 dir = exportNodes[i + 1].node.vDir;
                glm::dvec3 pP = exportNodes[i - 1].node.vPos;
                double a = glm::length(d[i + 1] - pP);
                double cosa = glm::dot(glm::normalize(d[i + 1] - pP), dir);
                e.push_back(glm::dvec4(d[i + 1] - dir * a / (2.0 * cosa), 0.0));
            } else if (strict == 5) {
                glm::dvec3 dp = exportNodes[i + 1].node.vPos - exportNodes[i - 1].node.vPos;
                glm::dvec3 dv = exportNodes[i + 1].node.vDir + exportNodes[i - 1].node.vDir;

                double a = glm::length2(dv) - 1.0;
                double b = glm::dot(dv, dp) * 2.0;
                double c = glm::length2(dp);

                b /= a;
                c /= a;

                double p = b / 2.0;
                double x0 = -p + sqrt(p * p - c);

                e.push_back(glm::dvec4(
                    exportNodes[i - 1].node.vPos - x0 * exportNodes[i - 1].node.vDir, 0.0));
                e.push_back(glm::dvec4(
                    exportNodes[i + 1].node.vPos + x0 * exportNodes[i + 1].node.vDir, 0.0));

            } else if (strict == 6) {
                e.push_back(glm::dvec4(d[i], 1.0));
            } else if (strict == 7) {
                e.push_back(glm::dvec4(d[i], 1.0));
            } else {
                e.push_back(glm::dvec4(d[i], 0.0));
            }
        } else {
            e.push_back(glm::dvec4(d[i], 0.0));
        }
    }
    e.push_back(glm::dvec4(d[size - 1], 1.0));

    std::vector<glm::dvec3> transformed(e.size());
    glm::dmat4 anchorBase = glm::translate(glm::dmat4(1.0), this->startPos) *
                            glm::rotate(glm::dmat4(1.0), (double)TO_RAD(this->startYaw - 90.0), glm::dvec3(0.0, 1.0, 0.0));

    for (int i = 0; i < (int)e.size(); ++i) {
        transformed[i] = glm::dvec3(anchorBase * glm::dvec4(glm::dvec3(e[i]), 1.0));
    }

    // bounding box centering
    glm::dvec3 bboxMin = transformed[0];
    glm::dvec3 bboxMax = transformed[0];
    for (int i = 1; i < (int)transformed.size(); ++i) {
        glm::dvec3 p = transformed[i];
        bboxMin = glm::min(bboxMin, p);
        bboxMax = glm::max(bboxMax, p);
    }

    glm::dvec3 cancellationOffset(0.0);
    glm::dvec3 bboxCenter = (bboxMin + bboxMax) * 0.5;

    cancellationOffset.x = bboxCenter.x - transformed[0].x;
    cancellationOffset.z = bboxCenter.z - transformed[0].z;
    cancellationOffset.y = (bboxMin.y - transformed[0].y) - 1.5;

    for (int i = 0; i < (int)e.size(); ++i) {
        glm::dvec3 ex = transformed[i] + cancellationOffset;
        fprintf(file, "\t\t\t<vertex>\n");
        fprintf(file, "\t\t\t\t<x>%e</x>\n", ex.x);
        fprintf(file, "\t\t\t\t<y>%e</y>\n", ex.y);
        fprintf(file, "\t\t\t\t<z>%e</z>\n", ex.z);
        if (e[i].w > 0.5) {
            fprintf(file, "\t\t\t\t<strict>true</strict>\n");
        }
        fprintf(file, "\t\t\t</vertex>\n");
    }

    double startLen = exportNodes[0].node.fTotalHeartLength;
    double endLen = exportNodes.back().node.fTotalHeartLength;

    for (size_t i = 0; i < size; ++i) {
        mnode* curNode = &exportNodes[i].node;

        glm::dvec3 up = glm::dvec3(anchorBase * glm::dvec4(-curNode->vNorm, 0.0));
        glm::dvec3 right = glm::dvec3(anchorBase * glm::dvec4(curNode->vLat, 0.0));
        double coord =
            (curNode->fTotalHeartLength - startLen) / (endLen - startLen);

        fprintf(file, "\t\t\t<roll>\n");
        fprintf(file, "\t\t\t\t<ux>%e</ux>\n", up.x);
        fprintf(file, "\t\t\t\t<uy>%e</uy>\n", up.y);
        fprintf(file, "\t\t\t\t<uz>%e</uz>\n", up.z);
        fprintf(file, "\t\t\t\t<rx>%e</rx>\n", right.x);
        fprintf(file, "\t\t\t\t<ry>%e</ry>\n", right.y);
        fprintf(file, "\t\t\t\t<rz>%e</rz>\n", right.z);
        fprintf(file, "\t\t\t\t<coord>%e</coord>\n", coord);
        fprintf(file, "\t\t\t\t<strict>false</strict>\n");
        fprintf(file, "\t\t\t</roll>\n");
    }

    return;
}

void track::exportNL2TrackCSV(FILE* file, double mPerNode, int fromIndex,
                              int toIndex) {
    std::vector<ExportNode> exportNodes;
    mnode* anchor = &lSections.at(fromIndex)->lNodes[0];
    ExportNode first;
    first.node = lSections.at(fromIndex)->lNodes[0];
    first.isBoundary = false;
    exportNodes.push_back(first);
    for (int i = fromIndex; i <= toIndex; ++i) {
        lSections.at(i)->fFillNodeList(exportNodes, mPerNode);
    }

    size_t size = exportNodes.size();
    std::vector<double> a(size);
    std::vector<double> b(size);
    std::vector<double> c(size);
    std::vector<glm::dvec3> d(size);

    for (size_t i = 0; i < size; ++i) {
        mnode* curNode = &exportNodes[i].node;
        d[i] = curNode->vPos;
        if (i == 0 || i == size - 1 || exportNodes[i].isBoundary) {
            a[i] = 0.0;
            b[i] = 1.0;
            c[i] = 0.0;
        } else {
            a[i] = 1.0 / 6.0;
            b[i] = 4.0 / 6.0;
            c[i] = 1.0 / 6.0;
        }
    }

    // tridiagonal solver
    c[0] = c[0] / b[0];
    d[0] = d[0] / b[0];

    for (size_t i = 1; i < size; ++i) {
        double m = 1.0 / (b[i] - a[i] * c[i - 1]);
        c[i] = c[i] * m;
        d[i] = m * (d[i] - a[i] * d[i - 1]);
    }

    for (size_t i = size - 1; i-- > 0;) {
        d[i] = d[i] - c[i] * d[i + 1];
    }

    // resolve strictness
    std::vector<glm::dvec4> e;
    e.push_back(glm::dvec4(d[0], 1.0));
    for (size_t i = 1; i < size - 1; ++i) {
        // is this a csv import?
        // if not, treat normally
        if (lSections[0]->type != 6) {
            int strict = 0;
            if (exportNodes[i - 1].isBoundary || (i == 1 && fromIndex == 0)) {
                strict |= 1;
            }
            if (exportNodes[i].isBoundary) {
                strict |= 2;
            }
            if (exportNodes[i + 1].isBoundary ||
                (i == size - 2 && exportNodes.size() > 1 &&
                 exportNodes.back().node.fTotalLength ==
                     lSections[0]->lNodes[0].fTotalLength)) {
                strict |= 4;
            }

            if (strict == 1) {
                glm::dvec3 dir = exportNodes[i - 1].node.vDir;
                glm::dvec3 nP = exportNodes[i + 1].node.vPos;
                double a = glm::length(nP - d[i - 1]);
                double cosa = glm::dot(glm::normalize(nP - d[i - 1]), dir);
                e.push_back(glm::dvec4(d[i - 1] + dir * a / (2.0 * cosa), 0.0));
            } else if (strict == 2) {
                // bad export status
            } else if (strict == 3) {
                e.push_back(glm::dvec4(d[i], 1.0));
            } else if (strict == 4) {
                glm::dvec3 dir = exportNodes[i + 1].node.vDir;
                glm::dvec3 pP = exportNodes[i - 1].node.vPos;
                double a = glm::length(d[i + 1] - pP);
                double cosa = glm::dot(glm::normalize(d[i + 1] - pP), dir);
                e.push_back(glm::dvec4(d[i + 1] - dir * a / (2.0 * cosa), 0.0));
            } else if (strict == 5) {
                glm::dvec3 dp =
                    exportNodes[i + 1].node.vPos - exportNodes[i - 1].node.vPos;
                glm::dvec3 dv =
                    exportNodes[i + 1].node.vDir + exportNodes[i - 1].node.vDir;

                double a = glm::length2(dv) - 1.0;
                double b = glm::dot(dv, dp) * 2.0;
                double c = glm::length2(dp);

                b /= a;
                c /= a;

                double p = b / 2.0;
                double x0 = -p + sqrt(p * p - c);

                e.push_back(glm::dvec4(
                    exportNodes[i - 1].node.vPos - x0 * exportNodes[i - 1].node.vDir,
                    0.0));
                e.push_back(glm::dvec4(
                    exportNodes[i + 1].node.vPos + x0 * exportNodes[i + 1].node.vDir,
                    0.0));

            } else if (strict == 6) {
                e.push_back(glm::dvec4(d[i], 1.0));
            } else if (strict == 7) {
                e.push_back(glm::dvec4(d[i], 1.0));
            } else {
                e.push_back(glm::dvec4(d[i], 0.0));
            }
        } else {
            e.push_back(glm::dvec4(d[i], 0.0));
        }
    }
    e.push_back(glm::dvec4(d[size - 1], 1.0));

    std::vector<glm::dvec3> transformed(e.size());
    glm::dmat4 anchorBase = glm::translate(glm::dmat4(1.0), this->startPos) *
                            glm::rotate(glm::dmat4(1.0), (double)TO_RAD(this->startYaw - 90.0), glm::dvec3(0.0, 1.0, 0.0));

    for (int i = 0; i < (int)e.size(); ++i) {
        transformed[i] = glm::dvec3(anchorBase * glm::dvec4(glm::dvec3(e[i]), 1.0));
    }

    // bounding box centering
    glm::dvec3 bboxMin = transformed[0];
    glm::dvec3 bboxMax = transformed[0];
    for (int i = 1; i < (int)transformed.size(); ++i) {
        glm::dvec3 p = transformed[i];
        bboxMin = glm::min(bboxMin, p);
        bboxMax = glm::max(bboxMax, p);
    }

    glm::dvec3 cancellationOffset(0.0);
    glm::dvec3 bboxCenter = (bboxMin + bboxMax) * 0.5;

    cancellationOffset.x = bboxCenter.x - transformed[0].x;
    cancellationOffset.z = bboxCenter.z - transformed[0].z;
    cancellationOffset.y = (bboxMin.y - transformed[0].y) - 1.5;

    for (int i = 0; i < (int)e.size(); ++i) {
        glm::dvec3 ex = transformed[i] + cancellationOffset;

        mnode* curNode = &exportNodes[i].node;

        glm::dvec3 front = glm::dvec3(anchorBase * glm::dvec4(curNode->vDir, 0.0));
        glm::dvec3 left = glm::dvec3(anchorBase * glm::dvec4(-curNode->vLat, 0.0));
        glm::dvec3 up = glm::dvec3(anchorBase * glm::dvec4(-curNode->vNorm, 0.0));

        fprintf(file, "%d\t%e\t%e\t%e\t%e\t%e\t%e\t%e\t%e\t%e\t%e\t%e\t%e\n", i + 1,
                ex.x, ex.y, ex.z, front.x, front.y, front.z, left.x, left.y, left.z,
                up.x, up.y, up.z);
    }

    return;
}

std::string track::saveTrack(ostream& file) {
    file << "TRC";

    int namelength = name.length();
    std::string stdName = name;

    writeBytes(&file, (const char*)&namelength, sizeof(int));
    file << stdName;

    // Placeholder for colors (3x RGBA floats = 3x16 bytes)
    writeNulls(&file, 3 * 16);

    // ANCHOR
    glm::vec3 startPosF = (glm::vec3)startPos;
    writeVec3(&file, startPosF);
    float fval;
    fval = (float)anchorNode->fRoll;
    writeBytes(&file, (const char*)&fval, sizeof(float));
    fval = (float)startPitch;
    writeBytes(&file, (const char*)&fval, sizeof(float));
    fval = (float)startYaw;
    writeBytes(&file, (const char*)&fval, sizeof(float));

    fval = (float)anchorNode->fVel;
    writeBytes(&file, (const char*)&fval, sizeof(float));

    fval = (float)anchorNode->forceNormal;
    writeBytes(&file, (const char*)&fval, sizeof(float));
    fval = (float)anchorNode->forceLateral;
    writeBytes(&file, (const char*)&fval, sizeof(float));

    fval = (float)fHeart;
    writeBytes(&file, (const char*)&fval, sizeof(float));
    float dummyGauge = 1.0f;
    writeBytes(&file, (const char*)&dummyGauge, sizeof(float));
    fval = (float)fFriction;
    writeBytes(&file, (const char*)&fval, sizeof(float));
    fval = (float)fResistance;
    writeBytes(&file, (const char*)&fval, sizeof(float));

    bool dummyUseGauge = false;
    writeBytes(&file, (const char*)&dummyUseGauge, sizeof(bool));
    bool dummyDrawTrack = (drawHeartline != 3);
    writeBytes(&file, (const char*)&dummyDrawTrack, sizeof(bool));
    writeBytes(&file, (const char*)&drawHeartline, sizeof(int));
    writeBytes(&file, (const char*)&style, sizeof(int));
    bool dummyWireframe = false;
    writeBytes(&file, (const char*)&dummyWireframe, sizeof(bool));

    writeBytes(&file, (const char*)&povPos.x, sizeof(float));
    writeBytes(&file, (const char*)&povPos.y, sizeof(float));

    // float fFriction;
    int size = lSections.size();
    writeBytes(&file, (const char*)&size, sizeof(int));

    for (int i = 0; i < (int)lSections.size(); ++i) {
        lSections.at(i)->saveSection(file);
    }

    size = smoothList.size();
    writeBytes(&file, (const char*)&size, sizeof(int));
    for (int i = 0; i < size; ++i) {
        smoothList[i]->saveSmooth(file);
    }

    file << "TRN";
    int numOffsets = trainOffsets.size();
    writeBytes(&file, (const char*)&numOffsets, sizeof(int));
    for (const auto& offset : trainOffsets) {
        std::string offsetName(offset.name);
        int namelen = offsetName.length();
        writeBytes(&file, (const char*)&namelen, sizeof(int));
        file << offsetName;
        writeBytes(&file, (const char*)&offset.offset.x, sizeof(float));
        writeBytes(&file, (const char*)&offset.offset.y, sizeof(float));
        writeBytes(&file, (const char*)&offset.offset.z, sizeof(float));
        writeBytes(&file, (const char*)&offset.showNormal, sizeof(bool));
        writeBytes(&file, (const char*)&offset.showLateral, sizeof(bool));
        writeBytes(&file, (const char*)&offset.color.x, sizeof(float));
        writeBytes(&file, (const char*)&offset.color.y, sizeof(float));
        writeBytes(&file, (const char*)&offset.color.z, sizeof(float));
    }

    file << "EOT";

    // OPTIONAL PARAMETRIC DATA
    unsigned char marker = 0xFD;
    writeBytes(&file, (const char*)&marker, 1);

    int numExtrusions = customExtrusions.size();
    writeBytes(&file, (const char*)&numExtrusions, sizeof(int));
    for (const auto& ext : customExtrusions) {
        int shape = (int)ext.shape;
        writeBytes(&file, (const char*)&shape, sizeof(int));
        writeBytes(&file, (const char*)&ext.size.x, sizeof(float));
        writeBytes(&file, (const char*)&ext.size.y, sizeof(float));
        writeBytes(&file, (const char*)&ext.offset.x, sizeof(float));
        writeBytes(&file, (const char*)&ext.offset.y, sizeof(float));
    }

    int numAssets = customAssets.size();
    writeBytes(&file, (const char*)&numAssets, sizeof(int));
    for (const auto& asset : customAssets) {
        int pathLen = asset.filepath.length();
        writeBytes(&file, (const char*)&pathLen, sizeof(int));
        file << asset.filepath;
        writeBytes(&file, (const char*)&asset.startDist, sizeof(float));
        writeBytes(&file, (const char*)&asset.endDist, sizeof(float));
        writeBytes(&file, (const char*)&asset.interval, sizeof(float));
        writeBytes(&file, (const char*)&asset.color.x, sizeof(float));
        writeBytes(&file, (const char*)&asset.color.y, sizeof(float));
        writeBytes(&file, (const char*)&asset.color.z, sizeof(float));
        writeBytes(&file, (const char*)&asset.fullLayout, sizeof(bool));
        writeBytes(&file, (const char*)&asset.toEnd, sizeof(bool));
    }

    return std::string("Save Successful");
}

std::string track::loadTrack(istream& file) {
    std::string tag = readString(&file, 3);
    if (tag != "TRC") {
        return std::string("Error: Not a track record!");
    }
    int namelength = readInt(&file);
    name = readString(&file, namelength);

    readNulls(&file, 3 * 16); // Skip colors placeholder

    startPos = readVec3(&file);
    anchorNode->fRoll = std::clamp((double)readFloat(&file), -1800.0, 1800.0);
    startPitch = std::clamp((double)readFloat(&file), -90.0, 90.0);
    startYaw = std::clamp((double)readFloat(&file), -1800.0, 1800.0);
    anchorNode->fVel = std::max(0.1, (double)readFloat(&file));
    anchorNode->forceNormal = std::clamp((double)readFloat(&file), -20.0, 20.0);
    anchorNode->forceLateral = std::clamp((double)readFloat(&file), -15.0, 15.0);

    fHeart = std::clamp((double)readFloat(&file), -10.0, 10.0);
    (void)readFloat(&file); // Discard legacy fGauge
    fFriction = std::max(0.0, (double)readFloat(&file));
    fResistance = std::max(0.0, (double)readFloat(&file));

    (void)readBool(&file); // Discard legacy useGauge
    (void)readBool(&file); // Discard legacy drawTrack
    drawHeartline = readInt(&file);
    style = (enum trackStyle)readInt(&file);
    (void)readBool(&file);

    povPos.x = readFloat(&file);
    povPos.y = readFloat(&file);

    anchorNode->fEnergy = 0.5f * anchorNode->fVel * anchorNode->fVel +
                          F_G * anchorNode->fPosHearty(0.9 * fHeart);

    anchorNode->changePitch(startPitch, false);
    anchorNode->setRoll(anchorNode->fRoll);
    anchorNode->updateNorm();

    string temp;
    int size = readInt(&file);
    for (int i = 0; i < size; ++i) {
        temp = readString(&file, 3);
        if (temp == "STR") {
            newSection(straight, -1, true);
            if (activeSection)
                activeSection->loadSection(file);
        } else if (temp == "CUR") {
            newSection(curved, -1, true);
            if (activeSection)
                activeSection->loadSection(file);
        } else if (temp == "GEO") {
            newSection(geometric, -1, true);
            if (activeSection)
                activeSection->loadSection(file);
        } else if (temp == "FRC") {
            newSection(forced, -1, true);
            if (activeSection)
                activeSection->loadSection(file);
        } else if (temp == "BEZ") {
            newSection(bezier, -1, true);
            if (activeSection)
                activeSection->loadSection(file);
        } else if (temp == "NLC") {
            newSection(nolimitscsv, -1, true);
            if (activeSection)
                activeSection->loadSection(file);
        } else if (temp == "GRL") {
            newSection(geometricriderlocal, -1, true);
            if (activeSection)
                activeSection->loadSection(file);
        } else {
            return std::string("Error while Loading: No Such Segment!");
        }
    }

    size = readInt(&file);
    for (int i = 0; i < size; ++i) {
        if (i >= (int)smoothList.size())
            smoothList.push_back(new smoothHandler(this, -2));
        smoothList[i]->loadSmooth(file);
    }

    temp = readString(&file, 3);
    if (temp == "TRN") {
        int numOffsets = readInt(&file);
        trainOffsets.clear();
        for (int j = 0; j < numOffsets; ++j) {
            TrainOffset offset;
            int namelen = readInt(&file);
            std::string offsetName = readString(&file, namelen);
            strncpy(offset.name, offsetName.c_str(), sizeof(offset.name) - 1);
            offset.name[sizeof(offset.name) - 1] = '\0';
            offset.offset.x = readFloat(&file);
            offset.offset.y = readFloat(&file);
            offset.offset.z = readFloat(&file);
            offset.showNormal = readBool(&file);
            offset.showLateral = readBool(&file);
            offset.color.x = readFloat(&file);
            offset.color.y = readFloat(&file);
            offset.color.z = readFloat(&file);
            trainOffsets.push_back(offset);
        }
        temp = readString(&file, 3);
    }

    if (temp == "EOT") {
        if (file.peek() != EOF) {
            unsigned char marker = 0;
            file.read((char*)&marker, 1);
            if (marker == 0xFD) {
                int numExtrusions = readInt(&file);
                if (numExtrusions > 0)
                    customExtrusions.clear();
                for (int i = 0; i < numExtrusions; ++i) {
                    ParametricExtrusion ext;
                    ext.shape = (ExtrusionShape)readInt(&file);
                    ext.size.x = readFloat(&file);
                    ext.size.y = readFloat(&file);
                    ext.offset.x = readFloat(&file);
                    ext.offset.y = readFloat(&file);
                    customExtrusions.push_back(ext);
                }

                int numAssets = readInt(&file);
                customAssets.clear();
                for (int i = 0; i < numAssets; ++i) {
                    CustomAssetInstance asset;
                    int pathLen = readInt(&file);
                    asset.filepath = readString(&file, pathLen);
                    asset.startDist = readFloat(&file);
                    asset.endDist = readFloat(&file);
                    asset.interval = readFloat(&file);
                    asset.color.x = readFloat(&file);
                    asset.color.y = readFloat(&file);
                    asset.color.z = readFloat(&file);
                    asset.loadedModel = nullptr;

                    // Read flags if they exist (simple way to handle versioning in binary format)
                    if (file.peek() != EOF) {
                        readBytes(&file, (char*)&asset.fullLayout, sizeof(bool));
                        readBytes(&file, (char*)&asset.toEnd, sizeof(bool));
                    } else {
                        asset.fullLayout = (asset.startDist == 0.0f && asset.endDist < 0.0f);
                        asset.toEnd = (asset.endDist < 0.0f);
                    }
                    asset.visible = true;

                    customAssets.push_back(asset);
                }
            }
        }
    }
    return std::string("Load Successful");
}

mnode* track::getPoint(int index) {
    int i = 0;
    if (index < 0)
        index = 0;
    while (lSections.size() > i && index > lSections.at(i)->lNodes.size() - 1) {
        index -= lSections.at(i++)->lNodes.size() - 1;
    }
    if (lSections.size() == (size_t)i) {
        if (lSections.size())
            return &lSections.back()->lNodes.back();
        else
            return anchorNode;
    }
    return &lSections.at(i)->lNodes[index];
}

int track::getIndexFromDist(double dist) {
    int lower = 0;
    int upper = getNumPoints();
    mnode* point = getPoint(upper);
    double cur = point->fTotalLength;
    if (dist > cur) {
        return upper;
    } else if (dist < 0.0) {
        return 0;
    } else {
        while (lower + 1 < upper) {
            int mid = lower + (upper - lower) / 2;
            point = getPoint(mid);
            cur = point->fTotalLength;
            if (cur < dist) {
                lower = mid;
            } else {
                upper = mid;
            }
        }
        return lower;
    }
}

int track::getNumPoints(section* until) {

    int sum = 0;
    for (int i = 0; i < lSections.size(); ++i) {
        if (lSections.at(i) == until)
            return sum;
        sum += lSections.at(i)->lNodes.size() - 1;
    }
    return sum;
}

float track::getTotalLength() const {
    // We cannot call getNumPoints() or getPoint() here because they are not const.
    // However, we can calculate it easily.
    if (lSections.empty())
        return 0.0f;
    return (float)lSections.back()->lNodes.back().fTotalLength;
}

int track::getSectionNumber(section* _section) {
    int number = 0;
    while (number < lSections.size() && lSections.at(number) != _section)
        ++number;
    if (number < lSections.size()) {
        return number;
    } else {
        return -1;
    }
}

void track::getSecNode(int index, int* node, int* section) {
    int i = 0;
    while (lSections.size() > i && index > lSections.at(i)->lNodes.size() - 1) {
        index -= lSections.at(i++)->lNodes.size() - 1;
    }
    if (lSections.size() == (size_t)i) {
        if (lSections.size()) {
            *node = lSections.back()->lNodes.size() - 1;
            *section = lSections.size() - 1;
        } else {
            *node = 0;
            *section = -1;
        }
        return;
    }
    *node = index;
    *section = i;
    return;
}

void track::requestUpdateTrack(int index, int iNode) {
    if (!pendingRebuild || index < pendingRebuildIndex) {
        pendingRebuildIndex = index;
        pendingRebuildNode = iNode;
    }
    pendingRebuild = true;
}

void track::requestUpdateTrack(section* fromSection, int iNode) {
    int idx = getSectionNumber(fromSection);
    if (idx < 0)
        idx = 0;
    requestUpdateTrack(idx, iNode);
}

void track::processPendingUpdates() {
    if (pendingRebuild) {
        updateTrack(pendingRebuildIndex, pendingRebuildNode);
        pendingRebuild = false;
    }
}

bool track::exportParametricStyle(const std::string& filepath) {
    std::ofstream out(filepath);
    if (!out)
        return false;
    out << "FVD_STYLE_V2\n";
    for (const auto& ext : customExtrusions) {
        out << "EXTRUSION " << (int)ext.shape << " " << ext.size.x << " " << ext.size.y << " " << ext.offset.x << " " << ext.offset.y << "\n";
    }
    for (const auto& asset : customAssets) {
        std::string filename = std::filesystem::path(asset.filepath).filename().string();
        out << "ASSET \"" << filename << "\" " << asset.startDist << " " << (asset.toEnd ? -1.0f : asset.endDist) << " " << asset.interval << " "
            << asset.color.x << " " << asset.color.y << " " << asset.color.z << " "
            << (asset.fullLayout ? 1 : 0) << " " << (asset.toEnd ? 1 : 0) << " " << (asset.smoothAlongSpline ? 1 : 0) << "\n";
    }
    return true;
}

bool track::importParametricStyle(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in)
        return false;
    std::string version;
    in >> version;
    if (version != "FVD_STYLE_V1" && version != "FVD_STYLE_V2")
        return false;

    std::string baseDir = std::filesystem::path(filepath).parent_path().string();
    if (!baseDir.empty())
        baseDir += "/";

    customExtrusions.clear();
    for (auto& asset : customAssets) {
        if (asset.loadedModel)
            delete asset.loadedModel;
    }
    customAssets.clear();

    std::string line;
    std::getline(in, line); // consume remainder of version line
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        std::stringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "EXTRUSION") {
            ParametricExtrusion ext;
            int shape;
            ss >> shape >> ext.size.x >> ext.size.y >> ext.offset.x >> ext.offset.y;
            ext.shape = (ExtrusionShape)shape;
            customExtrusions.push_back(ext);
        } else if (tag == "ASSET") {
            std::string remaining;
            std::getline(ss, remaining);

            size_t firstQuote = remaining.find('\"');
            size_t lastQuote = remaining.find_last_of('\"');

            std::string filename;
            std::string params;

            if (firstQuote != std::string::npos && lastQuote != std::string::npos && firstQuote != lastQuote) {
                // Quoted filename format
                filename = remaining.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                params = remaining.substr(lastQuote + 1);
            } else {
                // Backward compatibility: assume the last 6 tokens are parameters
                std::vector<std::string> tokens;
                std::stringstream rss(remaining);
                std::string t;
                while (rss >> t)
                    tokens.push_back(t);

                if (tokens.size() >= 7) {
                    int numFloats = 6;
                    int filenameTokens = (int)tokens.size() - numFloats;
                    for (int i = 0; i < filenameTokens; ++i) {
                        filename += tokens[i];
                        if (i < filenameTokens - 1)
                            filename += " ";
                    }
                    // Reconstruct params string for the stringstream below
                    for (size_t i = tokens.size() - 6; i < tokens.size(); ++i) {
                        params += tokens[i] + " ";
                    }
                }
            }

            if (!filename.empty()) {
                CustomAssetInstance asset;
                std::stringstream pss(params);
                if (pss >> asset.startDist >> asset.endDist >> asset.interval >> asset.color.x >> asset.color.y >> asset.color.z) {
                    // Try to read optional flags
                    int fl = 0, te = 0, blend = 1;
                    if (pss >> fl >> te >> blend) {
                        asset.fullLayout = (fl != 0);
                        asset.toEnd = (te != 0);
                        asset.smoothAlongSpline = (blend != 0);
                    } else if (pss >> fl >> te) {
                        asset.fullLayout = (fl != 0);
                        asset.toEnd = (te != 0);
                        asset.smoothAlongSpline = true;
                    } else {
                        // Fallback for older .fvdstyle files
                        asset.fullLayout = (asset.startDist == 0.0f && asset.endDist < 0.0f);
                        asset.toEnd = (asset.endDist < 0.0f);
                        asset.smoothAlongSpline = true;
                    }
                    asset.visible = true;

                    asset.filepath = baseDir + filename;
                    asset.loadedModel = nullptr;
                    customAssets.push_back(asset);
                }
            }
        }
    }
    customStyleFile = filepath;
    requestUpdateTrack(0, 0);
    return true;
}

bool track::exportMeasurementPoints(const std::string& filepath) {
    std::ofstream out(filepath);
    if (!out)
        return false;
    out << "FVD_MEASUREMENTS_V1\n";
    for (const auto& o : trainOffsets) {
        out << "POINT \"" << o.name << "\" " << o.offset.x << " " << o.offset.y << " " << o.offset.z << " "
            << (o.showNormal ? 1 : 0) << " " << (o.showLateral ? 1 : 0) << " "
            << o.color.x << " " << o.color.y << " " << o.color.z << "\n";
    }
    return true;
}

bool track::importMeasurementPoints(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in)
        return false;
    std::string version;
    in >> version;
    if (version != "FVD_MEASUREMENTS_V1")
        return false;

    trainOffsets.clear();
    std::string line;
    std::getline(in, line); // consume newline
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        std::stringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "POINT") {
            std::string remaining;
            std::getline(ss, remaining);
            size_t firstQuote = remaining.find('\"');
            size_t lastQuote = remaining.find_last_of('\"');
            if (firstQuote != std::string::npos && lastQuote != std::string::npos && firstQuote != lastQuote) {
                TrainOffset o;
                std::string name = remaining.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                strncpy(o.name, name.c_str(), 63);
                o.name[63] = '\0';
                std::stringstream pss(remaining.substr(lastQuote + 1));
                int sn, sl;
                if (pss >> o.offset.x >> o.offset.y >> o.offset.z >> sn >> sl >> o.color.x >> o.color.y >> o.color.z) {
                    o.showNormal = (sn != 0);
                    o.showLateral = (sl != 0);
                    trainOffsets.push_back(o);
                }
            }
        }
    }
    hasChanged = true;
    graphChanged = true;
    return true;
}

void track::saveTrackChunk(std::ostream& file) {
    // 1. Write PROP chunk
    std::stringstream propStream;
    int namelen = name.length();
    writeBytes(&propStream, (const char*)&namelen, sizeof(int));
    propStream << name;
    writeVec3(&propStream, (glm::vec3)startPos);
    float fval = (float)anchorNode->fRoll;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)startPitch;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)startYaw;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)anchorNode->fVel;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)anchorNode->forceNormal;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)anchorNode->forceLateral;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)fHeart;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)fFriction;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    fval = (float)fResistance;
    writeBytes(&propStream, (const char*)&fval, sizeof(float));
    writeBytes(&propStream, (const char*)&drawHeartline, sizeof(int));
    writeBytes(&propStream, (const char*)&style, sizeof(int));
    writeBytes(&propStream, (const char*)&povPos.x, sizeof(float));
    writeBytes(&propStream, (const char*)&povPos.y, sizeof(float));

    std::string propData = propStream.str();
    writeChunkHeader(file, "PROP", 1, propData.length());
    file.write(propData.data(), propData.length());

    // 2. Write SECS chunk (Sequential section list)
    std::stringstream secsStream;
    int numSecs = lSections.size();
    writeBytes(&secsStream, (const char*)&numSecs, sizeof(int));
    for (int i = 0; i < numSecs; ++i) {
        std::stringstream secStream;
        lSections.at(i)->saveSection(secStream);
        std::string secData = secStream.str();

        // Use section tag (first 3 bytes + space)
        std::string tag = secData.substr(0, 3) + " ";
        std::string payload = secData.substr(3); // skip "STR"/"CUR" tag prefix

        writeChunkHeader(secsStream, tag.c_str(), 1, payload.length());
        secsStream.write(payload.data(), payload.length());
    }
    std::string secsData = secsStream.str();
    writeChunkHeader(file, "SECS", 1, secsData.length());
    file.write(secsData.data(), secsData.length());

    // 3. Write EXTR chunk
    std::stringstream extrStream;
    int numExtrusions = customExtrusions.size();
    writeBytes(&extrStream, (const char*)&numExtrusions, sizeof(int));
    for (const auto& ext : customExtrusions) {
        int shape = (int)ext.shape;
        writeBytes(&extrStream, (const char*)&shape, sizeof(int));
        writeBytes(&extrStream, (const char*)&ext.size.x, sizeof(float));
        writeBytes(&extrStream, (const char*)&ext.size.y, sizeof(float));
        writeBytes(&extrStream, (const char*)&ext.offset.x, sizeof(float));
        writeBytes(&extrStream, (const char*)&ext.offset.y, sizeof(float));
    }
    std::string extrData = extrStream.str();
    writeChunkHeader(file, "EXTR", 1, extrData.length());
    file.write(extrData.data(), extrData.length());

    // 4. Write ASST chunk
    std::stringstream asstStream;
    int numAssets = customAssets.size();
    writeBytes(&asstStream, (const char*)&numAssets, sizeof(int));
    for (const auto& asset : customAssets) {
        int pathLen = asset.filepath.length();
        writeBytes(&asstStream, (const char*)&pathLen, sizeof(int));
        asstStream << asset.filepath;
        writeBytes(&asstStream, (const char*)&asset.startDist, sizeof(float));
        writeBytes(&asstStream, (const char*)&asset.endDist, sizeof(float));
        writeBytes(&asstStream, (const char*)&asset.interval, sizeof(float));
        writeBytes(&asstStream, (const char*)&asset.color.x, sizeof(float));
        writeBytes(&asstStream, (const char*)&asset.color.y, sizeof(float));
        writeBytes(&asstStream, (const char*)&asset.color.z, sizeof(float));
        writeBytes(&asstStream, (const char*)&asset.fullLayout, sizeof(bool));
        writeBytes(&asstStream, (const char*)&asset.toEnd, sizeof(bool));
    }
    std::string asstData = asstStream.str();
    writeChunkHeader(file, "ASST", 1, asstData.length());
    file.write(asstData.data(), asstData.length());

    // 5. Write OFFS chunk
    std::stringstream offsStream;
    int numOffsets = trainOffsets.size();
    writeBytes(&offsStream, (const char*)&numOffsets, sizeof(int));
    for (const auto& offset : trainOffsets) {
        std::string offsetName(offset.name);
        int namelen = offsetName.length();
        writeBytes(&offsStream, (const char*)&namelen, sizeof(int));
        offsStream << offsetName;
        writeBytes(&offsStream, (const char*)&offset.offset.x, sizeof(float));
        writeBytes(&offsStream, (const char*)&offset.offset.y, sizeof(float));
        writeBytes(&offsStream, (const char*)&offset.offset.z, sizeof(float));
        writeBytes(&offsStream, (const char*)&offset.showNormal, sizeof(bool));
        writeBytes(&offsStream, (const char*)&offset.showLateral, sizeof(bool));
        writeBytes(&offsStream, (const char*)&offset.color.x, sizeof(float));
        writeBytes(&offsStream, (const char*)&offset.color.y, sizeof(float));
        writeBytes(&offsStream, (const char*)&offset.color.z, sizeof(float));
    }
    std::string offsData = offsStream.str();
    writeChunkHeader(file, "OFFS", 1, offsData.length());
    file.write(offsData.data(), offsData.length());
}

void track::loadTrackChunk(std::istream& file, uint8_t version, uint32_t length) {
    std::streampos endPos = file.tellg() + (std::streamoff)length;
    while (file.tellg() < endPos) {
        ChunkHeader header = readChunkHeader(file);
        std::string tag(header.tag, 4);
        std::streampos chunkEnd = file.tellg() + (std::streamoff)header.length;

        if (tag == "PROP") {
            loadPropChunk(file, header.version, header.length);
        } else if (tag == "SECS") {
            loadSecsChunk(file, header.version, header.length);
        } else if (tag == "EXTR") {
            loadExtrChunk(file, header.version, header.length);
        } else if (tag == "ASST") {
            loadAsstChunk(file, header.version, header.length);
        } else if (tag == "OFFS") {
            loadOffsChunk(file, header.version, header.length);
        } else {
            LOG_INFO("Skipping unknown sub-chunk of TRK: %s", tag.c_str());
        }
        file.seekg(chunkEnd, ios::beg);
    }
}

void track::loadPropChunk(std::istream& file, uint8_t version, uint32_t length) {
    int namelength = readInt(&file);
    name = readString(&file, namelength);
    startPos = readVec3(&file);
    anchorNode->fRoll = std::clamp((double)readFloat(&file), -1800.0, 1800.0);
    startPitch = std::clamp((double)readFloat(&file), -90.0, 90.0);
    startYaw = std::clamp((double)readFloat(&file), -1800.0, 1800.0);
    anchorNode->fVel = std::max(0.1, (double)readFloat(&file));
    anchorNode->forceNormal = std::clamp((double)readFloat(&file), -20.0, 20.0);
    anchorNode->forceLateral = std::clamp((double)readFloat(&file), -15.0, 15.0);
    fHeart = std::clamp((double)readFloat(&file), -10.0, 10.0);
    fFriction = std::max(0.0, (double)readFloat(&file));
    fResistance = std::max(0.0, (double)readFloat(&file));
    drawHeartline = readInt(&file);
    style = (enum trackStyle)readInt(&file);
    povPos.x = readFloat(&file);
    povPos.y = readFloat(&file);

    anchorNode->fEnergy = 0.5f * anchorNode->fVel * anchorNode->fVel +
                          F_G * anchorNode->fPosHearty(0.9 * fHeart);
    anchorNode->changePitch(startPitch, false);
    anchorNode->setRoll(anchorNode->fRoll);
    anchorNode->updateNorm();
}

void track::loadSecsChunk(std::istream& file, uint8_t version, uint32_t length) {
    std::streampos endPos = file.tellg() + (std::streamoff)length;
    int size = readInt(&file);

    // Clear existing sections first
    for (auto sec : lSections) {
        delete sec;
    }
    lSections.clear();

    // Clear smooth list and cleanly re-add the starting/anchor smooth handler at index 0
    for (auto smooth : smoothList) {
        delete smooth;
    }
    smoothList.clear();
    smoothList.push_back(new smoothHandler(this, -1));

    while (file.tellg() < endPos) {
        ChunkHeader header = readChunkHeader(file);
        std::string tag(header.tag, 4);
        std::streampos chunkEnd = file.tellg() + (std::streamoff)header.length;

        std::string payloadTag = tag.substr(0, 3);
        if (payloadTag == "STR") {
            newSection(straight, -1, true);
        } else if (payloadTag == "CUR") {
            newSection(curved, -1, true);
        } else if (payloadTag == "FRC") {
            newSection(forced, -1, true);
        } else if (payloadTag == "GEO") {
            newSection(geometric, -1, true);
        } else if (payloadTag == "BEZ") {
            newSection(bezier, -1, true);
        } else if (payloadTag == "NLC") {
            newSection(nolimitscsv, -1, true);
        } else if (payloadTag == "GRL") {
            newSection(geometricriderlocal, -1, true);
        } else {
            LOG_INFO("Unknown section chunk: %s", tag.c_str());
            file.seekg(chunkEnd, ios::beg);
            continue;
        }

        if (activeSection) {
            std::string payload(header.length, '\0');
            file.read(&payload[0], header.length);

            std::stringstream secStream(payload);
            activeSection->loadSection(secStream);
        }
        file.seekg(chunkEnd, ios::beg);
    }
}

void track::loadExtrChunk(std::istream& file, uint8_t version, uint32_t length) {
    customExtrusions.clear();
    int numExtrusions = readInt(&file);
    for (int i = 0; i < numExtrusions; ++i) {
        ParametricExtrusion ext;
        ext.shape = (ExtrusionShape)readInt(&file);
        ext.size.x = readFloat(&file);
        ext.size.y = readFloat(&file);
        ext.offset.x = readFloat(&file);
        ext.offset.y = readFloat(&file);
        customExtrusions.push_back(ext);
    }
}

void track::loadAsstChunk(std::istream& file, uint8_t version, uint32_t length) {
    for (auto& asset : customAssets) {
        if (asset.loadedModel)
            delete asset.loadedModel;
    }
    customAssets.clear();

    int numAssets = readInt(&file);
    for (int i = 0; i < numAssets; ++i) {
        CustomAssetInstance asset;
        int pathLen = readInt(&file);
        asset.filepath = readString(&file, pathLen);
        asset.startDist = readFloat(&file);
        asset.endDist = readFloat(&file);
        asset.interval = readFloat(&file);
        asset.color.x = readFloat(&file);
        asset.color.y = readFloat(&file);
        asset.color.z = readFloat(&file);
        asset.fullLayout = readBool(&file);
        asset.toEnd = readBool(&file);
        asset.visible = true;
        asset.loadedModel = nullptr;
        customAssets.push_back(asset);
    }
}

void track::loadOffsChunk(std::istream& file, uint8_t version, uint32_t length) {
    trainOffsets.clear();
    int numOffsets = readInt(&file);
    for (int i = 0; i < numOffsets; ++i) {
        TrainOffset o;
        int namelen = readInt(&file);
        std::string offsetName = readString(&file, namelen);
        strncpy(o.name, offsetName.c_str(), 63);
        o.name[63] = '\0';
        o.offset = readVec3(&file);
        o.showNormal = readBool(&file);
        o.showLateral = readBool(&file);
        o.color.x = readFloat(&file);
        o.color.y = readFloat(&file);
        o.color.z = readFloat(&file);
        trainOffsets.push_back(o);
    }
}

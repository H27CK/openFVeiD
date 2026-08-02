/*
#    Copyright (C) 2026 Veia <h27ck@proton.me>
*/

#include "secnlcsv.h"
#include "exportfuncs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <limits>
#include <glm/gtx/quaternion.hpp>

using namespace std;

secnlcsv::secnlcsv(track* getParent, mnode* first)
    : section(getParent, nolimitscsv, first) {
    skipPoints = 0;
    interpolation = 0;
}

void secnlcsv::applyFiltering() {
    csvNodes.clear();
    if (importedNodes.empty())
        return;

    for (size_t i = 0; i < importedNodes.size(); i += (1 + skipPoints)) {
        csvNodes.push_back(importedNodes[i]);
    }
    // Make sure the last node is always included to maintain track length if possible,
    // or just let it be. Let's just ensure we don't duplicate the last node.
    if (csvNodes.back().vPos != importedNodes.back().vPos) {
        csvNodes.push_back(importedNodes.back());
    }
}

int secnlcsv::updateSection(int node) {
    (void)node;

    initDistances();

    if (lNodes.size() > 1) {
        lNodes.erase(lNodes.begin() + 1, lNodes.end());
    }

    lNodes[0].updateNorm();

    if (!csvNodes.size())
        return 0;

    float velocity = parent->anchorNode->fVel;
    float nodeDist = velocity / F_HZ;

    int numNode = 0;

    float trackLength = csvNodes.back().fTotalLength;

    int totalNumOfNodes = (int)floor(trackLength / nodeDist);
    nodeDist = trackLength / totalNumOfNodes;

    length = 0.0f;

    for (int i = 0; i <= totalNumOfNodes; i++) {
        mnode node = getNodeAtDistance(i * nodeDist);

        if (numNode) {
            lNodes.push_back(lNodes.back());
        }

        mnode* currentNode = &lNodes[numNode];
        currentNode->vPos = node.vPos;
        currentNode->vDir = node.vDir;
        currentNode->vLat = node.vLat;
        currentNode->fVel = velocity;
        currentNode->fDistFromLast = 0.0f;

        currentNode->updateNorm();
        currentNode->fRoll =
            glm::degrees(atan2(currentNode->vLat.y, -currentNode->vNorm.y));
        currentNode->fRollSpeed = 0.0f;

        if (numNode) {
            mnode* lastNode = &lNodes[numNode - 1];

            currentNode->fRollSpeed = (currentNode->fRoll - lastNode->fRoll) * F_HZ;

            currentNode->fHeartDistFromLast =
                glm::distance(currentNode->vPos, lastNode->vPos);
            currentNode->fTotalHeartLength += currentNode->fHeartDistFromLast;

            currentNode->fDistFromLast =
                glm::distance(currentNode->vPosHeart(parent->fHeart),
                              lastNode->vPosHeart(parent->fHeart));
            currentNode->fTotalLength += currentNode->fDistFromLast;

            calcDirFromLast(numNode);
        }

        this->length += currentNode->fDistFromLast;

        numNode++;
    }
    this->isStalled = false;
    return 0;
}

void secnlcsv::initDistances() {
    float len = 0.0f;
    glm::vec3 lastPos(0.f);

    if (!csvNodes.empty()) {
        lastPos = csvNodes.front().vPos;
    }

    for (int i = 0; i < (int)csvNodes.size(); i++) {
        glm::vec3 pos = csvNodes[i].vPos;

        len += glm::distance(lastPos, pos);
        csvNodes[i].fTotalLength = len;

        lastPos = pos;
    }
}

mnode secnlcsv::getNodeAtDistance(float distance) {
    int left = 0;
    int right = csvNodes.size() - 1;
    float range;
    int pos = 0;

    if (csvNodes.empty())
        return mnode();

    while (distance >= csvNodes.at(left).fTotalLength &&
           distance <= csvNodes.at(right).fTotalLength) {
        range = csvNodes.at(right).fTotalLength - csvNodes.at(left).fTotalLength;
        if (range < 0.0001f)
            break;

        pos = left + (int)(((double)right - left) *
                           (distance - csvNodes.at(left).fTotalLength) / range);

        if (distance > csvNodes.at(pos).fTotalLength)
            left = pos + 1;
        else if (distance < csvNodes.at(pos).fTotalLength)
            right = pos - 1;
        else
            break;

        if (left > right)
            break;
    }

    if (pos >= (int)csvNodes.size() - 1) {
        return csvNodes.back();
    }

    mnode currentNode = csvNodes.at(pos);
    mnode nextNode = csvNodes.at(pos + 1);

    double t = 0;
    double nodesDistanceDiff = nextNode.fTotalLength - currentNode.fTotalLength;
    double distanceDiff = distance - currentNode.fTotalLength;

    if (nodesDistanceDiff > std::numeric_limits<double>::epsilon()) {
        t = fmax(fmin(distanceDiff / nodesDistanceDiff, 1.0), 0.0);
    } else
        t = 0.5;

    mnode resultNode;

    if (interpolation == 1) { // Cubic Hermite
        mnode prevNode = pos > 0 ? csvNodes.at(pos - 1) : currentNode;
        mnode nextNextNode = pos + 2 < csvNodes.size() ? csvNodes.at(pos + 2) : nextNode;

        double t2 = t * t;
        double t3 = t2 * t;

        double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        double h10 = t3 - 2.0 * t2 + t;
        double h01 = -2.0 * t3 + 3.0 * t2;
        double h11 = t3 - t2;

        glm::dvec3 m0 = 0.5 * (nextNode.vPos - prevNode.vPos);
        glm::dvec3 m1 = 0.5 * (nextNextNode.vPos - currentNode.vPos);
        resultNode.vPos = h00 * currentNode.vPos + h10 * m0 + h01 * nextNode.vPos + h11 * m1;

        glm::dvec3 md0 = 0.5 * (nextNode.vDir - prevNode.vDir);
        glm::dvec3 md1 = 0.5 * (nextNextNode.vDir - currentNode.vDir);
        resultNode.vDir = glm::normalize(h00 * currentNode.vDir + h10 * md0 + h01 * nextNode.vDir + h11 * md1);

        glm::dvec3 ml0 = 0.5 * (nextNode.vLat - prevNode.vLat);
        glm::dvec3 ml1 = 0.5 * (nextNextNode.vLat - currentNode.vLat);
        resultNode.vLat = glm::normalize(h00 * currentNode.vLat + h10 * ml0 + h01 * nextNode.vLat + h11 * ml1);
    } else { // Linear
        resultNode.vPos = currentNode.vPos + ((nextNode.vPos - currentNode.vPos) * t);
        resultNode.vDir = currentNode.vDir + ((nextNode.vDir - currentNode.vDir) * t);
        resultNode.vLat = currentNode.vLat + ((nextNode.vLat - currentNode.vLat) * t);
    }

    return resultNode;
}

void secnlcsv::saveSection(std::ostream& file) {
    int size = importedNodes.size();

    file << "NLC";
    writeBytes(&file, (const char*)&size, sizeof(int));
    writeBytes(&file, (const char*)&skipPoints, sizeof(int));
    writeBytes(&file, (const char*)&interpolation, sizeof(int));

    for (int i = 0; i < (int)importedNodes.size(); i++) {
        glm::dvec3 tempPos = importedNodes[i].vPos;
        glm::dvec3 tempDir = importedNodes[i].vDir;
        glm::dvec3 tempLat = importedNodes[i].vLat;

        float fval;
        fval = (float)tempPos.x;
        writeBytes(&file, (const char*)&fval, sizeof(float));
        fval = (float)tempPos.y;
        writeBytes(&file, (const char*)&fval, sizeof(float));
        fval = (float)tempPos.z;
        writeBytes(&file, (const char*)&fval, sizeof(float));

        fval = (float)tempDir.x;
        writeBytes(&file, (const char*)&fval, sizeof(float));
        fval = (float)tempDir.y;
        writeBytes(&file, (const char*)&fval, sizeof(float));
        fval = (float)tempDir.z;
        writeBytes(&file, (const char*)&fval, sizeof(float));

        fval = (float)tempLat.x;
        writeBytes(&file, (const char*)&fval, sizeof(float));
        fval = (float)tempLat.y;
        writeBytes(&file, (const char*)&fval, sizeof(float));
        fval = (float)tempLat.z;
        writeBytes(&file, (const char*)&fval, sizeof(float));
    }
}

void secnlcsv::loadSection(std::istream& file) {
    importedNodes.clear();

    int size = readInt(&file);
    skipPoints = readInt(&file);
    interpolation = readInt(&file);

    for (int i = 0; i < size; i++) {
        mnode node;
        node.vPos = readVec3(&file);
        node.vDir = readVec3(&file);
        node.vLat = readVec3(&file);

        importedNodes.push_back(node);
    }
    applyFiltering();
}

double secnlcsv::getMaxArgument() {
    return 0.0;
}

bool secnlcsv::isLockable(func* _func) {
    (void)_func;
    return false;
}

bool secnlcsv::isInFunction(int index, subfunc* func) {
    (void)index;
    (void)func;
    return false;
}

void secnlcsv::loadTrack(std::string filename) {
    std::ifstream file(filename);

    importedNodes.clear();

    if (!file.is_open()) {
        return;
    }

    std::string line;
    int lineCount = 0;

    while (std::getline(file, line)) {
        if (lineCount > 0) {
            std::stringstream ss(line);
            std::string item;
            std::vector<std::string> lineSplitted;

            char delimiter = '\t';
            if (line.find(',') != std::string::npos)
                delimiter = ',';

            while (std::getline(ss, item, delimiter)) {
                lineSplitted.push_back(item);
            }

            if (lineSplitted.size() >= 10) {
                glm::dvec3 pos(std::stod(lineSplitted[1]), std::stod(lineSplitted[2]), std::stod(lineSplitted[3]));
                glm::dvec3 front(std::stod(lineSplitted[4]), std::stod(lineSplitted[5]), std::stod(lineSplitted[6]));
                glm::dvec3 left(-std::stod(lineSplitted[7]), -std::stod(lineSplitted[8]), -std::stod(lineSplitted[9]));

                mnode node;
                node.vPos = pos;
                node.vDir = front;
                node.vLat = left;
                importedNodes.push_back(node);
            }
        }
        lineCount++;
    }

    if (!importedNodes.empty()) {
        glm::dvec3 startPosFVD(0.0);
        glm::dquat rot(1.0, 0.0, 0.0, 0.0);
        bool snap = false;
        mnode* prevNode = nullptr;

        if (parent) {
            int secIdx = parent->getSectionNumber(this);
            if (secIdx > 0) {
                section* prevSec = parent->lSections[secIdx - 1];
                if (!prevSec->lNodes.empty()) {
                    prevNode = &prevSec->lNodes.back();
                    snap = true;
                }
            } else if (parent->anchorNode) {
                prevNode = parent->anchorNode;
                snap = true;
            }
        }

        if (snap && prevNode) {
            startPosFVD = prevNode->vPos;
            glm::dvec3 vFVD = prevNode->vDir;
            glm::dvec3 lFVD = prevNode->vLat;
            glm::dvec3 nFVD = prevNode->vNorm;

            glm::dvec3 vNL = glm::normalize(importedNodes.front().vDir);
            glm::dvec3 lNL = glm::normalize(importedNodes.front().vLat);
            glm::dvec3 nNL = glm::normalize(glm::cross(vNL, lNL));
            lNL = glm::cross(nNL, vNL);

            glm::dvec3 nNL_proj = nNL - glm::dot(nNL, vNL) * vNL;
            if (glm::length(nNL_proj) > 1e-4)
                nNL = glm::normalize(nNL_proj);
            lNL = glm::cross(nNL, vNL);

            glm::dmat3 Mcsv0(vNL, lNL, nNL);
            glm::dmat3 Mprev(vFVD, lFVD, nFVD);
            glm::dmat3 rotMat = Mprev * glm::transpose(Mcsv0);
            rot = glm::toQuat(rotMat);
        }

        glm::dvec3 anchorNL = importedNodes.front().vPos;
        for (auto& node : importedNodes) {
            glm::dvec3 localPos = node.vPos - anchorNL;
            node.vPos = startPosFVD + rot * localPos;
            node.vDir = rot * node.vDir;
            node.vLat = rot * node.vLat;
            node.updateRoll();
        }
    }

    applyFiltering();
    parent->updateTrack(this, 0);
}

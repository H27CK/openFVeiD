#include "secstatic.h"
#include "track.h"
#include "dummies.h"
#include "exportfuncs.h"
#include <cmath>
#include <iostream>

secstatic::~secstatic() {
    delete normForce;
    delete latForce;
}

secstatic::secstatic(track* getParent, mnode* first)
    : section(getParent, static_spline, first) {
    sName = "Static Spline";
    bSpeed = false;
    fVel = 0.0; // 0.0 means Inherit Speed from start node
    fAccel = 0.0;
}

int secstatic::updateSection(int node) {
    (void)node;

    if (lNodes.size() > 1) {
        lNodes.erase(lNodes.begin() + 1, lNodes.end());
    }

    lNodes[0].updateNorm();

    if (staticNodes.empty())
        return 0;

    mnode* startNode = &lNodes[0];

    glm::dvec3 startPos = startNode->vPos;
    glm::dvec3 startDir = glm::normalize(startNode->vDir);
    glm::dvec3 startNorm = glm::normalize(glm::cross(startDir, startNode->vLat));
    glm::dvec3 startLat = glm::normalize(glm::cross(startNorm, startDir));

    double startRoll = startNode->fRoll;
    double startEnergy = startNode->fEnergy;
    double startVel = startNode->fVel;

    // Initialize first node energy if coasting is enabled
    if (bSpeed) {
        lNodes[0].fEnergy = 0.5 * lNodes[0].fVel * lNodes[0].fVel +
                            F_G * (lNodes[0].vPosHeart(parent->fHeart * 0.9f).y +
                                   lNodes[0].fTotalLength * parent->fFriction);
    }

    length = 0.0;
    int numNodes = 1;
    bool stalled = false;

    for (size_t i = 0; i < staticNodes.size(); ++i) {
        lNodes.push_back(lNodes.back());
        mnode* prevNode = &lNodes[numNodes - 1];
        mnode* curNode = &lNodes[numNodes];

        const mnode& localNode = staticNodes[i];

        curNode->vPos = startPos + startDir * localNode.vPos.x + startLat * localNode.vPos.y + startNorm * localNode.vPos.z;
        curNode->vDir = glm::normalize(startDir * localNode.vDir.x + startLat * localNode.vDir.y + startNorm * localNode.vDir.z);
        curNode->vLat = glm::normalize(startDir * localNode.vLat.x + startLat * localNode.vLat.y + startNorm * localNode.vLat.z);

        curNode->updateNorm();
        curNode->fRoll = startRoll + localNode.fRoll;
        while (curNode->fRoll < -180.0)
            curNode->fRoll += 360.0;
        while (curNode->fRoll > 180.0)
            curNode->fRoll -= 360.0;

        curNode->fRollSpeed = (curNode->fRoll - prevNode->fRoll) * F_HZ;

        curNode->fHeartDistFromLast = glm::distance(curNode->vPos, prevNode->vPos);
        curNode->fTotalHeartLength = prevNode->fTotalHeartLength + curNode->fHeartDistFromLast;

        curNode->fDistFromLast = glm::distance(curNode->vPosHeart(parent->fHeart),
                                               prevNode->vPosHeart(parent->fHeart));
        curNode->fTotalLength = prevNode->fTotalLength + curNode->fDistFromLast;

        // Apply motion profile physics and speed calculations
        if (bSpeed) {
            // Coasting (Physics Engine)
            curNode->fEnergy = prevNode->fEnergy - (prevNode->fVel * prevNode->fVel * prevNode->fVel /
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
            if (this->fVel == 0.0 && this->fAccel == 0.0) {
                // Inherit Speed
                curNode->fVel = startVel;
                curNode->fEnergy = startEnergy;
            } else if (this->fAccel == 0.0) {
                // Constant velocity
                curNode->fVel = this->fVel;
                curNode->fEnergy = 0.5 * curNode->fVel * curNode->fVel +
                                   F_G * (curNode->vPosHeart(parent->fHeart * 0.9f).y +
                                          curNode->fTotalLength * parent->fFriction);
            } else {
                // Constant acceleration
                double minSpeed = (double)gloParent->mOptions->stallSpeed;
                curNode->fVel = std::max(minSpeed, prevNode->fVel + this->fAccel / F_HZ);
                curNode->fEnergy = 0.5 * curNode->fVel * curNode->fVel +
                                   F_G * (curNode->vPosHeart(parent->fHeart * 0.9f).y +
                                          curNode->fTotalLength * parent->fFriction);
            }
        }

        calcDirFromLast(numNodes);

        length += curNode->fDistFromLast;
        numNodes++;
    }

    this->isStalled = stalled;
    return 0;
}

void secstatic::saveSection(std::ostream& file) {
    int size = staticNodes.size();
    file << "STA";
    writeBytes(&file, (const char*)&size, sizeof(int));

    for (int i = 0; i < size; ++i) {
        writeVec3(&file, staticNodes[i].vPos);
        writeVec3(&file, staticNodes[i].vDir);
        writeVec3(&file, staticNodes[i].vLat);
        float r = (float)staticNodes[i].fRoll;
        writeBytes(&file, (const char*)&r, sizeof(float));
    }

    writeBytes(&file, (const char*)&bSpeed, sizeof(bool));
    float tempVel = (float)fVel;
    float tempAccel = (float)fAccel;
    writeBytes(&file, (const char*)&tempVel, sizeof(float));
    writeBytes(&file, (const char*)&tempAccel, sizeof(float));
}

void secstatic::loadSection(std::istream& file) {
    staticNodes.clear();
    int size = readInt(&file);

    for (int i = 0; i < size; ++i) {
        mnode node;
        node.vPos = readVec3(&file);
        node.vDir = readVec3(&file);
        node.vLat = readVec3(&file);
        node.fRoll = (double)readFloat(&file);
        staticNodes.push_back(node);
    }

    if (file.peek() != EOF) {
        bSpeed = readBool(&file);
    } else {
        bSpeed = false;
    }

    if (file.peek() != EOF) {
        fVel = (double)readFloat(&file);
    } else {
        fVel = 0.0;
    }

    if (file.peek() != EOF) {
        fAccel = (double)readFloat(&file);
    } else {
        fAccel = 0.0;
    }
}

double secstatic::getMaxArgument() {
    return 0.0;
}

bool secstatic::isLockable(func* _func) {
    (void)_func;
    return false;
}

bool secstatic::isInFunction(int index, subfunc* func) {
    (void)index;
    (void)func;
    return false;
}

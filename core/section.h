#ifndef SECTION_H
#define SECTION_H

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

#include "function.h"
#include "mnode.h"
#include <vector>
#include <string>
#include <iostream>

#define EULER true
#define QUATERNION false

#define TIME false
#define DISTANCE true

class track;

enum secType {
    anchor,
    straight,
    curved,
    forced,
    geometric,
    static_spline,
    geometricriderlocal,
    nolimitscsv
};

struct ExportNode {
    mnode node;
    bool isBoundary;
};

class section {
public:
    section(track* getParent, enum secType _type, mnode* first);
    virtual ~section();
    double length;
    bool isRestricted = false;
    bool isStalled = false;
    virtual int updateSection(int node = 0) = 0;
    virtual int exportSection(std::ostream* file, mnode* anchor, double mPerNode,
                              double fHeart, glm::dvec3& vHeartLat, glm::dvec3& Norm,
                              double fThreshold);
    virtual void fillPointList(std::vector<glm::dvec4>& List, std::vector<glm::dvec3>& Normals,
                               mnode* anchor, double mPerNode, double fThreshold);
    virtual void iFillPointList(std::vector<int>& List, double mPerNode);
    virtual void fillNodeList(std::vector<mnode>& List, double mPerNode);
    void Split(std::vector<int>& List, int l, int r, double total, double min);
    virtual void fFillPointList(std::vector<int>& List, double mPerNode);
    virtual void fFillNodeList(std::vector<ExportNode>& List, double mPerNode);

    virtual void saveSection(std::ostream& file) = 0;
    virtual void loadSection(std::istream& file) = 0;

    virtual double getMaxArgument() = 0;
    virtual bool isLockable(func* _func) = 0;
    virtual bool isInFunction(int index, subfunc* func) = 0;
    mnode sampleAtDistance(double dist);
    double getSpeed();
    bool setLocked(eFunctype func, int _id, bool _active);
    void calcDirFromLast(int i);
    std::vector<mnode> lNodes;
    track* parent;
    func* rollFunc;

    enum secType type;

    bool bSpeed;
    double fVel;
    double fAccel;

    bool bOrientation;
    bool bArgument;

    // Straight Section Parameters
    double fHLength;

    // Curved Section Parameters
    double fAngle;
    double fRadius;
    double fDirection;
    double fLeadIn;
    double fLeadOut;

    // Forced/Geometric Section Parameters
    int iTime;
    func* normForce;
    func* latForce;
    std::string sName;

    // Support line parameters
    std::vector<glm::dvec3> supList;
};

#endif // SECTION_H

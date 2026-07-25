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

#include "subfunction.h"
#include "tinyexpr.h"
#include "function.h"
#include "mnode.h"
#include "section.h"
#include "track.h"

#include "exportfuncs.h"
#include <iostream>
#include <cmath>

using namespace std;

double interpolate(double t, double x1, double x2) {
    return t * x2 + x1 - t * x1;
}

double interpolate(double t, double x1, double x2, double x3) {
    double t1 = 1.0 - t;
    return t1 * t1 * x1 + 2.0 * t * t1 * x2 + t * t * x3;
}

double interpolate(double t, double x1, double x2, double x3, double x4) {
    double t1 = 1.0 - t;
    return t1 * t1 * t1 * x1 + 3.0 * t * t1 * t1 * x2 + 3.0 * t * t * t1 * x3 +
           t * t * t * x4;
}

subfunc::subfunc() {
    compiledExpr = nullptr;
    customVarX = 0.0;
}

subfunc::subfunc(double min, double max, double start, double diff,
                 func* getparent) {
    compiledExpr = nullptr;
    customVarX = 0.0;

    minArgument = min;
    maxArgument = max;

    centerArg = 0.0;
    tensionArg = 0.0;
    symArg = diff;

    startValue = start;
    parent = getparent;
    lenAssert(getparent != NULL);

    if (parent->type == funcNormal) {
        changeDegree(cubic);
    } else {
        changeDegree(quartic);
    }
    locked = false;
}

subfunc::~subfunc() {
    if (compiledExpr) {
        te_free(static_cast<te_expr*>(compiledExpr));
        compiledExpr = nullptr;
    }
}

bool subfunc::compileExpr() {
    if (compiledExpr) {
        te_free(static_cast<te_expr*>(compiledExpr));
        compiledExpr = nullptr;
    }
    compileError.clear();

    if (customExpr.empty()) {
        compileError = "Expression is empty";
        return false;
    }

    te_variable vars[] = {
        {"x", &customVarX, TE_VARIABLE, 0}};

    int err = 0;
    compiledExpr = te_compile(customExpr.c_str(), vars, 1, &err);
    if (!compiledExpr) {
        compileError = "Syntax error near index " + to_string(err);
        return false;
    }
    return true;
}

void subfunc::update(double min, double max, double diff) {
    minArgument = min;
    maxArgument = max;

    symArg = diff;

    this->parent->translateValues(this);
}

void subfunc::updateBez() {
    int i = 0;
    valueList.clear();
    double t = 0;
    double nextT = 0, gotT;
    while (i < 100) {
        nextT += 0.01;
        valueList.push_back(interpolate(t, 0.0, pointList[0].y, pointList[1].y, 1.0));
        gotT = interpolate(t, 0.0, pointList[0].x, pointList[1].x, 1.0);
        t += (nextT - gotT) /
             (3.0 * interpolate(t, pointList[0].x, pointList[1].x - pointList[0].x,
                                1.0 - pointList[1].x));
        ++i;
    }
    valueList.push_back(1.0);
}

void subfunc::changeDegree(enum eDegree newDegree) {
    degree = newDegree;

    switch (newDegree) {
    case linear:
        arg1 = 0.0;
        break;
    case quadratic:
        arg1 = 1.0; // 1.0 is "Blend in"
        break;
    case cubic:
        arg1 = 0.0;
        break;
    case quartic:
        arg1 = -10.0;
        break;
    case quintic:
        arg1 = 0.0;
        break;
    case sinusoidal:
        arg1 = 0.0;
        break;
    case plateau:
        arg1 = 1.0;
        break;
    case freeform:
        pointList.clear();
        bez_t b;
        b.x = 0.3;
        b.y = 0.0;
        pointList.push_back(b);
        b.x = 0.7;
        b.y = 1.0;
        pointList.push_back(b);
        updateBez();
        break;
    case tozero:
        centerArg = 0.0;
        tensionArg = 0.0;
        symArg = -startValue;
        break;
    case custom_math:
        customExpr = "sin(x * pi())";
        compileExpr();
        break;
    default:
        lenAssert(0 && "unknown degree");
    }
    return;
}

double subfunc::getValue(double x, bool skipUpdate) {
    if (locked && !skipUpdate) {
        parent->changeLength(parent->secParent->getMaxArgument() - minArgument,
                             parent->getSubfuncNumber(this));
    } else if (x > maxArgument + 1e-4) {
        std::cerr << "Function got parameter out of bounds: x = " << x << std::endl;
        x = maxArgument;
    } else if (x < minArgument - 1e-4) {
        std::cerr << "Function got parameter out of bounds: x = " << x << std::endl;
        x = minArgument;
    } else {
        // Clamp for minor precision errors
        if (x > maxArgument)
            x = maxArgument;
        if (x < minArgument)
            x = minArgument;
    }

    if (std::abs(maxArgument - minArgument) < 1e-7) {
        return startValue;
    }

    x = (x - minArgument) / (maxArgument - minArgument);

    x = applyCenter(x);
    x = applyTension(x);

    double root;
    double max;
    double a, b, c, d, e;
    mnode *curNode, *prevNode;

    track* inTrack;

    switch (degree) {
    case linear:
        return symArg * x + startValue;
    case quadratic:
        if (isSymmetric()) {
            x = 2.0 * x - 1.0;
            return symArg * (1.0 - x * x) + startValue;
        } else if (arg1 < 0.0) {
            return symArg * (1.0 - (1.0 - x) * (1.0 - x)) + startValue;
        } else {
            return symArg * x * x + startValue;
        }
    case cubic:
        return symArg * x * x * (3.0 + x * (-2.0)) + startValue;
    case quartic:
        if (!isSymmetric()) {
            return x * x *
                       (-(6.0 * symArg * arg1) / (1.0 - 2.0 * arg1) +
                        x * (symArg * (4.0 * arg1 + 4.0) / (1.0 - 2.0 * arg1) +
                             x * ((-3.0 * symArg / (1.0 - 2.0 * arg1))))) +
                   startValue;
        } else {
            return symArg * x * x * (16.0 + x * (-32.0 + x * 16.0)) + startValue;
        }
        break;
    case quintic:
        if (fabs(arg1) < 0.005) {
            return symArg * x * x * x * (10.0 + x * (-15.0 + x * 6.0)) + startValue;
        } else if (arg1 < 0.0) {
            root = -sqrt(9.0 + fabs(arg1 / 10.0) * (-16.0 + 16.0 * fabs(arg1 / 10.0)));
            max = 0.01728 + 0.00576 * root +
                  fabs(arg1 / 10.0) *
                      (-0.0288 - 0.00448 * root +
                       fabs(arg1 / 10.0) *
                           (0.0032 - 0.00576 * root +
                            fabs(arg1 / 10.0) *
                                (-0.0704 + 0.02048 * root +
                                 fabs(arg1 / 10.0) * (0.1024 - 0.01024 * root +
                                                      arg1 / 10.0 * 0.04096))));
            return symArg / max * x * x * (x - 1.0) * (x - 1.0) * (x + arg1 / 10.0) +
                   startValue;
        } else {
            root = sqrt(9.0 + arg1 / 10.0 * (-16.0 + 16.0 * arg1 / 10.0));
            max = 0.01728 + 0.00576 * root +
                  arg1 / 10.0 *
                      (-0.0288 - 0.00448 * root +
                       arg1 / 10.0 *
                           (0.0032 - 0.00576 * root +
                            arg1 / 10.0 *
                                (-0.0704 + 0.02048 * root +
                                 arg1 / 10.0 *
                                     (0.1024 - 0.01024 * root -
                                      arg1 / 10.0 * 0.04096))));
            return symArg / max * x * x * (x - 1.0) * (x - 1.0) * (x - arg1 / 10.0) +
                   startValue;
        }
        break;
    case sinusoidal:
        return 0.5 * symArg * (1.0 - cos(F_PI * x)) + startValue;
        break;
    case plateau:
        return symArg * (1.0 - exp(-arg1 * 20.0 * pow(sin(F_PI * x), 4.0))) + startValue;
        break;
    case freeform:
        root = (x * (valueList.size() - 2));
        max = floor(root) + 0.01;
        root = root - floor(root);
        if ((int)max == valueList.size() - 1) {
            return root * symArg * valueList[(int)max] + startValue;
        } else {
            return (1.0 - root) * symArg * valueList[(int)max] +
                   root * symArg * valueList[(int)(max + 1)] + startValue;
        }
        break;
    case tozero:
        inTrack = parent->secParent->parent;

        curNode = this->parent->secParent->parent->getPoint(
            inTrack->getNumPoints(parent->secParent) + minArgument * F_HZ - 0.5);
        prevNode = this->parent->secParent->parent->getPoint(
            inTrack->getNumPoints(parent->secParent) + minArgument * F_HZ - 1.5);
        if (this->parent->secParent->bOrientation == EULER) {
            d = (curNode->fRollSpeed +
                 glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                     curNode->fYawFromLast -
                 prevNode->fRollSpeed -
                 glm::dot(prevNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                     prevNode->fYawFromLast) *
                F_HZ;
            e = startValue;
        } else {
            d = (curNode->fRollSpeed +
                 glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                     curNode->fYawFromLast -
                 prevNode->fRollSpeed -
                 glm::dot(prevNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                     prevNode->fYawFromLast) *
                F_HZ;
            e = -glm::dot(curNode->vDir, glm::dvec3(0.0, -1.0, 0.0)) *
                curNode->fYawFromLast * F_HZ;
            e += startValue;
        }
        if (std::abs(maxArgument - minArgument) < 1e-7) {
            arg1 = 0.0;
        } else {
            arg1 = -curNode->fRoll / (maxArgument - minArgument);
        }
        a = -2.5 * (d + 6.0 * (e - 2.0 * arg1));
        b = 6.0 * d + 32.0 * e - 60.0 * arg1;
        c = -d * 4.5 - 18.0 * e + 30.0 * arg1;
        return x * (d + x * (c + x * (b + x * a))) + e;
    case custom_math:
        if (!compiledExpr) {
            compileExpr();
        }
        if (compiledExpr) {
            customVarX = x;
            return te_eval(static_cast<const te_expr*>(compiledExpr)) + startValue;
        }
        return startValue;
    default:
        std::cerr << "unknown degree" << std::endl;
    }
    return -1.0;
}

double subfunc::getMinValue() // relic, doesn't get used at all at this time
{
    return startValue < endValue() ? startValue : endValue();
}

double subfunc::getMaxValue() {
    return startValue > endValue() ? startValue : endValue();
}

void subfunc::translateValues(double newStart) {
    startValue = newStart;
    if (degree == tozero) {
        symArg = -startValue;
    }
}

bool subfunc::isSymmetric() {
    if (degree == quadratic && fabs(arg1) < 0.5)
        return true;
    if (degree == quartic && arg1 < 0)
        return true;
    if (degree == quintic && fabs(arg1) > 0.005)
        return true;
    if (degree == plateau)
        return true;
    return false;
}

void subfunc::saveSubFunc(std::ostream& file) {
    writeBytes(&file, (const char*)&degree, sizeof(enum eDegree));
    float f;
    f = (float)minArgument;
    writeBytes(&file, (const char*)&f, sizeof(float));
    f = (float)maxArgument;
    writeBytes(&file, (const char*)&f, sizeof(float));
    f = (float)startValue;
    writeBytes(&file, (const char*)&f, sizeof(float));
    f = (float)arg1;
    writeBytes(&file, (const char*)&f, sizeof(float));
    f = (float)symArg;
    writeBytes(&file, (const char*)&f, sizeof(float));
    f = (float)centerArg;
    writeBytes(&file, (const char*)&f, sizeof(float));
    f = (float)tensionArg;
    writeBytes(&file, (const char*)&f, sizeof(float));
    writeBytes(&file, (const char*)&locked, sizeof(bool));
    if (degree == custom_math) {
        int len = (int)customExpr.length();
        writeBytes(&file, (const char*)&len, sizeof(int));
        file << customExpr;
    }
}

void subfunc::loadSubFunc(std::istream& file) {
    degree = (enum eDegree)readInt(&file);
    minArgument = readFloat(&file);
    maxArgument = readFloat(&file);

    // Ensure parent consistency (from old stringstream overload)
    if (parent) {
        parent->changeLength(maxArgument - minArgument,
                             parent->getSubfuncNumber(this));
    }

    startValue = readFloat(&file);
    arg1 = readFloat(&file);
    symArg = readFloat(&file);
    centerArg = readFloat(&file);
    tensionArg = readFloat(&file);
    locked = readBool(&file);
    if (degree == custom_math) {
        int len = readInt(&file);
        customExpr = readString(&file, len);
        compileExpr();
    } else {
        customExpr = "";
    }
}

double subfunc::applyTension(double x) {
    if (fabs(tensionArg) < 0.0005) {
        return x;
    } else if (tensionArg > 0.0) {
        x = 2.0 * tensionArg * (x - 0.5);
        x = sinh(x) / sinh(tensionArg);
        x = 0.5 * (x + 1.0);
    } else {
        x = 2.0 * sinh(tensionArg) * (x - 0.5);
        x = asinh(x) / tensionArg;
        x = 0.5 * (x + 1.0);
    }
    return x;
}

double subfunc::applyCenter(double x) {
    if (centerArg > 0.0) {
        x = pow(x, pow(2, centerArg / 2.0));
    } else if (centerArg < 0.0) {
        x = 1.0 - pow(1.0 - x, pow(2, -centerArg / 2.0));
    }
    return x;
}

double subfunc::endValue() {
    if (isSymmetric()) {
        return startValue;
    } else {
        return startValue + symArg;
    }
}

#ifndef SUBFUNCTION_H
#define SUBFUNCTION_H

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

#include <vector>
#include <fstream>

class func;

enum eDegree {
    linear = 0,
    quadratic = 1,
    cubic = 2,
    quartic = 3,
    quintic = 4,
    sinusoidal = 5,
    plateau = 6,
    tozero = 7,
    freeform = 8,
    custom_math = 9
};

typedef struct bez_s {
    double x;
    double y;
} bez_t;

class subfunc {
public:
    subfunc();
    subfunc(double min, double max, double start, double diff, func* getparent = 0);
    ~subfunc();
    void update(double min, double max, double diff);

    double getValue(double x, bool skipUpdate = false);

    bool compileExpr();

    void changeDegree(eDegree newDegree);
    void updateBez();

    double getMinValue();
    double getMaxValue();

    void translateValues(double newStart);

    bool isSymmetric();
    double endValue();

    void saveSubFunc(std::ostream& file);
    void loadSubFunc(std::istream& file);

    double minArgument;
    double maxArgument;

    double startValue;

    double arg1;
    double symArg;

    bool locked;

    // timewarp arguments
    double centerArg;
    double tensionArg;

    enum eDegree degree;

    func* parent;
    std::vector<bez_t> pointList;
    std::vector<double> valueList;

    // Custom math function members
    std::string customExpr;
    void* compiledExpr = nullptr;
    double customVarX = 0.0;
    std::string compileError;

private:
    double applyTension(double x);
    double applyCenter(double x);
};

#endif // SUBFUNCTION_H

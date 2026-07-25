#ifndef SECCURVED_H
#define SECCURVED_H

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
#include "track.h"
#include <vector>

class seccurved : public section {
public:
    seccurved(track* getParent, mnode* first, float getAngle, float getRadius);
    void changecurve(float newAngle, float newRadius, float newDirection);
    virtual int updateSection(int node = 0);
    virtual void saveSection(std::ostream& file);
    virtual void loadSection(std::istream& file);
    virtual double getMaxArgument();
    virtual bool isLockable(func* _func);
    virtual bool isInFunction(int index, subfunc* func);
    const std::vector<float>& getAngles() const {
        return lAngles;
    }

private:
    std::vector<float> lAngles;
};

#endif // SECCURVED_H

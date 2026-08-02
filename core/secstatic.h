#ifndef SECSTATIC_H
#define SECSTATIC_H

/*
#    FVD++, an advanced coaster design tool
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
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "section.h"
#include <vector>

class secstatic : public section {
public:
    ~secstatic();
    secstatic(track* getParent, mnode* first);
    virtual int updateSection(int node = 0);
    virtual void saveSection(std::ostream& file);
    virtual void loadSection(std::istream& file);
    virtual double getMaxArgument();
    virtual bool isLockable(func* _func);
    virtual bool isInFunction(int index, subfunc* func);

    std::vector<mnode> staticNodes; // Stored relative to the start node
};

#endif // SECSTATIC_H

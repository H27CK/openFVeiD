#ifndef EXPORTFUNCS_H
#define EXPORTFUNCS_H

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

#include "mnode.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>

void writeBytes(std::ostream* file, const char* data, size_t length);
void writeFloats(std::ostream* file, const float data);
void writeNulls(std::ostream* file, size_t length);
void writeVec3(std::ostream* file, const glm::vec3& vec);

std::string readString(std::istream* file, size_t length);
bool readNulls(std::istream* file, size_t length);
glm::vec3 readVec3(std::istream* file);
float readFloat(std::istream* file);
int readInt(std::istream* file);
bool readBool(std::istream* file);
void readBytes(std::istream* file, void* _ptr, size_t length);

#endif // EXPORTFUNCS_H

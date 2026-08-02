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

#include "exportfuncs.h"
#include <algorithm>

using namespace std;

void writeBytes(ostream* file, const char* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        file->write(data + length - 1 - i, 1);
    }
}

void writeFloats(ostream* file, const float data) {
    *file << std::fixed << std::setprecision(2) << data;
    *file << " ";
}

void writeNulls(ostream* file, size_t length) {
    char data[1] = {0x00};
    for (size_t i = 0; i < length; i++) {
        writeBytes(file, data, 1);
    }
}

void writeVec3(ostream* file, const glm::vec3& vec) {
    writeBytes(file, (const char*)&vec.x, sizeof(float));
    writeBytes(file, (const char*)&vec.y, sizeof(float));
    writeBytes(file, (const char*)&vec.z, sizeof(float));
}

string readString(istream* file, size_t length) {
    if (length > 10485760) { // Sanity bound: 10MB to prevent OOM on corrupted files
        std::cerr << "Warning: readString length (" << length << ") exceeded 10MB safety bound. Clamping to 10MB." << std::endl;
        length = 10485760;
    }
    string temp(length, '\0');
    file->read(&temp[0], length);
    return temp;
}

bool readNulls(istream* file, size_t length) {
    file->ignore(length);
    return true;
}

glm::vec3 readVec3(istream* file) {
    float x = readFloat(file);
    float y = readFloat(file);
    float z = readFloat(file);
    return glm::vec3(x, y, z);
}

float readFloat(istream* file) {
    union {
        char c[4];
        float f;
    } temp;
    file->read(temp.c, 4);
    std::swap(temp.c[0], temp.c[3]);
    std::swap(temp.c[1], temp.c[2]);
    return temp.f;
}

int readInt(istream* file) {
    union {
        char c[4];
        int i;
    } temp;
    file->read(temp.c, 4);
    std::swap(temp.c[0], temp.c[3]);
    std::swap(temp.c[1], temp.c[2]);
    return temp.i;
}

bool readBool(istream* file) {
    char temp;
    file->read(&temp, 1);
    return temp != 0;
}

void readBytes(istream* file, void* _ptr, size_t length) {
    char* ptr = (char*)_ptr;
    for (size_t i = 0; i < length; i++) {
        file->read(ptr + length - 1 - i, 1);
    }
}

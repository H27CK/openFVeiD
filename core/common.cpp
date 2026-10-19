/*
#    FVD++, an advanced coaster design tool for NoLimits
#    Copyright (C) 2012-2015, Stephan "Lenny" Alt <alt.stephan@web.de>
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

#include "common.h"
#include <filesystem>
#include <cctype>

namespace common {
std::string getResource(const char* file, bool fullpath) {
    // Simplified for now: just return the file path relative to current dir
    // Real implementation would use platform-specific executable path logic or std::filesystem
    return std::string(file);
}

std::string normalizeAssetPath(const std::string& path) {
    if (path.empty())
        return path;

    std::filesystem::path fsPath(path);
    return fsPath.make_preferred().string();
}

bool isPathFromOtherOS(const std::string& path) {
    if (path.empty())
        return false;

#ifdef _WIN32
    // On Windows, if it looks like an absolute Linux path (starts with '/' without a drive letter)
    if (path[0] == '/' && (path.length() < 2 || path[1] != ':')) {
        return true;
    }
#else
    // On Linux, if it contains backslashes or a Windows drive letter
    if (path.find('\\') != std::string::npos)
        return true;
    if (path.length() >= 2 && std::isalpha(path[0]) && path[1] == ':')
        return true;
#endif

    return false;
}
} // namespace common

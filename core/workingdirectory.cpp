/*
#    FVD++, an advanced coaster design tool
#    Copyright (C) 2026 Veia <h27ck@proton.me>
#    Copyright (C) 2026 Ercan Akyürek <ercan.akyuerek@gmail.com>
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

#include "workingdirectory.h"

#include <filesystem>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <cstdlib>

static std::filesystem::path bundleResourcesDirectory() {
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (!bundle)
        return {};
    CFURLRef resourcesUrl = CFBundleCopyResourcesDirectoryURL(bundle);
    if (!resourcesUrl)
        return {};
    char path[PATH_MAX];
    bool resolved = CFURLGetFileSystemRepresentation(resourcesUrl, true, (UInt8*)path, sizeof(path));
    CFRelease(resourcesUrl);
    return resolved ? std::filesystem::path(path) : std::filesystem::path();
}

static void initializeAppleWorkingDirectory() {
    const char* home = std::getenv("HOME");
    if (!home)
        return;

    std::error_code ignored;
    std::filesystem::path userDataDirectory = std::filesystem::path(home) / "Library/Application Support/FVD++";
    std::filesystem::create_directories(userDataDirectory, ignored);

    std::filesystem::path shippedTrackStyles = bundleResourcesDirectory() / "track_styles";
    if (!std::filesystem::exists(userDataDirectory / "track_styles", ignored) &&
        std::filesystem::exists(shippedTrackStyles, ignored)) {
        std::filesystem::copy(shippedTrackStyles, userDataDirectory / "track_styles",
                              std::filesystem::copy_options::recursive, ignored);
    }

    std::filesystem::current_path(userDataDirectory, ignored);
}
#endif

std::string platformResourcesDirectory() {
#ifdef __APPLE__
    return bundleResourcesDirectory().string();
#else
    return {};
#endif
}

void initializeWorkingDirectory() {
    std::error_code ignored;
    if (std::filesystem::exists("track_styles", ignored))
        return;
#ifdef __APPLE__
    initializeAppleWorkingDirectory();
#endif
}

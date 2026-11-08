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
#include "vfs.h"

#include <filesystem>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <cstdlib>

static std::filesystem::path bundleResourcesDirectory() {
    CFBundleRef bundle = CFBundleGetMainBundle();
    if (bundle) {
        CFURLRef resourcesUrl = CFBundleCopyResourcesDirectoryURL(bundle);
        if (resourcesUrl) {
            char path[PATH_MAX];
            bool resolved = CFURLGetFileSystemRepresentation(resourcesUrl, true, (UInt8*)path, sizeof(path));
            CFRelease(resourcesUrl);
            if (resolved)
                return std::filesystem::path(path);
        }
    }
    // Fallback: check relative to executable directory (Contents/MacOS/../Resources)
    std::error_code ec;
    std::filesystem::path exeDir = VFS::get().getExecutableDir();
    if (exeDir.filename() == "MacOS" && exeDir.parent_path().filename() == "Contents") {
        std::filesystem::path res = exeDir.parent_path() / "Resources";
        if (std::filesystem::exists(res, ec))
            return res;
    }
    return {};
}

static void initializeAppleWorkingDirectory() {
    const char* home = std::getenv("HOME");
    if (!home)
        return;

    std::error_code ignored;
    std::filesystem::path userDataDirectory = std::filesystem::path(home) / "Library/Application Support/FVD++";
    std::filesystem::create_directories(userDataDirectory, ignored);

    std::filesystem::path resDir = bundleResourcesDirectory();
    std::filesystem::path exeDir = VFS::get().getExecutableDir();

    // Shipped tracks
    std::filesystem::path shippedTracks = resDir / "tracks";
    if (!std::filesystem::exists(userDataDirectory / "tracks", ignored) &&
        std::filesystem::exists(shippedTracks, ignored)) {
        std::filesystem::copy(shippedTracks, userDataDirectory / "tracks",
                              std::filesystem::copy_options::recursive, ignored);
    }

    // Shipped templates
    std::filesystem::path shippedTemplates = resDir / "templates";
    if (!std::filesystem::exists(userDataDirectory / "templates", ignored) &&
        std::filesystem::exists(shippedTemplates, ignored)) {
        std::filesystem::copy(shippedTemplates, userDataDirectory / "templates",
                              std::filesystem::copy_options::recursive, ignored);
    }

    // Shipped skybox
    std::filesystem::path shippedSkybox = resDir / "skybox";
    if (!std::filesystem::exists(userDataDirectory / "skybox", ignored) &&
        std::filesystem::exists(shippedSkybox, ignored)) {
        std::filesystem::copy(shippedSkybox, userDataDirectory / "skybox",
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
#ifdef __APPLE__
    initializeAppleWorkingDirectory();
#else
    std::error_code ignored;
    std::filesystem::current_path(VFS::get().getExecutableDir(), ignored);
#endif
    VFS::get().init();
}

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

#ifndef VFS_H
#define VFS_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class VFS {
public:
    static VFS& get();

    void init();
    void refresh();

    std::vector<std::string> listFiles(const std::string& prefix, const std::vector<std::string>& extensions);

    bool exists(const std::string& virtualPath);
    bool readFileText(const std::string& virtualPath, std::string& outText);
    bool readFileBinary(const std::string& virtualPath, std::vector<uint8_t>& outBytes);

    const std::filesystem::path& getExecutableDir() const {
        return exeDir;
    }

private:
    VFS();
    ~VFS();

    VFS(const VFS&) = delete;
    VFS& operator=(const VFS&) = delete;

    std::filesystem::path exeDir;

    struct PackArchive;
    std::vector<std::unique_ptr<PackArchive>> mountedPacks;

    std::string normalizePath(const std::string& path) const;
};

#endif // VFS_H

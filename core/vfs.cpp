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

#include "vfs.h"
#include "ext/miniz.h"
#include "logger.h"
#include "workingdirectory.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#include <windows.h>
static std::filesystem::path findExecutableDir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
static std::filesystem::path findExecutableDir() {
    char buffer[1024];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        return std::filesystem::canonical(buffer).parent_path();
    }
    return std::filesystem::current_path();
}
#elif defined(__linux__)
static std::filesystem::path findExecutableDir() {
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return p.parent_path();
    }
    return std::filesystem::current_path();
}
#else
static std::filesystem::path findExecutableDir() {
    return std::filesystem::current_path();
}
#endif

struct VFS::PackArchive {
    std::string packPath;
    mz_zip_archive zip;
    bool valid = false;
    std::vector<std::string> rawFiles;
    std::unordered_map<std::string, mz_uint> fileMap; // normalized path -> index

    explicit PackArchive(const std::string& path)
        : packPath(path) {
        std::memset(&zip, 0, sizeof(zip));
        if (mz_zip_reader_init_file(&zip, packPath.c_str(), 0)) {
            valid = true;
            mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < numFiles; ++i) {
                mz_zip_archive_file_stat stat;
                if (mz_zip_reader_file_stat(&zip, i, &stat)) {
                    if (!stat.m_is_directory) {
                        std::string fn = stat.m_filename;
                        rawFiles.push_back(fn);
                        // Normalize key
                        std::string key = fn;
                        for (char& c : key) {
                            if (c == '\\')
                                c = '/';
                            c = (char)std::tolower((unsigned char)c);
                        }
                        if (!key.empty() && key[0] == '/')
                            key = key.substr(1);
                        fileMap[key] = i;
                    }
                }
            }
            LOG_INFO("VFS: Mounted pack '%s' (%u files)", packPath.c_str(), (unsigned int)rawFiles.size());
        } else {
            LOG_WARN("VFS: Failed to open pack archive '%s'", packPath.c_str());
        }
    }

    ~PackArchive() {
        if (valid) {
            mz_zip_reader_end(&zip);
        }
    }

    bool extractFile(const std::string& normalizedKey, std::vector<uint8_t>& outBytes) {
        if (!valid)
            return false;
        auto it = fileMap.find(normalizedKey);
        if (it == fileMap.end())
            return false;

        size_t uncompSize = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, it->second, &uncompSize, 0);
        if (!p)
            return false;

        outBytes.assign(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + uncompSize);
        mz_free(p);
        return true;
    }
};

VFS& VFS::get() {
    static VFS instance;
    return instance;
}

VFS::VFS() {
    init();
}

VFS::~VFS() = default;

void VFS::init() {
    exeDir = findExecutableDir();
    refresh();
}

static std::vector<std::filesystem::path> getSearchDirectories(const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> dirs;
    std::error_code ec;

    if (!exeDir.empty() && std::filesystem::exists(exeDir, ec)) {
        dirs.push_back(exeDir);
    }

    std::string resDirStr = platformResourcesDirectory();
    if (!resDirStr.empty()) {
        std::filesystem::path resDir(resDirStr);
        if (std::filesystem::exists(resDir, ec)) {
            bool duplicate = false;
            for (const auto& d : dirs) {
                if (std::filesystem::equivalent(d, resDir, ec)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                dirs.push_back(resDir);
            }
        }
    }

    std::filesystem::path curDir = std::filesystem::current_path(ec);
    if (!ec && std::filesystem::exists(curDir, ec)) {
        bool duplicate = false;
        for (const auto& d : dirs) {
            if (std::filesystem::equivalent(d, curDir, ec)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            dirs.push_back(curDir);
        }
    }

    return dirs;
}

void VFS::refresh() {
    mountedPacks.clear();

    std::vector<std::filesystem::path> searchDirs = getSearchDirectories(exeDir);
    std::unordered_set<std::string> mountedPackNames;
    std::error_code ec;

    for (const auto& dir : searchDirs) {
        if (!std::filesystem::exists(dir, ec))
            continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".fvdpack") {
                std::string filename = entry.path().filename().string();
                if (mountedPackNames.insert(filename).second) {
                    auto pack = std::make_unique<PackArchive>(entry.path().string());
                    if (pack->valid) {
                        mountedPacks.push_back(std::move(pack));
                    }
                }
            }
        }
    }
}

std::string VFS::normalizePath(const std::string& path) const {
    std::string s = path;
    for (char& c : s) {
        if (c == '\\')
            c = '/';
    }
    while (s.rfind("./", 0) == 0) {
        s = s.substr(2);
    }
    if (!s.empty() && s[0] == '/') {
        s = s.substr(1);
    }
    return s;
}

std::vector<std::string> VFS::listFiles(const std::string& prefix, const std::vector<std::string>& extensions) {
    std::vector<std::string> results;
    std::unordered_set<std::string> seenKeys;

    std::string normPrefix = normalizePath(prefix);
    if (!normPrefix.empty() && normPrefix.back() != '/') {
        normPrefix += '/';
    }

    std::vector<std::filesystem::path> searchDirs = getSearchDirectories(exeDir);
    std::error_code ec;

    // 1. Scan disk folders across search directories
    for (const auto& baseDir : searchDirs) {
        std::filesystem::path diskDir = baseDir / prefix;
        if (std::filesystem::exists(diskDir, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(diskDir, std::filesystem::directory_options::follow_directory_symlink, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::string ext = entry.path().extension().string();
                    bool matchesExt = false;
                    for (const auto& e : extensions) {
                        if (ext == e) {
                            matchesExt = true;
                            break;
                        }
                    }
                    if (matchesExt) {
                        std::filesystem::path rel = std::filesystem::relative(entry.path(), baseDir, ec);
                        std::string vpath = normalizePath(rel.string());
                        std::string key = vpath;
                        for (char& c : key)
                            c = (char)std::tolower((unsigned char)c);

                        if (seenKeys.insert(key).second) {
                            results.push_back(vpath);
                        }
                    }
                }
            }
        }
    }

    // 2. Scan mounted pack files
    for (const auto& pack : mountedPacks) {
        for (const auto& rawFn : pack->rawFiles) {
            std::string vpath = normalizePath(rawFn);
            if (!normPrefix.empty() && vpath.rfind(normPrefix, 0) != 0)
                continue;

            std::filesystem::path p(vpath);
            std::string ext = p.extension().string();
            bool matchesExt = false;
            for (const auto& e : extensions) {
                if (ext == e) {
                    matchesExt = true;
                    break;
                }
            }

            if (matchesExt) {
                std::string key = vpath;
                for (char& c : key)
                    c = (char)std::tolower((unsigned char)c);

                if (seenKeys.insert(key).second) {
                    results.push_back(vpath);
                }
            }
        }
    }

    std::sort(results.begin(), results.end());
    return results;
}

bool VFS::exists(const std::string& virtualPath) {
    std::string norm = normalizePath(virtualPath);
    std::error_code ec;

    // 1. Check disk directories
    std::vector<std::filesystem::path> searchDirs = getSearchDirectories(exeDir);
    for (const auto& baseDir : searchDirs) {
        if (std::filesystem::exists(baseDir / norm, ec))
            return true;
    }

    // 2. Check packs
    std::string key = norm;
    for (char& c : key)
        c = (char)std::tolower((unsigned char)c);
    for (const auto& pack : mountedPacks) {
        if (pack->fileMap.find(key) != pack->fileMap.end())
            return true;
    }

    return false;
}

bool VFS::readFileBinary(const std::string& virtualPath, std::vector<uint8_t>& outBytes) {
    std::error_code ec;

    // 0. Direct check for absolute or existing path on disk (e.g. user file dialog selections)
    std::filesystem::path directPath(virtualPath);
    if (std::filesystem::exists(directPath, ec) && !std::filesystem::is_directory(directPath, ec)) {
        std::ifstream f(directPath, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            outBytes.resize(static_cast<size_t>(size));
            if (f.read(reinterpret_cast<char*>(outBytes.data()), size)) {
                return true;
            }
        }
    }

    std::string norm = normalizePath(virtualPath);

    // 1. Try disk search directories
    std::vector<std::filesystem::path> searchDirs = getSearchDirectories(exeDir);
    for (const auto& baseDir : searchDirs) {
        std::filesystem::path diskPath = baseDir / norm;
        if (std::filesystem::exists(diskPath, ec) && !std::filesystem::is_directory(diskPath, ec)) {
            std::ifstream f(diskPath, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                std::streamsize size = f.tellg();
                f.seekg(0, std::ios::beg);
                outBytes.resize(static_cast<size_t>(size));
                if (f.read(reinterpret_cast<char*>(outBytes.data()), size)) {
                    return true;
                }
            }
        }
    }

    // 2. Try in-memory extraction from mounted packs
    std::string key = norm;
    for (char& c : key)
        c = (char)std::tolower((unsigned char)c);
    for (const auto& pack : mountedPacks) {
        if (pack->extractFile(key, outBytes)) {
            return true;
        }
    }

    return false;
}

bool VFS::readFileText(const std::string& virtualPath, std::string& outText) {
    std::vector<uint8_t> bytes;
    if (!readFileBinary(virtualPath, bytes))
        return false;

    outText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

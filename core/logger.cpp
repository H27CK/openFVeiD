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

#include "logger.h"
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <ctime>
#include <mutex>

namespace common {

static std::ofstream g_LogFile;
static std::mutex g_LogMutex;

void InitLogger(const std::string& filename) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    g_LogFile.open(filename, std::ios::out | std::ios::trunc);
    if (!g_LogFile.is_open()) {
        std::cerr << "Failed to open log file: " << filename << '\n';
    }
}

void Log(LogLevel level, const char* fmt, ...) {
    if (!g_LogFile.is_open())
        return;

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    const char* levelStr = "INFO";
    if (level == LogLevel::Debug)
        levelStr = "DEBUG";
    else if (level == LogLevel::Warn)
        levelStr = "WARN";
    else if (level == LogLevel::Error)
        levelStr = "ERROR";

    std::time_t now = std::time(nullptr);
    char timeBuffer[20];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        g_LogFile << "[" << timeBuffer << "] [" << levelStr << "] " << buffer << '\n';
    }
}

void FlushLog() {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    if (g_LogFile.is_open()) {
        g_LogFile.flush();
    }
}

} // namespace common

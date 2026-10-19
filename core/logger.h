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

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <cstdint>

namespace common {
enum class LogLevel : uint8_t { Debug,
                                Info,
                                Warn,
                                Error };

void InitLogger(const std::string& filename);
void Log(LogLevel level, const char* fmt, ...);
void FlushLog();
} // namespace common

#define LOG_DEBUG(...) common::Log(common::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) common::Log(common::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) common::Log(common::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) common::Log(common::LogLevel::Error, __VA_ARGS__)

#endif // LOGGER_H

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

#include "crashhandler.h"
#include "logger.h"
#include "portable-file-dialogs.h"
#include <iostream>
#include <fstream>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <climits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
#include <unistd.h>
#ifndef __APPLE__
#include <link.h>
#endif
#endif

namespace common {

#if !defined(_WIN32) && !defined(__APPLE__)
struct DlIterData {
    void* addr;
    uintptr_t base;
    const char* fname;
};

static int DlIterCallback(struct dl_phdr_info* info, size_t size, void* data) {
    DlIterData* d = (DlIterData*)data;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            uintptr_t vaddr = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
            if ((uintptr_t)d->addr >= vaddr && (uintptr_t)d->addr < vaddr + info->dlpi_phdr[i].p_memsz) {
                d->base = info->dlpi_addr;
                d->fname = info->dlpi_name;
                return 1;
            }
        }
    }
    return 0;
}
#endif

#ifdef _WIN32
static LONG WINAPI WindowsCrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    common::FlushLog();

    std::ofstream crashLog("fvd_crash.log");
    if (crashLog.is_open()) {
        HMODULE hModule = GetModuleHandle(NULL);
        crashLog << "FVD++ Crash Report (Windows)" << std::endl;
        crashLog << "Exception Code: 0x" << std::hex << pExceptionInfo->ExceptionRecord->ExceptionCode << std::dec << std::endl;
        crashLog << "Fault Address: 0x" << std::hex << (DWORD64)pExceptionInfo->ExceptionRecord->ExceptionAddress << std::dec << std::endl;
        crashLog << "Module Base: 0x" << std::hex << (DWORD64)hModule << std::dec << std::endl;

        if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            pExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR accessType = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR targetAddr = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];
            crashLog << "Access Violation: Attempted to "
                     << (accessType == 0 ? "read from" : (accessType == 1 ? "write to" : "execute"))
                     << " address 0x" << std::hex << targetAddr << std::dec << std::endl;
        }

        crashLog << "Stack Trace:" << std::endl;

        void* stack[64];
        unsigned short frames = CaptureStackBackTrace(0, 64, stack, NULL);

        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);

        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symbolBuffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        for (unsigned short i = 0; i < frames; i++) {
            DWORD64 displacement = 0;
            DWORD displacementLine = 0;
            crashLog << "[" << i << "] ";
            if (SymFromAddr(GetCurrentProcess(), (DWORD64)stack[i], &displacement, pSymbol)) {
                crashLog << pSymbol->Name;
                if (SymGetLineFromAddr64(GetCurrentProcess(), (DWORD64)stack[i], &displacementLine, &line)) {
                    crashLog << " at " << line.FileName << ":" << line.LineNumber;
                } else {
                    crashLog << " + 0x" << std::hex << displacement << std::dec;
                }
                crashLog << " (Address: " << stack[i] << ")" << std::endl;
            } else {
                DWORD64 rva = (DWORD64)stack[i] - (DWORD64)hModule;
                crashLog << "fvd.exe + 0x" << std::hex << rva << " (Address: " << stack[i] << ")" << std::dec << std::endl;
            }
        }
        crashLog.close();
    }

    pfd::message("FVD++ Fatal Error",
                 "FVD++ encountered a fatal error and has crashed.\n\nA crash log has been saved to fvd_crash.log.\nPlease include this file and fvd.log when reporting the issue.",
                 pfd::choice::ok, pfd::icon::error);

    return EXCEPTION_EXECUTE_HANDLER;
}
#else
static void LinuxCrashHandler(int sig) {
    common::FlushLog();

    std::ofstream crashLog("fvd_crash.log");
    if (crashLog.is_open()) {
#ifdef __APPLE__
        crashLog << "FVD++ Crash Report (macOS)" << std::endl;
#else
        crashLog << "FVD++ Crash Report (Linux)" << std::endl;
#endif
        crashLog << "Signal: " << sig << std::endl;
        crashLog << "Stack Trace (Resolved):" << std::endl;

        void* array[64];
        int size = backtrace(array, 64);
        char** strings = backtrace_symbols(array, size);

        if (strings) {
            for (int i = 0; i < size; i++) {
#ifdef __APPLE__
                crashLog << "[" << i << "] " << strings[i] << std::endl;
#else
                DlIterData d = {array[i], 0, nullptr};
                dl_iterate_phdr(DlIterCallback, &d);

                uintptr_t offset = (uintptr_t)array[i] - d.base;
                char realExePath[PATH_MAX];
                const char* exePath = "/proc/self/exe";
                if (d.fname && d.fname[0]) {
                    if (realpath(d.fname, realExePath)) {
                        exePath = realExePath;
                    } else {
                        exePath = d.fname;
                    }
                } else {
                    if (realpath("/proc/self/exe", realExePath)) {
                        exePath = realExePath;
                    }
                }

                char cmd[PATH_MAX + 128];
                snprintf(cmd, sizeof(cmd), "addr2line -e %s -fCi %lx 2>/dev/null", exePath, (unsigned long)offset);

                FILE* fp = popen(cmd, "r");
                if (fp) {
                    char func[256], line[256];
                    if (fgets(func, sizeof(func), fp) && fgets(line, sizeof(line), fp)) {
                        func[strcspn(func, "\r\n")] = 0;
                        line[strcspn(line, "\r\n")] = 0;

                        if (line[0] == '?' && line[1] == '?') {
                            crashLog << "[" << i << "] " << strings[i] << std::endl;
                        } else {
                            crashLog << "[" << i << "] " << func << " at " << line << " (" << array[i] << ")" << std::endl;
                        }
                    } else {
                        crashLog << "[" << i << "] " << strings[i] << std::endl;
                    }
                    pclose(fp);
                } else {
                    crashLog << "[" << i << "] " << strings[i] << std::endl;
                }
#endif
            }
            free(strings);
        }
        crashLog.close();
    }

    // Note: pfd::message in a signal handler is technically risky but usually works
    // because it forks a separate process (zenity/kdialog).
    pfd::message("FVD++ Fatal Error",
                 "FVD++ encountered a fatal error and has crashed.\n\nA crash log has been saved to fvd_crash.log.\nPlease include this file and fvd.log when reporting the issue.",
                 pfd::choice::ok, pfd::icon::error);

    std::exit(sig);
}
#endif

void InitCrashHandler() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(WindowsCrashHandler);
#else
    std::signal(SIGSEGV, LinuxCrashHandler);
    std::signal(SIGABRT, LinuxCrashHandler);
    std::signal(SIGFPE, LinuxCrashHandler);
    std::signal(SIGILL, LinuxCrashHandler);
#endif
}

} // namespace common

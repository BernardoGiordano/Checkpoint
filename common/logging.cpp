/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#include "logging.hpp"

#if defined(__3DS__)
#include "server.hpp"
#include <3ds.h>
#elif defined(__SWITCH__)
#include "server.hpp"
#include <switch.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {
    std::string applicationLogs;
    std::chrono::steady_clock::time_point startTime;
    std::string logFilePath;

    std::mutex logMutex;
    constexpr size_t LOG_BUFFER_SIZE = 8192;
    // Cap the in-memory session log so it can't grow unbounded over a long session
    // (it is copied wholesale on every /logs/memory hit). Keep a trailing window.
    constexpr size_t APPLICATION_LOGS_CAP = 256 * 1024;
    std::string logBuffer;
    FILE* logFile = nullptr;

    void flushLogBuffer()
    {
        if (logBuffer.empty()) {
            return;
        }
        if (logFile != NULL) {
            fprintf(logFile, "%s", logBuffer.c_str());
            fflush(logFile);
            logBuffer.clear();
        }
    }
}

void Logging::init()
{
    startTime = std::chrono::steady_clock::now();

    // Get current date for log filename
    auto now        = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time_t);

    char dateBuf[9];
    std::strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", now_tm);

#if defined(__3DS__)
    logFilePath = std::string("sdmc:/3ds/Checkpoint/logs/checkpoint_") + dateBuf + ".log";
#elif defined(__SWITCH__)
    logFilePath = std::string("/switch/Checkpoint/logs/checkpoint_") + dateBuf + ".log";
#else
    logFilePath = std::string("checkpoint_") + dateBuf + ".log";
#endif

    logBuffer.reserve(LOG_BUFFER_SIZE);

    std::string versionInfo = std::format("Checkpoint v{:d}.{:d}.{:d}-{:s}", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, GIT_REV);
    info(versionInfo);

#if defined(SERVER_HPP)
    Server::registerHandler("/logs/memory",
        [](const std::string& path, const std::string& requestData) -> Server::HttpResponse { return {200, "text/plain", getApplicationLogs()}; });

    Server::registerHandler("/logs/file", [](const std::string& path, const std::string& requestData) -> Server::HttpResponse {
        std::lock_guard<std::mutex> lock(logMutex);
        flushLogBuffer();

        // Read through the already-open append handle instead of a second fopen():
        // Switch's FS sysmodule refuses opening a file for read while it is open
        // for write, so a separate read handle returns null there and 404s. The
        // handle is opened "a+", so reads are non-destructive and writes still
        // append; we fflush + rewind, read, then seek back to end.
        if (logFile == nullptr) {
            return {404, "text/plain", "Log file not found"};
        }

        fflush(logFile);
        fseek(logFile, 0, SEEK_END);
        long fileSize = ftell(logFile);
        fseek(logFile, 0, SEEK_SET);

        std::string logData;
        if (fileSize > 0) {
            logData.resize(fileSize);
            size_t read = fread(logData.data(), 1, fileSize, logFile);
            logData.resize(read);
        }
        fseek(logFile, 0, SEEK_END);

        return {200, "text/plain", logData};
    });
#endif
}

std::string Logging::getApplicationLogs()
{
    std::lock_guard<std::mutex> lock(logMutex);
    return applicationLogs;
}

void Logging::info(const std::string& message)
{
    log(LogLevel::INFO, message);
}

void Logging::warning(const std::string& message)
{
    log(LogLevel::WARN, message);
}

void Logging::error(const std::string& message)
{
    log(LogLevel::ERROR, message);
}

void Logging::debug(const std::string& message)
{
    log(LogLevel::DEBUG, message);
}

void Logging::trace(const std::string& message)
{
    log(LogLevel::TRACE, message);
}

void Logging::log(LogLevel level, const std::string& message)
{
    std::stringstream ss;
    auto now        = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms     = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    ss << std::put_time(std::localtime(&now_time_t), "[%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << now_ms.count() << "]";

    switch (level) {
        case LogLevel::TRACE:
            ss << " TRACE - ";
            break;
        case LogLevel::DEBUG:
            ss << " DEBUG - ";
            break;
        case LogLevel::INFO:
            ss << "  INFO - ";
            break;
        case LogLevel::WARN:
            ss << "  WARN - ";
            break;
        case LogLevel::ERROR:
            ss << " ERROR - ";
            break;
    }

    std::string logEntry = ss.str() + message + "\n";

    std::lock_guard<std::mutex> lock(logMutex);
    applicationLogs += logEntry;
    if (applicationLogs.size() > APPLICATION_LOGS_CAP) {
        // Drop the oldest data, trimming to the next line boundary so the window
        // never starts mid-entry.
        size_t drop = applicationLogs.size() - APPLICATION_LOGS_CAP;
        size_t nl   = applicationLogs.find('\n', drop);
        applicationLogs.erase(0, nl == std::string::npos ? drop : nl + 1);
    }
    logBuffer += logEntry;

    // Flush if buffer is getting full or for important messages
    if (logFile != nullptr) {
        if (logBuffer.size() >= LOG_BUFFER_SIZE || level == LogLevel::ERROR || level == LogLevel::WARN) {
            flushLogBuffer();
        }
    }
}

void Logging::initFileLogging()
{
    logFile = fopen(logFilePath.c_str(), "a+");
}

void Logging::exit()
{
    std::lock_guard<std::mutex> lock(logMutex);
    flushLogBuffer();
    if (logFile != nullptr) {
        fclose(logFile);
        logFile = nullptr;
    }
}

namespace {
    std::terminate_handler prevTerminate = nullptr;

    [[noreturn]] void terminateHandler()
    {
        // Guard against re-entry: if formatting/logging below throws (e.g. the
        // terminate was an OOM and std::format allocates again), don't recurse
        // into ourselves — bail straight to abort so Atmosphere still reports.
        static std::atomic<bool> handling{false};
        if (handling.exchange(true)) {
            std::abort();
        }

        std::string what = "no active C++ exception (abort / noexcept / memory fault)";
        if (std::exception_ptr ex = std::current_exception()) {
            try {
                std::rethrow_exception(ex);
            }
            catch (const std::exception& e) {
                what = std::string("std::exception: ") + e.what();
            }
            catch (...) {
                what = "non-standard exception";
            }
        }

        try {
            Logging::error("[FATAL] std::terminate: {}", what);
            Logging::exit(); // flush + close so the breadcrumb survives the crash
        }
        catch (...) {
        }

        // Hand back to the default handler so Atmosphere's creport still fires
        // with the faulting PC (feed that to tools/nx-crash-bt.py).
        if (prevTerminate) {
            prevTerminate();
        }
        std::abort();
    }
}

void Logging::installCrashHandlers()
{
    prevTerminate = std::set_terminate(terminateHandler);
}
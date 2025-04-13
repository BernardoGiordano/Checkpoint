/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2025 Bernardo Giordano FlagBrew
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
#include <switch.h>
#endif

#include <chrono>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {
    std::string applicationLogs;
    std::chrono::steady_clock::time_point startTime;
    std::string logFilePath;

    std::mutex logMutex;
    constexpr size_t LOG_BUFFER_SIZE = 8192;
    std::string logBuffer;
    FILE* logFile = nullptr;

    void flushLogBuffer()
    {
        if (logBuffer.empty()) {
            return;
        }
        if (logFile != NULL) {
            fprintf(logFile, logBuffer.c_str());
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
        [](const std::string& path, const std::string& requestData) -> Server::HttpResponse { return {200, "text/plain", applicationLogs}; });

    Server::registerHandler("/logs/file", [](const std::string& path, const std::string& requestData) -> Server::HttpResponse {
        std::lock_guard<std::mutex> lock(logMutex);
        flushLogBuffer();

        FILE* readFile = fopen(logFilePath.c_str(), "r");
        if (readFile == nullptr) {
            return {404, "text/plain", "Log file not found"};
        }

        fseek(readFile, 0, SEEK_END);
        long fileSize = ftell(readFile);
        fseek(readFile, 0, SEEK_SET);

        std::string logData;
        logData.resize(fileSize);
        fread(logData.data(), 1, fileSize, readFile);
        fclose(readFile);

        return {200, "text/plain", logData};
    });
#endif
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
    logFile = fopen(logFilePath.c_str(), "a");
}

void Logging::exit()
{
    std::lock_guard<std::mutex> lock(logMutex);
    flushLogBuffer();
    if (logFile != nullptr) {
        fclose(logFile);
    }
}
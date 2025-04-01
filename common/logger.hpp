/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "common.hpp"
#include <chrono>
#include <stdio.h>
#include <string>

class Logger {
public:
    static Logger& getInstance(void)
    {
        static Logger mLogger;
        return mLogger;
    }

    inline static const std::string INFO  = "[ INFO]";
    inline static const std::string DEBUG = "[DEBUG]";
    inline static const std::string ERROR = "[ERROR]";
    inline static const std::string WARN  = "[ WARN]";

    template <typename... Args>
    void log(const std::string& level, const std::string& format = {}, Args... args)
    {
        buffer += StringUtils::format(("[" + DateTime::logDateTime() + "] " + level + " " + format + "\n").c_str(), args...);
    }

    void flush(void)
    {
        mFile = fopen(mPath.c_str(), "a");
        if (mFile != NULL) {
            fprintf(mFile, buffer.c_str());
            fprintf(stderr, buffer.c_str());
            fclose(mFile);
        }
    }

private:
    Logger(void)
    {
        buffer = "";

        // Get current date for log filename
        auto now        = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* now_tm = std::localtime(&now_time_t);
        char dateBuf[9];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", now_tm);

#if defined(__3DS__)
        mPath = std::string("sdmc:/3ds/Checkpoint/logs/checkpoint_") + dateBuf + ".log";
#elif defined(__SWITCH__)
        mPath = std::string("/switch/Checkpoint/logs/checkpoint_") + dateBuf + ".log";
#else
        mPath = std::string("checkpoint_") + dateBuf + ".log";
#endif
    }
    ~Logger(void) {}

    Logger(Logger const&)         = delete;
    void operator=(Logger const&) = delete;

    std::string mPath;
    FILE* mFile;
    std::string buffer;
};

#endif
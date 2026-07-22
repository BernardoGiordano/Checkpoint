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

#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <cstdio>
#include <format>
#include <string>

enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR };

namespace Logging {
    void init(void);
    void initFileLogging(void);
    void exit(void);

    // Installs a std::terminate handler that, before the process dies, forces
    // the in-flight exception's what() to disk and flushes the log. Any uncaught
    // throw anywhere (a main-thread overlay, a libnx callback — not only the
    // script worker's try/catch) then leaves a breadcrumb instead of an
    // information-free abort. Chains to the previous handler / std::abort so
    // Atmosphere still writes its crash report (which carries the faulting PC).
    // Call once, as early as file logging is up.
    void installCrashHandlers(void);

    void log(LogLevel level, const std::string& message);

    // A copy of the accumulated in-memory log (every formatted line since
    // startup), taken under the log mutex. Backs the Switch settings log viewer
    // and the /logs/memory HTTP endpoint.
    std::string getApplicationLogs(void);

    void trace(const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);

    template <typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args)
    {
        trace(std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        debug(std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args)
    {
        info(std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        warning(std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        error(std::format(fmt, std::forward<Args>(args)...));
    }
}

#endif
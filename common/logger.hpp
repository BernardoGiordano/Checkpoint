/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#include <cstdio>
#include <string>

#include "datetime.hpp"
#include "stringutils.hpp"
#include "platform.hpp"

namespace Logger {
    inline static const std::string INFO  = "[ INFO]";
    inline static const std::string DEBUG = "[DEBUG]";
    inline static const std::string ERROR = "[ERROR]";
    inline static const std::string WARN  = "[ WARN]";

    void appendToBuffer(const std::string& data);
    void flush();

    template <typename... Args>
    void log(const std::string& level, const std::string& format = {}, Args... args)
    {
        const auto& d = StringUtils::format(("[" + DateTime::logDateTime() + "] " + level + " " + format + "\n").c_str(), args...);
        appendToBuffer(d);
    }
}

#endif

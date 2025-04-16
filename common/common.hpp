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

#ifndef COMMON_HPP
#define COMMON_HPP

#include <algorithm>
#include <arpa/inet.h>
#include <codecvt>
#include <cstdio>
#include <locale>
#include <memory>
#include <netinet/in.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define ATEXIT(func) atexit((void (*)())func)

namespace DateTime {
    std::string timeStr(void);
    std::string dateTimeStr(void);
    std::string logDateTime(void);
}

namespace StringUtils {
    bool containsInvalidChar(const std::string& str);
    std::string escapeJson(const std::string& s);
    std::string format(const std::string fmt_str, ...);
    std::string removeForbiddenCharacters(std::string src);
    std::string UTF16toUTF8(const std::u16string& src);
    void ltrim(std::string& s);
    void rtrim(std::string& s);
    void trim(std::string& s);
}

char* getConsoleIP(void);

#endif
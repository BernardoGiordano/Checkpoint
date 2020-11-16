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

#include <locale>
#include <codecvt>
#include <algorithm>
#include <cstdarg>

#include "stringutils.hpp"

std::string StringUtils::UTF16toUTF8(const std::u16string& src)
{
    static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::string dst = convert.to_bytes(src);
    while(dst.size() && dst.back() == '\0') {
        dst.pop_back();
    }
    return dst;
}

std::string StringUtils::removeForbiddenCharacters(std::string src)
{
    static const std::string illegalChars = ".,!\\/:?*\"<>|";
    for (auto& c : src) {
        if (illegalChars.find(c) != std::string::npos){
            c = ' ';
        }
    }

    // remove all trailing whitespace
    size_t last_space_pos = src.find_last_not_of(' ');
    if (last_space_pos == std::string::npos) // only spaces
        return "";
    else
        src.erase(last_space_pos + 1);

    return src;
}

struct AllocatedCharArrayDeleter {
    void operator()(char* arr)
    {
        free(arr);
    }
};

std::string StringUtils::format(const std::string fmt_str, ...)
{
    va_list ap;
    char* fp = NULL;
    va_start(ap, fmt_str);
    vasprintf(&fp, fmt_str.c_str(), ap);
    va_end(ap);
    std::unique_ptr<char, AllocatedCharArrayDeleter> formatted(fp);
    return std::string(formatted.get());
}

bool StringUtils::containsInvalidChar(const std::string& str)
{
    for (const auto c : str) {
        if (!isascii(c)) {
            return true;
        }
    }
    return false;
}

void StringUtils::ltrim(std::string& s)
{
    s.erase(s.begin(), std::find_if (s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
}

void StringUtils::rtrim(std::string& s)
{
    s.erase(std::find_if (s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
}

void StringUtils::trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}
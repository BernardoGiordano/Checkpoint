/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#include "common.hpp"

std::string DateTime::timeStr(void)
{
    time_t unixTime = time(NULL);
    struct tm* timeStruct = gmtime((const time_t*)&unixTime);
    return StringUtils::format("%02i:%02i:%02i", timeStruct->tm_hour, timeStruct->tm_min, timeStruct->tm_sec);
}

std::string DateTime::dateTimeStr(void)
{
    time_t unixTime = time(NULL);
    struct tm* timeStruct = gmtime((const time_t*)&unixTime);
    return StringUtils::format("%04i%02i%02i-%02i%02i%02i", timeStruct->tm_year + 1900, timeStruct->tm_mon + 1, timeStruct->tm_mday, timeStruct->tm_hour, timeStruct->tm_min, timeStruct->tm_sec);
}

std::string StringUtils::UTF16toUTF8(const std::u16string& src)
{
    static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> convert;
    std::string dst = convert.to_bytes(src);
    return dst;
}

std::string StringUtils::removeForbiddenCharacters(std::string src)
{
    static const std::string illegalChars = ".,!\\/:?*\"<>|";
    for (size_t i = 0, sz = src.length(); i < sz; i++)
    {
        if (illegalChars.find(src[i]) != std::string::npos)
        {
            src[i] = ' ';
        }
    }
    
    size_t i;
    for (i = src.length() - 1; i > 0 && src[i] == ' '; i--);
    src.erase(i + 1, src.length() - i);

    return src;
}

std::string StringUtils::format(const std::string fmt_str, ...)
{
    va_list ap;
    char *fp = NULL;
    va_start(ap, fmt_str);
    vasprintf(&fp, fmt_str.c_str(), ap);
    va_end(ap);
    std::unique_ptr<char[]> formatted(fp);
    return std::string(formatted.get());
}

std::string StringUtils::sizeString(double size)
{
    int i = 0;
    static const char* units[] = {"B", "kB", "MB", "GB"};
    while (size > 1024)
    {
        size /= 1024;
        i++;
    }
    return StringUtils::format("%.*f %s", i, size, units[i]);
}

bool StringUtils::containsInvalidChar(const std::string& str)
{
    for (size_t i = 0, sz = str.length(); i < sz; i++)
    {
        if (!isascii(str[i]))
        {
            return true;
        }
    }
    return false;
}

void StringUtils::ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

void StringUtils::rtrim(std::string &s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

void StringUtils::trim(std::string &s)
{
    ltrim(s);
    rtrim(s);
}
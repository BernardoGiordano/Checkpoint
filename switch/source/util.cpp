/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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

#include "util.hpp"

void servicesExit(void)
{
    // debug
    // socketExit();
    nsExit();
    Account::exit();
    hbkbd::exit();
    Gui::exit();
}

Result servicesInit(void)
{
    Result res = 0;
    res = io::createDirectory("sdmc:/switch");
    res = io::createDirectory("sdmc:/switch/Checkpoint");
    res = io::createDirectory("sdmc:/switch/Checkpoint/saves");
    if (R_FAILED(res))
    {
        return res;
    }

    if (R_FAILED(res = Account::init()))
    {
        return res;
    }

    if (R_FAILED(res = nsInitialize()))
    {
        return res;
    }  
    
    Gui::init();
    hbkbd::init();

    // debug
    // static int sock = -1;
    // struct sockaddr_in srv_addr;
    // int ret = socketInitializeDefault();
    // if (ret != 0) {
    //     Gui::createError(ret, "socketInitializeDefault");
    // } else {
	//     sock = socket(AF_INET, SOCK_STREAM, 0);
	//     if (!sock) {
    //         Gui::createError(-3, "socket");
    // }

	//     bzero(&srv_addr, sizeof srv_addr);
    //     srv_addr.sin_family=AF_INET;
    //     srv_addr.sin_addr = __nxlink_host;
    //     srv_addr.sin_port=htons(NXLINK_CLIENT_PORT);

	//     ret = connect(sock, (struct sockaddr *) &srv_addr, sizeof(srv_addr));
    // 	   if (ret != 0) {
    //         Gui::createError(ret, "connect");
    // 	   } else {
	// 	       fflush(stdout);
	// 	       dup2(sock, STDOUT_FILENO);
    // 	   }
    // }

    return 0;
}

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

std::string StringUtils::removeForbiddenCharacters(std::string src)
{
    static const std::string illegalChars = ".,!\\/:?*\"<>|";
    for (size_t i = 0; i < src.length(); i++)
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

// https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
std::string StringUtils::format(const std::string fmt_str, ...)
{
    int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
    std::unique_ptr<char[]> formatted;
    va_list ap;
    while(1)
    {
        formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
        strcpy(&formatted[0], fmt_str.c_str());
        va_start(ap, fmt_str);
        final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
        va_end(ap);
        if (final_n < 0 || final_n >= n)
            n += abs(final_n - n + 1);
        else
            break;
    }
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

size_t StringUtils::lines(const std::string& str)
{
    size_t n = 1;
    for (size_t i = 0, sz = str.length(); i < sz; i++)
    {
        if (str[i] == '\n')
        {
            n++;
        }
    }
    return n;
}
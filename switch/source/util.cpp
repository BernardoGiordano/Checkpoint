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
    freeIcons();
    nsExit();
    Account::exit();
    Gui::exit();
    plExit();
}

Result servicesInit(void)
{
    Result res = 0;
    romfsInit();
    res = io::createDirectory("sdmc:/switch");
    res = io::createDirectory("sdmc:/switch/Checkpoint");
    res = io::createDirectory("sdmc:/switch/Checkpoint/saves");
    if (R_FAILED(res))
    {
        return res;
    }

    if (R_FAILED(res = plInitialize()))
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

    // debug
    // if (socketInitializeDefault() == 0)
    // {
    //     nxlinkStdio();
    // }

    Configuration::getInstance();

    return 0;
}

std::u16string StringUtils::UTF8toUTF16(const char* src)
{
    char16_t tmp[256] = {0};
    utf8_to_utf16((uint16_t *)tmp, (uint8_t *)src, 256);
    return std::u16string(tmp);
}

// https://stackoverflow.com/questions/14094621/change-all-accented-letters-to-normal-letters-in-c
std::string StringUtils::removeAccents(std::string str)
{
    std::u16string src = UTF8toUTF16(str.c_str());
    const std::u16string illegal = UTF8toUTF16("ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ");
    const std::u16string fixed   = UTF8toUTF16("AAAAAAECEEEEIIIIDNOOOOOx0UUUUYPsaaaaaaeceeeeiiiiOnooooo/0uuuuypy");
    
    for (size_t i = 0, sz = src.length(); i < sz; i++)
    {
        size_t index = illegal.find(src[i]);
        if (index != std::string::npos)
        {
            src[i] = fixed[index];
        }
    }

    return UTF16toUTF8(src);
}

std::string StringUtils::removeNotAscii(std::string str)
{
    for (size_t i = 0, sz = str.length(); i < sz; i++)
    {
        if (!isascii(str[i]))
        {
            str[i] = ' ';
        }
    }
    return str; 
}
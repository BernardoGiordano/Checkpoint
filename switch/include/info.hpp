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

#ifndef INFO_HPP
#define INFO_HPP

#include <switch.h>
#include <string>
#include "SDLHelper.hpp"
#include "util.hpp"

typedef enum {
    TYPE_INFO,
    TYPE_ERROR
} Info_t;

class Info
{
public:
    Info(void);
    ~Info(void) { };

    void draw(void);
    void init(std::string title, std::string message, int ttl, Info_t type);
    void init(Result res, std::string message, int ttl, Info_t type);

private:
    size_t      mw;
    size_t      mh;
    u32         mx;
    u32         my;
    int         mTTL;
    Result      mRes;
    Info_t      mType;
    std::string mTitle;
    std::string mMessage;
};

#endif

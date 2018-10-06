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

#ifndef ACCOUNT_HPP
#define ACCOUNT_HPP

#include <string>
#include <string.h>
#include <switch.h>
#include <map>
#include <vector>
#include "SDLHelper.hpp"
#include "types.hpp"

#define USER_ICON_SIZE 80

struct User {
    u128         id;
    std::string  name;
    SDL_Texture* icon; 
};

namespace Account 
{
    Result      init(void);
    void        exit(void);

    std::vector
    <u128>       ids(void);
    SDL_Texture* icon(u128 id);
    std::string  username(u128 id);
}

#endif
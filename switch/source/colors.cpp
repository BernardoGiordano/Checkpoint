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

#include "colors.hpp"

static const struct Theme defaultTheme = {
    COLOR_BLACK,
    COLOR_GREY_BG,
    COLOR_GREY_DARKER,
    COLOR_GREY_DARK,
    COLOR_GREY_MEDIUM,
    COLOR_GREY_LIGHT,
    COLOR_WHITE
};

static const struct Theme pkmsTheme = {
    {17, 38, 85, 255},
    COLOR_HIGHBLUE,
    COLOR_DARKBLUE,
    COLOR_MENUBLUE,
    COLOR_PALEBLUE,
    COLOR_LIGHTBLUE,
    COLOR_WHITE
};

static struct Theme currentTheme = defaultTheme;

void theme(int t)
{
    switch(t) {
        case THEME_PKSM: currentTheme = pkmsTheme; break;
        default: currentTheme = defaultTheme; break;
    }
}

struct Theme theme(void)
{
    return currentTheme;
}

struct Theme dtheme(void)
{
    return defaultTheme;
}
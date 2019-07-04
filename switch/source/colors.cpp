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

#include "colors.hpp"

const SDL_Color COLOR_WHITE     = FC_MakeColor(255, 255, 255, 255);
const SDL_Color COLOR_BLACK     = FC_MakeColor(0, 0, 0, 255);
const SDL_Color COLOR_BLUE      = FC_MakeColor(29, 50, 243, 255);
const SDL_Color COLOR_GREEN     = FC_MakeColor(0, 254, 197, 255);
const SDL_Color COLOR_RED       = FC_MakeColor(255, 81, 48, 255);
const SDL_Color COLOR_GOLD      = FC_MakeColor(255, 215, 0, 255);
const SDL_Color COLOR_OVERLAY   = FC_MakeColor(0, 0, 0, 160);
const SDL_Color COLOR_WHITEMASK = FC_MakeColor(255, 255, 255, 80);
const SDL_Color COLOR_NULL      = FC_MakeColor(0, 0, 0, 0);

const SDL_Color COLOR_GREY_BG     = FC_MakeColor(51, 51, 51, 255);
const SDL_Color COLOR_GREY_DARKER = FC_MakeColor(70, 70, 70, 255);
const SDL_Color COLOR_GREY_DARK   = FC_MakeColor(79, 79, 79, 255);
const SDL_Color COLOR_GREY_MEDIUM = FC_MakeColor(94, 94, 94, 255);
const SDL_Color COLOR_GREY_LIGHT  = FC_MakeColor(138, 138, 138, 255);
const SDL_Color COLOR_HIGHBLUE    = FC_MakeColor(48, 65, 106, 255);

static const struct Theme defaultTheme = {
    COLOR_BLACK,       // c0
    COLOR_GREY_BG,     // c1
    COLOR_GREY_DARKER, // c2
    COLOR_GREY_DARK,   // c3
    COLOR_GREY_MEDIUM, // c4
    COLOR_GREY_LIGHT,  // c5
    COLOR_WHITE        // c6
};

static struct Theme currentTheme = defaultTheme;

void theme(int t)
{
    switch (t) {
        default:
            currentTheme = defaultTheme;
            break;
    }
}

struct Theme theme(void)
{
    return currentTheme;
}
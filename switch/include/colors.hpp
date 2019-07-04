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

#ifndef COLORS_HPP
#define COLORS_HPP

#include "SDLHelper.hpp"

extern const SDL_Color COLOR_WHITE;
extern const SDL_Color COLOR_BLACK;
extern const SDL_Color COLOR_BLUE;
extern const SDL_Color COLOR_GREEN;
extern const SDL_Color COLOR_RED;
extern const SDL_Color COLOR_GOLD;
extern const SDL_Color COLOR_OVERLAY;
extern const SDL_Color COLOR_WHITEMASK;
extern const SDL_Color COLOR_NULL;

extern const SDL_Color COLOR_GREY_BG;
extern const SDL_Color COLOR_GREY_DARKER;
extern const SDL_Color COLOR_GREY_DARK;
extern const SDL_Color COLOR_GREY_MEDIUM;
extern const SDL_Color COLOR_GREY_LIGHT;
extern const SDL_Color COLOR_HIGHBLUE;

struct Theme {
    SDL_Color c0;
    SDL_Color c1;
    SDL_Color c2;
    SDL_Color c3;
    SDL_Color c4;
    SDL_Color c5;
    SDL_Color c6;
};

#define THEME_DEFAULT 0

void theme(int t);
struct Theme theme(void);

#endif
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

#ifndef COLORS_HPP
#define COLORS_HPP

#include "SDLHelper.hpp"

inline const SDL_Color COLOR_OVERLAY    = FC_MakeColor(0, 0, 0, 200);
inline const SDL_Color COLOR_WHITEMASK  = FC_MakeColor(255, 255, 255, 80);
inline const SDL_Color COLOR_WHITE      = FC_MakeColor(255, 255, 255, 255);
inline const SDL_Color COLOR_BLACK      = FC_MakeColor(0, 0, 0, 255);
inline const SDL_Color COLOR_RED        = FC_MakeColor(255, 81, 48, 255);
inline const SDL_Color COLOR_GOLD       = FC_MakeColor(215, 183, 64, 255);
inline const SDL_Color COLOR_GREY_LIGHT = FC_MakeColor(138, 138, 138, 255);

inline const SDL_Color COLOR_BLACK_DARKERR = FC_MakeColor(18, 18, 20, 255);
inline const SDL_Color COLOR_BLACK_DARKER  = FC_MakeColor(26, 26, 30, 255);
inline const SDL_Color COLOR_BLACK_DARK    = FC_MakeColor(32, 32, 36, 255);
inline const SDL_Color COLOR_BLACK_MEDIUM  = FC_MakeColor(50, 50, 53, 255);
inline const SDL_Color COLOR_PURPLE_DARK   = FC_MakeColor(9, 25, 69, 255);
inline const SDL_Color COLOR_PURPLE_LIGHT  = FC_MakeColor(122, 66, 196, 255);

inline const SDL_Color COLOR_GREEN = FC_MakeColor(0, 254, 197, 255);

#endif
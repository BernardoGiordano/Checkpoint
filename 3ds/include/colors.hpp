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

#include <citro2d.h>

inline const u32 COLOR_OVERLAY    = C2D_Color32(0, 0, 0, 200);
inline const u32 COLOR_WHITEMASK  = C2D_Color32(255, 255, 255, 80);
inline const u32 COLOR_WHITE      = C2D_Color32(255, 255, 255, 255);
inline const u32 COLOR_BLACK      = C2D_Color32(0, 0, 0, 255);
inline const u32 COLOR_RED        = C2D_Color32(255, 0, 0, 255);
inline const u32 COLOR_GOLD       = C2D_Color32(215, 183, 64, 255);
inline const u32 COLOR_GREY_LIGHT = C2D_Color32(138, 138, 138, 255);

inline const u32 COLOR_BLACK_DARKERR = C2D_Color32(15, 15, 17, 255);
inline const u32 COLOR_BLACK_DARKER  = C2D_Color32(22, 22, 26, 255);
inline const u32 COLOR_BLACK_DARK    = C2D_Color32(27, 27, 31, 255);
inline const u32 COLOR_BLACK_MEDIUM  = C2D_Color32(43, 43, 46, 255);
inline const u32 COLOR_PURPLE_DARK   = C2D_Color32(9, 25, 69, 255);
inline const u32 COLOR_PURPLE_LIGHT  = C2D_Color32(122, 66, 196, 255);
inline const u32 COLOR_LIGHT_BLUE    = C2D_Color32(195, 240, 239, 255);

#endif
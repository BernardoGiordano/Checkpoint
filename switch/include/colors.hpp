/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
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

// Design tokens for the dark-first UI.
// A light theme derives later; every draw call in switch/ uses these tokens
// today (no hardcoded SDL_Color literals outside this file).

inline const SDL_Color COLOR_BG       = FC_MakeColor(16, 16, 20, 255); // screen background
inline const SDL_Color COLOR_SURFACE  = FC_MakeColor(23, 23, 29, 255); // settings rows
inline const SDL_Color COLOR_SURFACE2 = FC_MakeColor(21, 21, 27, 255); // panels / cards
inline const SDL_Color COLOR_TILE     = FC_MakeColor(35, 35, 44, 255); // title-icon placeholders, avatar

inline const SDL_Color COLOR_FILL1 = FC_MakeColor(255, 255, 255, 10); // unfocused list rows
inline const SDL_Color COLOR_FILL2 = FC_MakeColor(255, 255, 255, 15); // pills, chips, meta badges
inline const SDL_Color COLOR_FILL3 = FC_MakeColor(255, 255, 255, 31); // button-hint circles, toggle-off track

inline const SDL_Color COLOR_STROKE1 = FC_MakeColor(255, 255, 255, 15); // hairline separators
inline const SDL_Color COLOR_STROKE2 = FC_MakeColor(255, 255, 255, 20); // card/tile borders
inline const SDL_Color COLOR_STROKE3 = FC_MakeColor(255, 255, 255, 41); // outlined-button border

inline const SDL_Color COLOR_ACCENT       = FC_MakeColor(139, 124, 246, 255); // primary buttons, selection, focus ring
inline const SDL_Color COLOR_ACCENT_LIGHT = FC_MakeColor(183, 171, 255, 255); // accent text on dark
inline const SDL_Color COLOR_ACCENT_TINT  = FC_MakeColor(139, 124, 246, 41);  // focused/selected row background

inline const SDL_Color COLOR_TEXT     = FC_MakeColor(242, 242, 247, 255); // primary text
inline const SDL_Color COLOR_TEXT2    = FC_MakeColor(154, 154, 168, 255); // secondary text, inactive nav
inline const SDL_Color COLOR_TEXT3    = FC_MakeColor(109, 109, 122, 255); // tertiary text, section labels, paths
inline const SDL_Color COLOR_MONO_VAL = FC_MakeColor(201, 201, 212, 255); // monospace values on unfocused rows

inline const SDL_Color COLOR_GOLD    = FC_MakeColor(245, 196, 81, 255);  // favorite star
inline const SDL_Color COLOR_SUCCESS = FC_MakeColor(62, 207, 142, 255);  // "running" status text
inline const SDL_Color COLOR_DANGER  = FC_MakeColor(229, 126, 141, 255); // delete affordances

inline const SDL_Color COLOR_WHITE = FC_MakeColor(255, 255, 255, 255);
inline const SDL_Color COLOR_BLACK = FC_MakeColor(0, 0, 0, 255);
inline const SDL_Color COLOR_SCRIM = FC_MakeColor(0, 0, 0, 200); // modal backdrops

#endif

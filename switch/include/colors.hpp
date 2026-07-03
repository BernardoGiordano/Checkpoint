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

#include <string>

// Design tokens for the UI. These are mutable globals: every draw call in
// switch/ reads them by name (no hardcoded SDL_Color literals outside this
// file), and Colors::apply() rewrites them in place when the theme changes.
// The defaults below are the dark theme; the light values live in apply().

inline SDL_Color COLOR_BG       = FC_MakeColor(16, 16, 20, 255); // screen background
inline SDL_Color COLOR_SURFACE  = FC_MakeColor(23, 23, 29, 255); // settings rows
inline SDL_Color COLOR_SURFACE2 = FC_MakeColor(21, 21, 27, 255); // panels / cards
inline SDL_Color COLOR_TILE     = FC_MakeColor(35, 35, 44, 255); // title-icon placeholders, avatar

inline SDL_Color COLOR_FILL1 = FC_MakeColor(255, 255, 255, 10); // unfocused list rows
inline SDL_Color COLOR_FILL2 = FC_MakeColor(255, 255, 255, 15); // pills, chips, meta badges
inline SDL_Color COLOR_FILL3 = FC_MakeColor(255, 255, 255, 31); // button-hint circles, toggle-off track

inline SDL_Color COLOR_STROKE1 = FC_MakeColor(255, 255, 255, 15); // hairline separators
inline SDL_Color COLOR_STROKE2 = FC_MakeColor(255, 255, 255, 20); // card/tile borders
inline SDL_Color COLOR_STROKE3 = FC_MakeColor(255, 255, 255, 41); // outlined-button border

inline SDL_Color COLOR_ACCENT       = FC_MakeColor(139, 124, 246, 255); // primary buttons, selection, focus ring
inline SDL_Color COLOR_ACCENT_LIGHT = FC_MakeColor(183, 171, 255, 255); // accent text on surfaces
inline SDL_Color COLOR_ACCENT_TINT  = FC_MakeColor(139, 124, 246, 41);  // focused/selected row background

inline SDL_Color COLOR_TEXT     = FC_MakeColor(242, 242, 247, 255); // primary text
inline SDL_Color COLOR_TEXT2    = FC_MakeColor(154, 154, 168, 255); // secondary text, inactive nav
inline SDL_Color COLOR_TEXT3    = FC_MakeColor(109, 109, 122, 255); // tertiary text, section labels, paths
inline SDL_Color COLOR_MONO_VAL = FC_MakeColor(201, 201, 212, 255); // monospace values on unfocused rows

inline SDL_Color COLOR_GOLD    = FC_MakeColor(245, 196, 81, 255);  // favorite star
inline SDL_Color COLOR_SUCCESS = FC_MakeColor(62, 207, 142, 255);  // "running" status text
inline SDL_Color COLOR_DANGER  = FC_MakeColor(229, 126, 141, 255); // delete affordances

// Absolute colors: identical in both themes. COLOR_WHITE stays white because it
// is only ever drawn on the accent fill (button labels, toggle knob), never as
// body text on a surface.
inline SDL_Color COLOR_WHITE = FC_MakeColor(255, 255, 255, 255);
inline SDL_Color COLOR_BLACK = FC_MakeColor(0, 0, 0, 255);
inline SDL_Color COLOR_SCRIM = FC_MakeColor(0, 0, 0, 200); // modal backdrops

namespace Colors {
    // Rewrites the tokens above to match the requested theme ("light" selects
    // the light palette; anything else, including "dark", selects the dark one).
    // Safe to call every time the theme setting changes; the next frame redraws
    // with the new values since the whole UI is re-rendered each frame.
    inline void apply(const std::string& theme)
    {
        if (theme == "light") {
            COLOR_BG       = FC_MakeColor(236, 236, 240, 255);
            COLOR_SURFACE  = FC_MakeColor(255, 255, 255, 255);
            COLOR_SURFACE2 = FC_MakeColor(247, 247, 250, 255);
            COLOR_TILE     = FC_MakeColor(222, 222, 229, 255);

            // White overlays lighten on dark; on light surfaces we darken instead,
            // so the fills/strokes become low-alpha black.
            COLOR_FILL1 = FC_MakeColor(0, 0, 0, 8);
            COLOR_FILL2 = FC_MakeColor(0, 0, 0, 13);
            COLOR_FILL3 = FC_MakeColor(0, 0, 0, 28);

            COLOR_STROKE1 = FC_MakeColor(0, 0, 0, 18);
            COLOR_STROKE2 = FC_MakeColor(0, 0, 0, 26);
            COLOR_STROKE3 = FC_MakeColor(0, 0, 0, 48);

            COLOR_ACCENT       = FC_MakeColor(124, 108, 240, 255); // slightly deeper for white-text contrast
            COLOR_ACCENT_LIGHT = FC_MakeColor(92, 74, 214, 255);   // accent text readable on light
            COLOR_ACCENT_TINT  = FC_MakeColor(124, 108, 240, 36);

            COLOR_TEXT     = FC_MakeColor(24, 24, 30, 255);
            COLOR_TEXT2    = FC_MakeColor(92, 92, 104, 255);
            COLOR_TEXT3    = FC_MakeColor(132, 132, 144, 255);
            COLOR_MONO_VAL = FC_MakeColor(70, 70, 82, 255);

            COLOR_GOLD    = FC_MakeColor(200, 150, 20, 255);
            COLOR_SUCCESS = FC_MakeColor(22, 158, 100, 255);
            COLOR_DANGER  = FC_MakeColor(204, 58, 80, 255);

            COLOR_SCRIM = FC_MakeColor(0, 0, 0, 120);
        }
        else {
            COLOR_BG       = FC_MakeColor(16, 16, 20, 255);
            COLOR_SURFACE  = FC_MakeColor(23, 23, 29, 255);
            COLOR_SURFACE2 = FC_MakeColor(21, 21, 27, 255);
            COLOR_TILE     = FC_MakeColor(35, 35, 44, 255);

            COLOR_FILL1 = FC_MakeColor(255, 255, 255, 10);
            COLOR_FILL2 = FC_MakeColor(255, 255, 255, 15);
            COLOR_FILL3 = FC_MakeColor(255, 255, 255, 31);

            COLOR_STROKE1 = FC_MakeColor(255, 255, 255, 15);
            COLOR_STROKE2 = FC_MakeColor(255, 255, 255, 20);
            COLOR_STROKE3 = FC_MakeColor(255, 255, 255, 41);

            COLOR_ACCENT       = FC_MakeColor(139, 124, 246, 255);
            COLOR_ACCENT_LIGHT = FC_MakeColor(183, 171, 255, 255);
            COLOR_ACCENT_TINT  = FC_MakeColor(139, 124, 246, 41);

            COLOR_TEXT     = FC_MakeColor(242, 242, 247, 255);
            COLOR_TEXT2    = FC_MakeColor(154, 154, 168, 255);
            COLOR_TEXT3    = FC_MakeColor(109, 109, 122, 255);
            COLOR_MONO_VAL = FC_MakeColor(201, 201, 212, 255);

            COLOR_GOLD    = FC_MakeColor(245, 196, 81, 255);
            COLOR_SUCCESS = FC_MakeColor(62, 207, 142, 255);
            COLOR_DANGER  = FC_MakeColor(229, 126, 141, 255);

            COLOR_SCRIM = FC_MakeColor(0, 0, 0, 200);
        }
    }
}

#endif

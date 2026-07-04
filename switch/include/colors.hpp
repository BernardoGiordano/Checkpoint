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

#include "gfxtypes.hpp"

#include <string>

// Design tokens for the UI. These are mutable globals: every draw call in
// switch/ reads them by name (no hardcoded Color literals outside this
// file), and Colors::apply() rewrites them in place when the theme changes.
// The defaults below are the dark theme; the light values live in apply().

inline Color COLOR_BG       = makeColor(16, 16, 20, 255); // screen background
inline Color COLOR_SURFACE  = makeColor(23, 23, 29, 255); // settings rows
inline Color COLOR_SURFACE2 = makeColor(21, 21, 27, 255); // panels / cards
inline Color COLOR_TILE     = makeColor(35, 35, 44, 255); // title-icon placeholders, avatar

inline Color COLOR_FILL1 = makeColor(255, 255, 255, 10); // unfocused list rows
inline Color COLOR_FILL2 = makeColor(255, 255, 255, 15); // pills, chips, meta badges
inline Color COLOR_FILL3 = makeColor(255, 255, 255, 31); // button-hint circles, toggle-off track

inline Color COLOR_STROKE1 = makeColor(255, 255, 255, 15); // hairline separators
inline Color COLOR_STROKE2 = makeColor(255, 255, 255, 20); // card/tile borders
inline Color COLOR_STROKE3 = makeColor(255, 255, 255, 41); // outlined-button border

inline Color COLOR_ACCENT       = makeColor(139, 124, 246, 255); // primary buttons, selection, focus ring
inline Color COLOR_ACCENT_LIGHT = makeColor(183, 171, 255, 255); // accent text on surfaces
inline Color COLOR_ACCENT_TINT  = makeColor(139, 124, 246, 41);  // focused/selected row background

inline Color COLOR_TEXT     = makeColor(242, 242, 247, 255); // primary text
inline Color COLOR_TEXT2    = makeColor(154, 154, 168, 255); // secondary text, inactive nav
inline Color COLOR_TEXT3    = makeColor(109, 109, 122, 255); // tertiary text, section labels, paths
inline Color COLOR_MONO_VAL = makeColor(201, 201, 212, 255); // monospace values on unfocused rows

inline Color COLOR_GOLD    = makeColor(245, 196, 81, 255);  // favorite star
inline Color COLOR_SUCCESS = makeColor(62, 207, 142, 255);  // "running" status text
inline Color COLOR_DANGER  = makeColor(229, 126, 141, 255); // delete affordances

// Absolute colors: identical in both themes. COLOR_WHITE stays white because it
// is only ever drawn on the accent fill (button labels, toggle knob), never as
// body text on a surface.
inline Color COLOR_WHITE = makeColor(255, 255, 255, 255);
inline Color COLOR_BLACK = makeColor(0, 0, 0, 255);
inline Color COLOR_SCRIM = makeColor(0, 0, 0, 200); // modal backdrops

namespace Colors {
    // Rewrites the tokens above to match the requested theme ("light" selects
    // the light palette; anything else, including "dark", selects the dark one).
    // Safe to call every time the theme setting changes; the next frame redraws
    // with the new values since the whole UI is re-rendered each frame.
    inline void apply(const std::string& theme)
    {
        if (theme == "light") {
            COLOR_BG       = makeColor(236, 236, 240, 255);
            COLOR_SURFACE  = makeColor(255, 255, 255, 255);
            COLOR_SURFACE2 = makeColor(247, 247, 250, 255);
            COLOR_TILE     = makeColor(222, 222, 229, 255);

            // White overlays lighten on dark; on light surfaces we darken instead,
            // so the fills/strokes become low-alpha black.
            COLOR_FILL1 = makeColor(0, 0, 0, 8);
            COLOR_FILL2 = makeColor(0, 0, 0, 13);
            COLOR_FILL3 = makeColor(0, 0, 0, 28);

            COLOR_STROKE1 = makeColor(0, 0, 0, 18);
            COLOR_STROKE2 = makeColor(0, 0, 0, 26);
            COLOR_STROKE3 = makeColor(0, 0, 0, 48);

            COLOR_ACCENT       = makeColor(124, 108, 240, 255); // slightly deeper for white-text contrast
            COLOR_ACCENT_LIGHT = makeColor(92, 74, 214, 255);   // accent text readable on light
            COLOR_ACCENT_TINT  = makeColor(124, 108, 240, 36);

            COLOR_TEXT     = makeColor(24, 24, 30, 255);
            COLOR_TEXT2    = makeColor(92, 92, 104, 255);
            COLOR_TEXT3    = makeColor(132, 132, 144, 255);
            COLOR_MONO_VAL = makeColor(70, 70, 82, 255);

            COLOR_GOLD    = makeColor(200, 150, 20, 255);
            COLOR_SUCCESS = makeColor(22, 158, 100, 255);
            COLOR_DANGER  = makeColor(204, 58, 80, 255);

            COLOR_SCRIM = makeColor(0, 0, 0, 120);
        }
        else {
            COLOR_BG       = makeColor(16, 16, 20, 255);
            COLOR_SURFACE  = makeColor(23, 23, 29, 255);
            COLOR_SURFACE2 = makeColor(21, 21, 27, 255);
            COLOR_TILE     = makeColor(35, 35, 44, 255);

            COLOR_FILL1 = makeColor(255, 255, 255, 10);
            COLOR_FILL2 = makeColor(255, 255, 255, 15);
            COLOR_FILL3 = makeColor(255, 255, 255, 31);

            COLOR_STROKE1 = makeColor(255, 255, 255, 15);
            COLOR_STROKE2 = makeColor(255, 255, 255, 20);
            COLOR_STROKE3 = makeColor(255, 255, 255, 41);

            COLOR_ACCENT       = makeColor(139, 124, 246, 255);
            COLOR_ACCENT_LIGHT = makeColor(183, 171, 255, 255);
            COLOR_ACCENT_TINT  = makeColor(139, 124, 246, 41);

            COLOR_TEXT     = makeColor(242, 242, 247, 255);
            COLOR_TEXT2    = makeColor(154, 154, 168, 255);
            COLOR_TEXT3    = makeColor(109, 109, 122, 255);
            COLOR_MONO_VAL = makeColor(201, 201, 212, 255);

            COLOR_GOLD    = makeColor(245, 196, 81, 255);
            COLOR_SUCCESS = makeColor(62, 207, 142, 255);
            COLOR_DANGER  = makeColor(229, 126, 141, 255);

            COLOR_SCRIM = makeColor(0, 0, 0, 200);
        }
    }
}

#endif

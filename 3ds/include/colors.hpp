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
#include <string>

// Color tokens are mutable globals: the whole UI reads them by name and
// Colors::apply() rewrites them in place when the theme changes. The defaults
// below are the dark theme; the light values live in apply(). The next frame
// picks up the change automatically since every frame is fully redrawn.

inline u32 COLOR_OVERLAY    = C2D_Color32(0, 0, 0, 200);
inline u32 COLOR_WHITEMASK  = C2D_Color32(255, 255, 255, 80);
inline u32 COLOR_WHITE      = C2D_Color32(255, 255, 255, 255);
inline u32 COLOR_BLACK      = C2D_Color32(0, 0, 0, 255);
inline u32 COLOR_RED        = C2D_Color32(255, 0, 0, 255);
inline u32 COLOR_GOLD       = C2D_Color32(215, 183, 64, 255);
inline u32 COLOR_GREY_LIGHT = C2D_Color32(138, 138, 138, 255);

inline u32 COLOR_BLACK_DARKERR = C2D_Color32(15, 15, 17, 255);
inline u32 COLOR_BLACK_DARKER  = C2D_Color32(22, 22, 26, 255);
inline u32 COLOR_BLACK_DARK    = C2D_Color32(27, 27, 31, 255);
inline u32 COLOR_BLACK_MEDIUM  = C2D_Color32(43, 43, 46, 255);
inline u32 COLOR_PURPLE_DARK   = C2D_Color32(9, 25, 69, 255);
inline u32 COLOR_PURPLE_LIGHT  = C2D_Color32(122, 66, 196, 255);
inline u32 COLOR_LIGHT_BLUE    = C2D_Color32(195, 240, 239, 255);

// ---- v4 redesign tokens (Checkpoint UI/UX refresh) ----
// Surfaces
inline u32 COLOR_V4_BASE    = C2D_Color32(14, 16, 20, 255); // #0E1014 screens
inline u32 COLOR_V4_SURFACE = C2D_Color32(21, 24, 30, 255); // #15181E bars
inline u32 COLOR_V4_CARD    = C2D_Color32(19, 22, 28, 255); // #13161C cards
inline u32 COLOR_V4_RAISED  = C2D_Color32(26, 30, 38, 255); // #1A1E26 buttons
inline u32 COLOR_V4_LINE    = C2D_Color32(32, 36, 44, 255); // #20242C hairlines
// Accents
inline u32 COLOR_V4_ACCENT = C2D_Color32(122, 66, 196, 255);  // #7A42C4 selection
inline u32 COLOR_V4_RING   = C2D_Color32(154, 107, 255, 255); // #9A6BFF pulsing ring
inline u32 COLOR_V4_TEAL   = C2D_Color32(143, 227, 218, 255); // #8FE3DA additive actions
inline u32 COLOR_V4_GOLD   = C2D_Color32(230, 195, 77, 255);  // #E6C34D favorites
inline u32 COLOR_V4_DANGER = C2D_Color32(229, 72, 77, 255);   // #E5484D delete
// Text
inline u32 COLOR_V4_TEXT  = C2D_Color32(238, 241, 245, 255); // #EEF1F5 primary
inline u32 COLOR_V4_MUTED = C2D_Color32(154, 163, 177, 255); // #9AA3B1 secondary
inline u32 COLOR_V4_FAINT = C2D_Color32(111, 119, 133, 255); // #6F7785 captions
inline u32 COLOR_V4_DIM   = C2D_Color32(0, 0, 0, 130);       // multi-select dim veil

namespace Colors {
    // Rewrites the tokens above to match the requested theme ("light" selects
    // the light palette; anything else, including "dark", selects the dark one).
    // Only the semantic surface/text tokens flip. Absolute colors (WHITE, BLACK,
    // RED) and the accent hues (ACCENT/RING/TEAL/GOLD/DANGER) stay put: WHITE is
    // only ever drawn on the accent fill, so it must remain white in both themes.
    inline void apply(const std::string& theme)
    {
        if (theme == "light") {
            // Surfaces: light greys, subtle elevation.
            COLOR_V4_BASE    = C2D_Color32(238, 239, 242, 255);
            COLOR_V4_SURFACE = C2D_Color32(248, 249, 251, 255);
            COLOR_V4_CARD    = C2D_Color32(255, 255, 255, 255);
            COLOR_V4_RAISED  = C2D_Color32(230, 232, 236, 255);
            COLOR_V4_LINE    = C2D_Color32(210, 213, 219, 255);
            // Text: dark on light.
            COLOR_V4_TEXT  = C2D_Color32(24, 27, 33, 255);
            COLOR_V4_MUTED = C2D_Color32(88, 94, 104, 255);
            COLOR_V4_FAINT = C2D_Color32(128, 134, 144, 255);
            // Accents: deepen slightly so white text keeps contrast on light.
            COLOR_V4_ACCENT = C2D_Color32(108, 54, 180, 255);
            COLOR_V4_GOLD   = C2D_Color32(190, 150, 30, 255);
            COLOR_V4_DANGER = C2D_Color32(206, 48, 54, 255);
            // Masks/veils invert: darken over light instead of lightening.
            COLOR_WHITEMASK    = C2D_Color32(0, 0, 0, 40);
            COLOR_BLACK_MEDIUM = C2D_Color32(212, 214, 219, 255); // progress-bar track
            COLOR_OVERLAY      = C2D_Color32(0, 0, 0, 110);       // modal scrim
        }
        else {
            COLOR_V4_BASE      = C2D_Color32(14, 16, 20, 255);
            COLOR_V4_SURFACE   = C2D_Color32(21, 24, 30, 255);
            COLOR_V4_CARD      = C2D_Color32(19, 22, 28, 255);
            COLOR_V4_RAISED    = C2D_Color32(26, 30, 38, 255);
            COLOR_V4_LINE      = C2D_Color32(32, 36, 44, 255);
            COLOR_V4_TEXT      = C2D_Color32(238, 241, 245, 255);
            COLOR_V4_MUTED     = C2D_Color32(154, 163, 177, 255);
            COLOR_V4_FAINT     = C2D_Color32(111, 119, 133, 255);
            COLOR_V4_ACCENT    = C2D_Color32(122, 66, 196, 255);
            COLOR_V4_GOLD      = C2D_Color32(230, 195, 77, 255);
            COLOR_V4_DANGER    = C2D_Color32(229, 72, 77, 255);
            COLOR_WHITEMASK    = C2D_Color32(255, 255, 255, 80);
            COLOR_BLACK_MEDIUM = C2D_Color32(43, 43, 46, 255);
            COLOR_OVERLAY      = C2D_Color32(0, 0, 0, 200);
        }
    }
}

#endif
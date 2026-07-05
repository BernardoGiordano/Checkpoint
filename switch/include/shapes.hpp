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

#ifndef SHAPES_HPP
#define SHAPES_HPP

#include "gfx.hpp"

// SDL2 has no native rounded-rect draw call, so the whole design system's
// radii (999px pills, 7-16px cards/tiles/buttons) go through this one
// module. A pill or circle is simply a rect whose radius is h/2 — there is no
// separate pill/circle primitive.
namespace Shapes {
    // Filled rounded rect. Radius is clamped to min(w,h)/2. Implemented as one
    // fill for the straight middle band plus one 1px-tall fill per corner row
    // (mirrored top/bottom) — cheap enough to call a few dozen times a frame.
    void fillRound(int x, int y, int w, int h, int r, Color color);

    // Filled rounded rect with a border: an outer fillRound in borderColor,
    // then an inset fillRound in fillColor. Correct for the flat, opaque card/
    // row/tile backgrounds used throughout (borderPx == 0 skips the outer pass
    // entirely and just fills).
    void cardRound(int x, int y, int w, int h, int r, Color fillColor, Color borderColor, int borderPx);

    // Hollow rounded-rect outline of the given thickness — unlike cardRound,
    // does not touch the pixels inside the ring, so it can be drawn over an
    // already-drawn element (a tile, a row) without erasing it. Row-by-row
    // (not the cheap corners-only path fillRound uses); only ever a handful of
    // these are on screen at once (the focus ring + its glow layers).
    void strokeRound(int x, int y, int w, int h, int r, int thickness, Color color);

    // The joypad focus ring: a crisp 3px ring at inset -4 (radius r+3),
    // plus two wider, lower-alpha rings outside it approximating the spec's
    // `0 0 18px rgba(139,124,246,.45)` CSS glow. Static, not pulsing — draw it
    // fresh each frame around whatever is focused; exactly one should be on
    // screen at a time.
    void focusRing(int x, int y, int w, int h, int r, Color accent);
}

#endif

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

#include "shapes.hpp"
#include "gfxutils.hpp"
#include <algorithm>

namespace {
    int clampRadius(int w, int h, int r)
    {
        int maxR = std::min(w, h) / 2;
        return std::max(0, std::min(r, maxR));
    }
}

void Shapes::fillRound(int x, int y, int w, int h, int r, Color color)
{
    Gfx::FillRoundRect(x, y, w, h, clampRadius(w, h, r), color);
}

void Shapes::cardRound(int x, int y, int w, int h, int r, Color fillColor, Color borderColor, int borderPx)
{
    r = clampRadius(w, h, r);
    if (borderPx <= 0 || w <= 2 * borderPx || h <= 2 * borderPx) {
        fillRound(x, y, w, h, r, fillColor);
        return;
    }
    fillRound(x, y, w, h, r, borderColor);
    fillRound(x + borderPx, y + borderPx, w - 2 * borderPx, h - 2 * borderPx, std::max(0, r - borderPx), fillColor);
}

void Shapes::strokeRound(int x, int y, int w, int h, int r, int thickness, Color color)
{
    if (thickness <= 0 || w <= 0 || h <= 0)
        return;
    Gfx::StrokeRoundRect(x, y, w, h, clampRadius(w, h, r), thickness, color);
}

void Shapes::focusRing(int x, int y, int w, int h, int r, Color accent)
{
    // The pre-redesign selector: a square outline hugging the element that
    // breathes accent -> white on a ~1s triangle wave. Preferred over a rounded
    // ring because SDL2's hand-rasterised corners look pixelated at this size
    // and the square outline reads more crisply. `r` is intentionally ignored.
    (void)r;
    drawPulsingOutline(x, y, w, h, 4, accent);
}

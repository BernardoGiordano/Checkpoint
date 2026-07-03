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
#include <algorithm>
#include <cmath>

namespace {
    int clampRadius(int w, int h, int r)
    {
        int maxR = std::min(w, h) / 2;
        return std::max(0, std::min(r, maxR));
    }

    // Horizontal inset (from each side) at row `row` (0-indexed from the
    // shape's top edge) of a w x h rect with corner radius r. 0 outside the
    // corner rows, symmetric top/bottom.
    int cornerInset(int r, int h, int row)
    {
        int fromEdge = std::min(row, h - row - 1);
        if (fromEdge >= r)
            return 0;
        int dy = r - fromEdge;
        int dx = (int)(std::sqrt((double)(r * r - dy * dy)) + 0.5);
        return r - dx;
    }
}

void Shapes::fillRound(int x, int y, int w, int h, int r, SDL_Color color)
{
    r = clampRadius(w, h, r);
    if (r <= 0) {
        SDLH_DrawRect(x, y, w, h, color);
        return;
    }

    if (h > 2 * r) {
        SDLH_DrawRect(x, y + r, w, h - 2 * r, color);
    }
    for (int i = 0; i < r; i++) {
        int inset = cornerInset(r, h, i);
        int rowW  = w - 2 * inset;
        if (rowW <= 0)
            continue;
        SDLH_DrawRect(x + inset, y + i, rowW, 1, color);         // top
        SDLH_DrawRect(x + inset, y + h - 1 - i, rowW, 1, color); // bottom (mirrored)
    }
}

void Shapes::cardRound(int x, int y, int w, int h, int r, SDL_Color fillColor, SDL_Color borderColor, int borderPx)
{
    r = clampRadius(w, h, r);
    if (borderPx <= 0 || w <= 2 * borderPx || h <= 2 * borderPx) {
        fillRound(x, y, w, h, r, fillColor);
        return;
    }
    fillRound(x, y, w, h, r, borderColor);
    fillRound(x + borderPx, y + borderPx, w - 2 * borderPx, h - 2 * borderPx, std::max(0, r - borderPx), fillColor);
}

void Shapes::strokeRound(int x, int y, int w, int h, int r, int thickness, SDL_Color color)
{
    r = clampRadius(w, h, r);
    if (thickness <= 0 || w <= 0 || h <= 0)
        return;

    const int ix = thickness, iy = thickness;
    const int iw = w - 2 * thickness, ih = h - 2 * thickness;
    const int ir        = std::max(0, r - thickness);
    const bool hasInner = iw > 0 && ih > 0;

    for (int row = 0; row < h; row++) {
        int outerInset = cornerInset(r, h, row);
        int outerLeft  = x + outerInset;
        int outerRight = x + w - outerInset;
        if (outerRight <= outerLeft)
            continue;

        int innerRow = row - iy;
        if (hasInner && innerRow >= 0 && innerRow < ih) {
            int innerInset = cornerInset(ir, ih, innerRow);
            int innerLeft  = x + ix + innerInset;
            int innerRight = x + ix + iw - innerInset;
            if (innerLeft > outerLeft) {
                SDLH_DrawRect(outerLeft, y + row, innerLeft - outerLeft, 1, color);
            }
            if (outerRight > innerRight) {
                SDLH_DrawRect(innerRight, y + row, outerRight - innerRight, 1, color);
            }
        }
        else {
            SDLH_DrawRect(outerLeft, y + row, outerRight - outerLeft, 1, color);
        }
    }
}

void Shapes::focusRing(int x, int y, int w, int h, int r, SDL_Color accent)
{
    // The pre-redesign selector: a square outline hugging the element that
    // breathes accent -> white on a ~1s triangle wave. Preferred over a rounded
    // ring because SDL2's hand-rasterised corners look pixelated at this size
    // and the square outline reads more crisply. `r` is intentionally ignored.
    (void)r;
    drawPulsingOutline(x, y, w, h, 4, accent);
}

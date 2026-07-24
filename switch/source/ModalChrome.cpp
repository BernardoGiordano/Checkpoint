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

#include "ModalChrome.hpp"
#include <algorithm>

namespace {
    // Rewind to the start of the UTF-8 sequence covering byte `i`, so a cut
    // never lands inside a multi-byte glyph.
    size_t utf8Boundary(const std::string& s, size_t i)
    {
        while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) {
            i--;
        }
        return i;
    }
}

ModalChrome::Layout ModalChrome::fitText(std::string& text, int size, bool hasHeader)
{
    const int headerH = hasHeader ? HEADER_H : 0;
    const int chromeH = PAD + headerH + TEXT_GAP + BTN_H + BTN_PAD;
    const int maxText = CARD_MAX_H - chromeH;

    u32 w, h;
    Gfx::MeasureTextBox(size, text.c_str(), TEXT_MAX_W, &w, &h);

    // Cut proportionally to the overshoot and re-measure: the wrapping happens
    // inside DrawTextBox, so the line count is only knowable by measuring.
    // `keep` always loses more bytes than the "..." adds, so this terminates.
    while ((int)h > maxText && text.size() > 8) {
        size_t keep = std::min((size_t)(text.size() * (double)maxText / (double)h), text.size() - 4);
        keep        = utf8Boundary(text, keep);
        if (keep == 0) {
            break;
        }
        text = text.substr(0, keep) + "...";
        Gfx::MeasureTextBox(size, text.c_str(), TEXT_MAX_W, &w, &h);
    }

    Layout l;
    l.cardH   = std::clamp(chromeH + (int)h, CARD_MIN_H, CARD_MAX_H);
    l.cardY   = CARD_CENTER_Y - l.cardH / 2;
    l.headerY = l.cardY + 16;
    l.btnY    = l.cardY + l.cardH - BTN_PAD - BTN_H;

    // Center the block in whatever band is left between header and buttons: on
    // a min-height card that band is taller than the text, on a grown one it
    // matches it exactly.
    const int bandTop = l.cardY + PAD + headerH;
    const int bandH   = l.btnY - TEXT_GAP - bandTop;
    l.textX           = CARD_X + ((int)CARD_W - (int)w) / 2;
    l.textY           = bandTop + std::max(0, (bandH - (int)h) / 2);
    return l;
}

void ModalChrome::dim(void)
{
    Gfx::DrawRect(0, 0, 1280, 720, COLOR_SCRIM);
}

void ModalChrome::drawCard(const Layout& layout, Color surface)
{
    Gfx::DrawRect(CARD_X, layout.cardY, CARD_W, layout.cardH, surface);
}

void ModalChrome::drawText(const Layout& layout, const std::string& text, Color color, int size)
{
    Gfx::DrawTextBox(size, layout.textX, layout.textY, color, TEXT_MAX_W, text.c_str());
}

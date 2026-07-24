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
#include "util.hpp"
#include <algorithm>

namespace {
    // Keep the first `maxLines` lines of an already-wrapped block, marking the
    // cut with an ellipsis so a clipped message doesn't read as a complete one.
    std::string keepLines(const std::string& wrapped, int maxLines)
    {
        size_t pos = 0;
        for (int i = 0; i < maxLines; i++) {
            const size_t next = wrapped.find('\n', pos);
            if (next == std::string::npos) {
                return wrapped;
            }
            pos = next + 1;
        }
        return wrapped.substr(0, pos) + "...";
    }
}

ModalChrome::Layout ModalChrome::fitText(std::string& text, float textSize, bool hasHeader)
{
    text = StringUtils::wrap(text, textSize, TEXT_MAX_W);

    const int headerH = hasHeader ? HEADER_H : 0;
    const int chromeH = PAD + headerH + TEXT_GAP + BTN_H + PAD;
    const float lineH = StringUtils::textHeight("", textSize);
    int textH         = (int)StringUtils::textHeight(text, textSize);

    if (chromeH + textH > CARD_MAX_H) {
        // One line short of the ellipsis' own line, so the "..." fits too.
        const int fits = std::max(1, (int)((CARD_MAX_H - chromeH) / lineH) - 1);
        text           = keepLines(text, fits);
        textH          = (int)StringUtils::textHeight(text, textSize);
    }

    Layout l;
    l.cardH   = std::clamp(chromeH + textH, CARD_MIN_H, CARD_MAX_H);
    l.cardY   = CARD_CENTER_Y - l.cardH / 2;
    l.headerY = l.cardY + 8;
    l.btnY    = l.cardY + l.cardH - PAD - BTN_H;

    // Center the block in whatever band is left between header and buttons; on
    // a min-height card that band is taller than the text, on a grown one it
    // matches it exactly.
    const int bandTop = l.cardY + PAD + headerH;
    const int bandH   = l.btnY - TEXT_GAP - bandTop;
    l.textY           = bandTop + std::max(0, (bandH - textH) / 2);
    return l;
}

void ModalChrome::dimTop(float z)
{
    C2D_DrawRectSolid(0, 0, z, 400, 240, COLOR_OVERLAY);
}

void ModalChrome::dimBottom(float z)
{
    C2D_DrawRectSolid(0, 0, z, 320, 240, COLOR_OVERLAY);
}

void ModalChrome::drawCard(const Layout& layout, u32 outlineColor)
{
    C2D_DrawRectSolid(CARD_X, layout.cardY, 0.5f, CARD_W, layout.cardH, COLOR_CARD);
    Gui::drawOutline(CARD_X, layout.cardY, CARD_W, layout.cardH, 2, outlineColor);
}

void ModalChrome::drawListCard(void)
{
    C2D_DrawRectSolid(LIST_X, LIST_Y, 0.6f, LIST_W, LIST_H, COLOR_CARD);
    Gui::drawOutline(LIST_X, LIST_Y, LIST_W, LIST_H, 2, COLOR_ACCENT);
}

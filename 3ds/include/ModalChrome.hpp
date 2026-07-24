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

#ifndef MODALCHROME_HPP
#define MODALCHROME_HPP

#include "colors.hpp"
#include "gui.hpp"
#include <citro2d.h>
#include <string>

// The one home for the modal-dialog geometry. Every overlay draws its backdrop
// and card through these helpers, so a dialog's box and its hit-tests / button
// outlines can never drift apart.
namespace ModalChrome {
    // Small message / choice card on the bottom screen. Near-full width (16..304
    // of the 320-wide screen) so a dialog doesn't read as a floating chip. Its
    // height is not fixed: fitText() grows it to the wrapped message (see below).
    constexpr int CARD_X = 16, CARD_W = 288;
    // Text is wrapped to this width inside the card (16px padding each side).
    constexpr int TEXT_MAX_W = 256;
    // Button row inside the small card: one wide OK, or a left/right pair.
    constexpr int BTN_H      = 32;
    constexpr int BTN_WIDE_X = 28, BTN_WIDE_W = 264;
    constexpr int BTN_LEFT_X = 28, BTN_RIGHT_X = 164, BTN_HALF_W = 128;
    // Full-screen list-picker card on the top screen.
    constexpr int LIST_X = 24, LIST_Y = 14, LIST_W = 352, LIST_H = 212;

    // Auto-sizing: the card keeps `PAD` of breathing room above the text and
    // below the button row, `TEXT_GAP` between the two, and `HEADER_H` for the
    // optional accent line. It never shrinks below CARD_MIN_H (the old fixed
    // box, so short dialogs look unchanged) nor grows past CARD_MAX_H, which
    // leaves SCREEN_MARGIN against both screen edges. The card always stays
    // centered on CARD_CENTER_Y, so CARD_MAX_H is exactly twice its headroom.
    constexpr int PAD = 12, HEADER_H = 20, TEXT_GAP = 10;
    constexpr int CARD_CENTER_Y = 120, SCREEN_MARGIN = 8;
    constexpr int CARD_MIN_H = 132, CARD_MAX_H = 2 * (CARD_CENTER_Y - SCREEN_MARGIN);

    // Where an auto-sized card and its contents landed.
    struct Layout {
        int cardY, cardH;
        int headerY; // accent line, valid only when fitText was told there is one
        int textY;   // top of the wrapped text block
        int btnY;    // top of the button row
    };

    // Wraps `text` to TEXT_MAX_W in place, sizes a card around it and returns
    // that geometry. Text taller than the biggest allowed card is cut to the
    // lines that fit, with the last one marked "...".
    Layout fitText(std::string& text, float textSize, bool hasHeader);

    // Dimmed backdrop over a whole screen at depth `z`.
    void dimTop(float z = 0.5f);
    void dimBottom(float z = 0.5f);
    // Small card on the bottom screen, outlined in `outlineColor`.
    void drawCard(const Layout& layout, u32 outlineColor);
    // List-picker card on the top screen (accent outline, picker depth).
    void drawListCard(void);
}

#endif

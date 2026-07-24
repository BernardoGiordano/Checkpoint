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
#include "gfx.hpp"
#include <string>

// The one home for the message/choice modal geometry on Switch. Info, error,
// yes/no and script-message overlays all draw their scrim, card and buttons
// through these constants/helpers, so the box, its text bounds and its button
// hit-tests can never drift apart (mirrors the 3DS ModalChrome).
namespace ModalChrome {
    // Centered card on the 1280x720 screen. Wider than the old 640-wide box so
    // it reads as a dialog rather than a floating chip. Its height is not fixed:
    // fitText() grows it to the wrapped message (see below).
    constexpr int CARD_X = 192, CARD_W = 896;
    constexpr int PAD = 32;

    // Message body: wrapped to this width, smaller than the old size-28 so long
    // strings fit inside the card instead of bleeding past its edges.
    constexpr int TEXT_SIZE  = 24;
    constexpr int TEXT_X     = CARD_X + PAD;     // 224
    constexpr int TEXT_MAX_W = CARD_W - 2 * PAD; // 832

    // Button row along the bottom of the card: one wide button, or a split pair.
    constexpr int BTN_H       = 56;
    constexpr int BTN_SIZE    = 24;
    constexpr int BTN_PAD     = 24;               // card bottom edge -> button row
    constexpr int BTN_WIDE_X  = CARD_X + PAD;     // 224
    constexpr int BTN_WIDE_W  = CARD_W - 2 * PAD; // 832
    constexpr int BTN_GAP     = 16;
    constexpr int BTN_HALF_W  = (BTN_WIDE_W - BTN_GAP) / 2;        // 408
    constexpr int BTN_LEFT_X  = BTN_WIDE_X;                        // 224
    constexpr int BTN_RIGHT_X = BTN_WIDE_X + BTN_HALF_W + BTN_GAP; // 648

    // Auto-sizing: the card keeps `PAD` above the text, `TEXT_GAP` between text
    // and button row, `BTN_PAD` below it, and `HEADER_H` for the optional error
    // line. It never shrinks below CARD_MIN_H (the old fixed box, so short
    // dialogs look unchanged) nor grows past CARD_MAX_H, which leaves
    // SCREEN_MARGIN against both screen edges. The card stays centered on
    // CARD_CENTER_Y, so CARD_MAX_H is exactly twice its headroom.
    constexpr int TEXT_GAP = 16, HEADER_H = 40;
    constexpr int CARD_CENTER_Y = 340, SCREEN_MARGIN = 40;
    constexpr int CARD_MIN_H = 300, CARD_MAX_H = 2 * (CARD_CENTER_Y - SCREEN_MARGIN);

    // Where an auto-sized card and its contents landed.
    struct Layout {
        int cardY, cardH;
        int headerY;      // error line, valid only when fitText was told there is one
        int textX, textY; // top-left of the wrapped text block
        int btnY;         // top of the button row
    };

    // Sizes a card around `text` wrapped to TEXT_MAX_W and returns that
    // geometry. Text taller than the biggest allowed card is cut to what fits,
    // ending in "...".
    Layout fitText(std::string& text, int size = TEXT_SIZE, bool hasHeader = false);

    // Full-screen dimmed scrim behind the card.
    void dim(void);
    // The card itself, filled with `surface`.
    void drawCard(const Layout& layout, Color surface);
    // The message, wrapped to the card width and placed by `layout`.
    void drawText(const Layout& layout, const std::string& text, Color color, int size = TEXT_SIZE);
}

#endif

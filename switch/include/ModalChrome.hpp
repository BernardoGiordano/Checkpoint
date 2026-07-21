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
    // it reads as a dialog rather than a floating chip.
    constexpr int CARD_X = 192, CARD_Y = 190, CARD_W = 896, CARD_H = 300;
    constexpr int PAD = 32;

    // Message body: wrapped to this width, smaller than the old size-28 so long
    // strings fit inside the card instead of bleeding past its edges.
    constexpr int TEXT_SIZE  = 24;
    constexpr int TEXT_X     = CARD_X + PAD;     // 224
    constexpr int TEXT_MAX_W = CARD_W - 2 * PAD; // 832

    // Button row along the bottom of the card: one wide button, or a split pair.
    constexpr int BTN_H       = 56;
    constexpr int BTN_SIZE    = 24;
    constexpr int BTN_Y       = CARD_Y + CARD_H - 24 - BTN_H; // 410
    constexpr int BTN_WIDE_X  = CARD_X + PAD;                 // 224
    constexpr int BTN_WIDE_W  = CARD_W - 2 * PAD;             // 832
    constexpr int BTN_GAP     = 16;
    constexpr int BTN_HALF_W  = (BTN_WIDE_W - BTN_GAP) / 2;        // 408
    constexpr int BTN_LEFT_X  = BTN_WIDE_X;                        // 224
    constexpr int BTN_RIGHT_X = BTN_WIDE_X + BTN_HALF_W + BTN_GAP; // 648

    // Full-screen dimmed scrim behind the card.
    void dim(void);
    // The card itself, filled with `surface`.
    void drawCard(Color surface);
    // Wrap `text` to the card width and draw it centered in the band between the
    // card top and the button row.
    void drawText(const std::string& text, Color color, int size = TEXT_SIZE);
}

#endif

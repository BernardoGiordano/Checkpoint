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

#include "MessageOverlay.hpp"
#include "ModalChrome.hpp"
#include "textpool.hpp"

MessageOverlay::MessageOverlay(Screen& screen, const std::string& mtext, const Style& style) : Overlay(screen), mStyle(style)
{
    mButton = std::make_unique<Clickable>(
        ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, style.buttonBg, style.buttonFg, " OK", true);
    mButton->selected(true);
    mText = StringUtils::wrap(mtext, SIZE, 220);
    // Center the text between the header line (if any) and the button row.
    const int top = mStyle.header.empty() ? 54 : 74;
    const int h   = mStyle.header.empty() ? 88 : 68;
    mPosx         = ceilf((320 - StringUtils::textWidth(mText, SIZE)) / 2);
    mPosy         = top + ceilf((h - StringUtils::textHeight(mText, SIZE)) / 2);
}

void MessageOverlay::drawTop(void) const
{
    ModalChrome::dimTop();
}

void MessageOverlay::drawBottom(void) const
{
    ModalChrome::dimBottom();
    ModalChrome::drawCard(mStyle.outline);
    if (!mStyle.header.empty()) {
        TextPool::get().draw(mStyle.header, 46, 62, 0.42f, mStyle.headerColor);
    }
    TextPool::get().draw(mText, mPosx, mPosy, SIZE, COLOR_TEXT);
    mButton->draw(0.55f, mStyle.ring);
    Gui::drawPulsingOutline(ModalChrome::BTN_WIDE_X, ModalChrome::BTN_Y, ModalChrome::BTN_WIDE_W, ModalChrome::BTN_H, 2, mStyle.ring);
}

void MessageOverlay::update(const InputState& input)
{
    (void)input;
    if (mButton->released() || (hidKeysDown() & KEY_A) || (hidKeysDown() & KEY_B)) {
        screen.removeOverlay();
    }
}

InfoOverlay::InfoOverlay(Screen& screen, const std::string& mtext)
    : MessageOverlay(screen, mtext, Style{COLOR_LINE, COLOR_ACCENT, COLOR_WHITE, COLOR_RING, "", 0})
{
}

ErrorOverlay::ErrorOverlay(Screen& screen, Result res, const std::string& mtext)
    : MessageOverlay(
          screen, mtext, Style{COLOR_DANGER, COLOR_DANGER, COLOR_WHITE, COLOR_DANGER, StringUtils::format("Error: 0x%08lX", res), COLOR_DANGER})
{
}

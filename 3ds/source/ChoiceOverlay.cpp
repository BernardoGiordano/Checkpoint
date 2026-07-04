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

#include "ChoiceOverlay.hpp"
#include "ModalChrome.hpp"
#include "textpool.hpp"

ChoiceOverlay::ChoiceOverlay(Screen& screen, const std::string& text, Button first, Button second, u32 dismissKeys)
    : Overlay(screen), mText(text), mButtons{std::move(first), std::move(second)}, mHid(2, 2), mDismissKeys(dismissKeys)
{
    for (size_t i = 0; i < 2; i++) {
        mClick[i] = std::make_unique<Clickable>(
            mButtons[i].x, ModalChrome::BTN_Y, ModalChrome::BTN_HALF_W, ModalChrome::BTN_H, mButtons[i].bg, mButtons[i].fg, mButtons[i].label, true);
    }
}

void ChoiceOverlay::drawTop(void) const
{
    ModalChrome::dimTop();
}

void ChoiceOverlay::drawBottom(void) const
{
    ModalChrome::dimBottom();
    ModalChrome::drawCard(COLOR_LINE);
    TextPool::get().drawCentered(mText, 0, 320, 84, 0.55f, COLOR_TEXT);

    mClick[0]->draw(0.55f, COLOR_RING);
    mClick[1]->draw(0.55f, COLOR_RING);

    const size_t sel = mHid.index();
    Gui::drawPulsingOutline(mButtons[sel].x, ModalChrome::BTN_Y, ModalChrome::BTN_HALF_W, ModalChrome::BTN_H, 2, COLOR_RING);
}

void ChoiceOverlay::update(const InputState& input)
{
    (void)input;
    u32 kDown = hidKeysDown();
    mHid.update(2);

    mHid.index(mClick[0]->held() ? 0 : mClick[1]->held() ? 1 : mHid.index());
    mClick[0]->selected(mHid.index() == 0);
    mClick[1]->selected(mHid.index() == 1);

    for (size_t i = 0; i < 2; i++) {
        if (mClick[i]->released() || (kDown & mButtons[i].extraKeys) || ((kDown & KEY_A) && mHid.index() == i)) {
            mButtons[i].action();
            return;
        }
    }
    if (kDown & mDismissKeys) {
        screen.removeOverlay();
    }
}

YesNoOverlay::YesNoOverlay(
    Screen& screen, const std::string& mtext, const std::function<void()>& callbackYes, const std::function<void()>& callbackNo)
    : ChoiceOverlay(screen, mtext, Button{" Confirm", ModalChrome::BTN_RIGHT_X, COLOR_ACCENT, COLOR_WHITE, 0, callbackYes},
          Button{" Cancel", ModalChrome::BTN_LEFT_X, COLOR_RAISED, COLOR_TEXT, KEY_B, callbackNo})
{
}

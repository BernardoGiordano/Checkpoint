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

#ifndef CHOICEOVERLAY_HPP
#define CHOICEOVERLAY_HPP

#include "Overlay.hpp"
#include "clickable.hpp"
#include "colors.hpp"
#include "hid.hpp"
#include <functional>
#include <memory>
#include <string>

// Two-button modal: one prompt line, a left/right button pair, d-pad + touch
// selection, A activates the highlighted button. Owns the whole chrome and the
// update loop; YesNoOverlay and TransferMenuOverlay are just configurations.
class ChoiceOverlay : public Overlay {
public:
    // A button in hid-index order; `x` places it (ModalChrome::BTN_LEFT_X or
    // BTN_RIGHT_X) and `extraKeys` triggers it regardless of the highlight.
    struct Button {
        std::string label;
        int x;
        u32 bg, fg;
        u32 extraKeys;
        std::function<void()> action;
    };

    ChoiceOverlay(Screen& screen, const std::string& text, Button first, Button second, u32 dismissKeys = 0);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;

private:
    std::string mText;
    Button mButtons[2];
    std::unique_ptr<Clickable> mClick[2];
    Hid<HidDirection::HORIZONTAL, HidDirection::HORIZONTAL> mHid;
    u32 mDismissKeys;
};

class YesNoOverlay : public ChoiceOverlay {
public:
    YesNoOverlay(Screen& screen, const std::string& mtext, const std::function<void()>& callbackYes, const std::function<void()>& callbackNo);
};

#endif

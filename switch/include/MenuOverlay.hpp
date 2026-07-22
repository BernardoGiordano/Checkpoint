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

#ifndef MENUOVERLAY_HPP
#define MENUOVERLAY_HPP

#include "Overlay.hpp"
#include <functional>
#include <string>
#include <vector>

// A generic vertical action menu, same panel chrome as the script picker: a
// fixed list of labelled items, each with a callback fired (after the menu
// dismisses) when A is pressed on it. Raised by the main screen's Minus button
// as a touch-free home for actions that would otherwise fight the backup
// buttons for a slot (Scripts, Settings, ...).
class MenuOverlay : public Overlay {
public:
    struct Item {
        std::string label;
        std::function<void()> action;
    };

    MenuOverlay(Screen& screen, const std::string& prompt, std::vector<Item> items);
    void draw(void) const override;
    void update(const InputState& input) override;

private:
    std::string mPrompt;
    std::vector<Item> mItems;
    int mCursor = 0;
    int mScroll = 0;
};

#endif

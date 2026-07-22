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

#include "ListPickerOverlay.hpp"
#include <functional>
#include <string>
#include <vector>

// A generic vertical action menu over the shared ListPickerOverlay chrome: a
// fixed list of labelled items, each with a callback fired (after the menu
// dismisses) when A is pressed on it. It is the main screen's SELECT entry
// point — a touch-free home for actions that would otherwise fight the backup
// buttons for a slot (Scripts, Settings, ...), and grows without stealing more
// physical buttons.
class MenuOverlay : public ListPickerOverlay {
public:
    struct Item {
        std::string label;
        std::function<void()> action;
    };

    MenuOverlay(Screen& screen, const std::string& prompt, std::vector<Item> items);
    void update(const InputState& input) override;

protected:
    int rowCount(void) const override;
    void drawEmptyMessage(void) const override;
    void drawRowContent(int k, int rowY, bool selected) const override;
    std::string bottomHints(void) const override;

private:
    std::vector<Item> mItems;
};

#endif

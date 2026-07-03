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

#ifndef TITLEPICKEROVERLAY_HPP
#define TITLEPICKEROVERLAY_HPP

#include "Overlay.hpp"
#include <functional>
#include <string>
#include <switch.h>
#include <utility>
#include <vector>

// A scrollable title chooser raised by Settings > Save folders' "+ Add title"
// row. A selects a title and fires onPick; B dismisses. Styled with the shared
// design tokens.
class TitlePickerOverlay : public Overlay {
public:
    TitlePickerOverlay(Screen& screen, const std::string& heading, std::vector<std::pair<u64, std::string>> items, std::function<void(u64)> onPick);
    void draw(void) const override;
    void update(const InputState& input) override;

private:
    std::string mHeading;
    std::vector<std::pair<u64, std::string>> mItems;
    std::function<void(u64)> mOnPick;
    int mCursor = 0;
    int mScroll = 0;
};

#endif // TITLEPICKEROVERLAY_HPP

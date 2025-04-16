/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#ifndef SCROLLABLE_HPP
#define SCROLLABLE_HPP

#include "clickable.hpp"
#include "colors.hpp"
#include "hid.hpp"
#include "iscrollable.hpp"
#include "main.hpp"
#include <citro2d.h>
#include <vector>

class Scrollable : public IScrollable<u32> {
public:
    Scrollable(int x, int y, u32 w, u32 h, size_t visibleEntries) : IScrollable(x, y, w, h, visibleEntries), mHid(visibleEntries, 1) {}

    virtual ~Scrollable() = default;

    void c2dText(size_t i, const std::string& v);
    void draw(bool condition = false) override;
    void setIndex(size_t i);
    void push_back(u32 color, u32 colorMessage, const std::string& message, bool selected) override;
    void resetIndex(void) override;
    void updateSelection(void) override;

protected:
    Hid<HidDirection::VERTICAL, HidDirection::HORIZONTAL> mHid;
};

#endif

/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#ifndef YESNOOVERLAY_HPP
#define YESNOOVERLAY_HPP

#include "Overlay.hpp"
#include "SDLHelper.hpp"
#include "clickable.hpp"
#include "colors.hpp"
#include "hid.hpp"
#include <functional>
#include <memory>
#include <string>

class Clickable;

class YesNoOverlay : public Overlay {
public:
    YesNoOverlay(Screen& screen, const std::string& mtext, const std::function<void()>& callbackYes, const std::function<void()>& callbackNo);
    ~YesNoOverlay(void) {}
    void draw(void) const override;
    void update(touchPosition* touch) override;

private:
    u32 textw, texth;
    std::string text;
    std::unique_ptr<Clickable> buttonYes, buttonNo;
    Hid<HidDirection::HORIZONTAL, HidDirection::HORIZONTAL> hid;
    std::function<void()> yesFunc, noFunc;
};

#endif

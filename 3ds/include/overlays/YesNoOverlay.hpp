/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#include <functional>
#include <string>

#include "Screen.hpp"
#include "clickable.hpp"
#include "dualscreen.hpp"
#include "ihid.hpp"

class YesNoOverlay : public DualScreenOverlay {
public:
    YesNoOverlay(Screen& screen, const std::string& mtext, const std::function<void(InputDataHolder&)>& callbackYes, const std::function<void(InputDataHolder&)>& callbackNo);
    ~YesNoOverlay();

private:
    void drawTop(DrawDataHolder& d) const override;
    void drawBottom(DrawDataHolder& d) const override;
    void update(InputDataHolder& input) override;

    u32 posx, posy;
    C2D_TextBuf textBuf;
    C2D_Text text;
    Clickable buttonYes, buttonNo;
    Hid<HidDirection::HORIZONTAL, HidDirection::HORIZONTAL> hid;
    std::function<void(InputDataHolder&)> yesFunc, noFunc;
};

#endif

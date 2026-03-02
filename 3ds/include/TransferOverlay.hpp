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

#ifndef TRANSFEROVERLAY_HPP
#define TRANSFEROVERLAY_HPP

#include "Overlay.hpp"
#include "clickable.hpp"
#include "colors.hpp"
#include "gui.hpp"
#include "hid.hpp"
#include <functional>
#include <memory>
#include <string>

class TransferMenuOverlay : public Overlay {
public:
    TransferMenuOverlay(Screen& screen, const std::function<void()>& callbackSend, const std::function<void()>& callbackReceive);
    ~TransferMenuOverlay(void);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;

private:
    u32 posx, posy;
    C2D_TextBuf textBuf;
    C2D_Text text;
    std::unique_ptr<Clickable> buttonSend, buttonReceive;
    Hid<HidDirection::HORIZONTAL, HidDirection::HORIZONTAL> hid;
    std::function<void()> sendFunc, receiveFunc;
};

class ReceiveOverlay : public Overlay {
public:
    ReceiveOverlay(Screen& screen);
    ~ReceiveOverlay(void);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;

private:
    C2D_TextBuf textBuf;
};

#endif

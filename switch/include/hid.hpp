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

#ifndef HID_HPP
#define HID_HPP

#include "ihid.hpp"
#include "InputState.hpp"
#include <switch.h>

#define DELAY_TICKS 2500000

template <HidDirection ListDirection, HidDirection PageDirection>
class Hid : public IHid<ListDirection, PageDirection, DELAY_TICKS> {
public:
    Hid(size_t entries, size_t columns) : IHid<ListDirection, PageDirection, DELAY_TICKS>(entries, columns), input(nullptr) {}
    Hid(size_t entries, size_t columns, const InputState& _input) : IHid<ListDirection, PageDirection, DELAY_TICKS>(entries, columns), input(&_input) {}

private:
    const InputState* input;

    bool downDown() const override { return input->kDown & HidNpadButton_Down; }
    bool upDown() const override { return input->kDown & HidNpadButton_Up; }
    bool leftDown() const override { return input->kDown & HidNpadButton_Left; }
    bool rightDown() const override { return input->kDown & HidNpadButton_Right; }
    bool leftTriggerDown() const override { return input->kDown & HidNpadButton_L; }
    bool rightTriggerDown() const override { return input->kDown & HidNpadButton_R; }

    bool downHeld() const override { return input->kHeld & HidNpadButton_Down; }
    bool upHeld() const override { return input->kHeld & HidNpadButton_Up; }
    bool leftHeld() const override { return input->kHeld & HidNpadButton_Left; }
    bool rightHeld() const override { return input->kHeld & HidNpadButton_Right; }
    bool leftTriggerHeld() const override { return input->kHeld & HidNpadButton_L; }
    bool rightTriggerHeld() const override { return input->kHeld & HidNpadButton_R; }
    u64 tick() const override { return armGetSystemTick(); }
};

#endif

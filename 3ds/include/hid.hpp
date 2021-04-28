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
#include <3ds.h>

#define DELAY_TICKS 50000000

template <HidDirection ListDirection, HidDirection PageDirection>
class Hid : public IHid<ListDirection, PageDirection, DELAY_TICKS> {
public:
    Hid(size_t entries, size_t columns) : IHid<ListDirection, PageDirection, DELAY_TICKS>(entries, columns) {}

private:
    bool downDown() const override { return hidKeysDown() & KEY_DOWN; }
    bool upDown() const override { return hidKeysDown() & KEY_UP; }
    bool leftDown() const override { return hidKeysDown() & KEY_LEFT; }
    bool rightDown() const override { return hidKeysDown() & KEY_RIGHT; }
    bool leftTriggerDown() const override { return hidKeysDown() & KEY_L || hidKeysDown() & KEY_ZL; }
    bool rightTriggerDown() const override { return hidKeysDown() & KEY_R || hidKeysDown() & KEY_ZR; }
    bool downHeld() const override { return hidKeysHeld() & KEY_DOWN; }
    bool upHeld() const override { return hidKeysHeld() & KEY_UP; }
    bool leftHeld() const override { return hidKeysHeld() & KEY_LEFT; }
    bool rightHeld() const override { return hidKeysHeld() & KEY_RIGHT; }
    bool leftTriggerHeld() const override { return hidKeysHeld() & KEY_L || hidKeysHeld() & KEY_ZL; }
    bool rightTriggerHeld() const override { return hidKeysHeld() & KEY_R || hidKeysHeld() & KEY_ZR; }
    u64 tick() const override { return svcGetSystemTick(); }
};

#endif

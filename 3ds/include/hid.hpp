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

class HidHorizontal : public IHidHorizontal {
public:
    HidHorizontal(size_t entries, size_t columns) : IHidHorizontal(entries, columns) { mDelayTicks = 35000000; }

    virtual ~HidHorizontal(void) {}

    u64 down(void) override { return (u64)hidKeysDown(); }

    u64 held(void) override { return (u64)hidKeysHeld(); }

    u64 tick(void) override { return svcGetSystemTick(); }

    u64 _KEY_ZL(void) override { return KEY_ZL; }
    u64 _KEY_ZR(void) override { return KEY_ZR; }
    u64 _KEY_LEFT(void) override { return KEY_LEFT; }
    u64 _KEY_RIGHT(void) override { return KEY_RIGHT; }
    u64 _KEY_UP(void) override { return KEY_UP; }
    u64 _KEY_DOWN(void) override { return KEY_DOWN; }
};

class HidVertical : public IHidVertical {
public:
    HidVertical(size_t entries, size_t columns) : IHidVertical(entries, columns) { mDelayTicks = 50000000; }

    virtual ~HidVertical(void) {}

    u64 down(void) override { return (u64)hidKeysDown(); }

    u64 held(void) override { return (u64)hidKeysHeld(); }

    u64 tick(void) override { return svcGetSystemTick(); }

    u64 _KEY_ZL(void) override { return KEY_ZL; }
    u64 _KEY_ZR(void) override { return KEY_ZR; }
    u64 _KEY_LEFT(void) override { return KEY_LEFT; }
    u64 _KEY_RIGHT(void) override { return KEY_RIGHT; }
    u64 _KEY_UP(void) override { return KEY_UP; }
    u64 _KEY_DOWN(void) override { return KEY_DOWN; }
};

#endif
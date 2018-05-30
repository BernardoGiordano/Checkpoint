/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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

#include <switch.h>
#include "ihid.hpp"

class Hid : public IHid
{
public:
    Hid(size_t entries, size_t columns)
    : IHid(entries, columns) { }

    virtual ~Hid(void) { }

    u64 down(void) override
    {
        return hidKeysDown(CONTROLLER_P1_AUTO);
    }

    u64 held(void) override
    {
        return hidKeysHeld(CONTROLLER_P1_AUTO);
    }

    void update(size_t count)
    {
        IHid::update(count);
        if (mSleep)
        {
            svcSleepThread(FASTSCROLL_WAIT);
        }
    }

    u64 _KEY_ZL(void) override { return KEY_ZL; }
    u64 _KEY_ZR(void) override { return KEY_ZR; }
    u64 _KEY_LEFT(void) override { return KEY_LEFT; }
    u64 _KEY_RIGHT(void) override { return KEY_RIGHT; }
    u64 _KEY_UP(void) override { return KEY_UP; }
    u64 _KEY_DOWN(void) override { return KEY_DOWN; }
};

#endif
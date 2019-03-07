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

#ifndef IHID_HPP
#define IHID_HPP

#include <cstdint>
#include <cmath>
#include "common.hpp"

typedef uint64_t u64;

class IHid 
{
public:
    IHid(size_t entries, size_t columns)
    : mMaxVisibleEntries(entries), mColumns(columns)
    {
        reset();
        mMaxPages = 0;
        mCurrentTime = 0;
        mLastTime = 0;
        mDelayTicks = 1e10;
    }

    virtual ~IHid(void) { }

    size_t fullIndex(void);
    size_t index(void);
    void   index(size_t v);
    size_t maxEntries(size_t max);
    size_t maxVisibleEntries(void);
    int    page(void);
    void   page(int v);
    void   reset(void);

protected:
    void page_back(void);
    void page_forward(void);
    virtual void update(size_t count) = 0;

    virtual u64 down(void) = 0;
    virtual u64 held(void) = 0;
    virtual u64 tick(void) = 0;
    virtual u64 _KEY_ZL(void) = 0;
    virtual u64 _KEY_ZR(void) = 0;
    virtual u64 _KEY_LEFT(void) = 0;
    virtual u64 _KEY_RIGHT(void) = 0;
    virtual u64 _KEY_UP(void) = 0;
    virtual u64 _KEY_DOWN(void) = 0;

    size_t mIndex;
    int    mPage;
    size_t mMaxPages;
    size_t mMaxVisibleEntries;
    size_t mColumns;
    u64    mCurrentTime;
    u64    mLastTime;
    u64    mDelayTicks;
};

class IHidVertical : public IHid
{
public:
    IHidVertical(size_t entries, size_t columns)
    : IHid(entries, columns) { }

    virtual ~IHidVertical(void) { }

    void update(size_t count) override;
};

class IHidHorizontal : public IHid
{
public:
    IHidHorizontal(size_t entries, size_t columns)
    : IHid(entries, columns) { }

    virtual ~IHidHorizontal(void) { }

    void update(size_t count) override;
};

#endif

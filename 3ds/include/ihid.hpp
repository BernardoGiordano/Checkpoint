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

#ifndef HID_HPP
#define HID_HPP

#include "inputdata.hpp"

enum class HidDirection
{
    VERTICAL,
    HORIZONTAL
};

class HidBase {
protected:
    bool downDownRepeat(const InputDataHolder& input) const;
    bool upDownRepeat(const InputDataHolder& input) const;
    bool leftDownRepeat(const InputDataHolder& input) const;
    bool rightDownRepeat(const InputDataHolder& input) const;
    bool leftTriggerDownRepeat(const InputDataHolder& input) const;
    bool rightTriggerDownRepeat(const InputDataHolder& input) const;
};

template <HidDirection GrowDirection, HidDirection PageDirection>
class Hid : HidBase {
public:
    Hid(const size_t visiblePerChunk, const size_t visibleChunks)
    :
    mMaxVisibleEntries(visiblePerChunk * visibleChunks),
    mVisiblePerChunk(visiblePerChunk),
    mVisibleChunks(visibleChunks)
    {
        mIndexInVisible = 0;
        mPage           = 0;
        mMaxPages       = 0;
    }

    size_t fullIndex() const { return mIndexInVisible + mPage * mMaxVisibleEntries; }
    size_t index() const { return mIndexInVisible; }
    void index(size_t v) { mIndexInVisible = v; }
    size_t maxVisibleEntries() const { return mMaxVisibleEntries; }
    size_t page() const { return mPage; }
    void page(size_t v) { mPage = v; }
    size_t maxEntries(size_t count) const
    {
        // if we are not on the last possible page for this value of count
        // then we can fit an entire page
        // otherwise, we can fit the remaining amount
        return ((count / mMaxVisibleEntries) > mPage) ? mMaxVisibleEntries : count % mMaxVisibleEntries;
    }
    void pageBack()
    {
        /*
        if (mPage > 0)
        {
            mPage--;
        }
        else if (mPage == 0)
        {
            mPage = mMaxPages - 1;
        }
        */

        if (mPage == 0)
        {
            mPage = mMaxPages;
        }
        --mPage;
    }
    void pageForward()
    {
        /*
        if (mPage < (int)mMaxPages - 1)
        {
            mPage++;
        }
        else if (mPage == (int)mMaxPages - 1)
        {
            mPage = 0;
        }
        */

        mPage++;
        if (mPage == mMaxPages) {
            mPage = 0;
        }
    }
    void reset()
    {
        mIndexInVisible = 0;
        mPage  = 0;
    }
    void correctIndex(size_t count)
    {
        if (mIndexInVisible >= maxEntries(count))
        {
            mIndexInVisible = maxEntries(count) - 1;
        }
    }

    void update(const InputDataHolder& input, size_t count);

private:
    const size_t mMaxVisibleEntries;
    const size_t mVisiblePerChunk;
    const size_t mVisibleChunks;

    size_t mIndexInVisible;
    size_t mPage;
    size_t mMaxPages;
};

#include "ihid.tcc"

#endif

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

#ifndef ISCROLLABLE_HPP
#define ISCROLLABLE_HPP

#include "iclickable.hpp"

template<typename T>
class IScrollable 
{
public:
    IScrollable(int x, int y, u16 w, u16 h, size_t visibleEntries)
    : mx(x), my(y), mw(w), mh(h), mVisibleEntries(visibleEntries)
    {
        mIndex = 0;
        mPage = 0;
    }

    virtual ~IScrollable(void)
    {
        flush();
    }

    virtual void draw(bool condition = false) = 0;
    virtual void push_back(T color, T colorMessage, const std::string& message, bool selected) = 0;
    virtual void updateSelection(void) = 0;

    std::string cellName(size_t index)
    {
        return mCells.at(index)->text();
    }

    void flush(void)
    {
        for (size_t i = 0; i < size(); i++)
        {
            delete mCells[i];
        }
        mCells.clear();
    }

    size_t size(void)
    {
        return mCells.size();
    }

    size_t maxVisibleEntries(void)
    {
        return (size() - mPage*mVisibleEntries) > mVisibleEntries ? mVisibleEntries : size() - mPage*mVisibleEntries;
    }

    size_t index(void)
    {
        return mIndex + mPage*mVisibleEntries;
    }

    int page(void)
    {
        return mPage;
    }

    virtual void resetIndex(void)
    {
        mIndex = 0;
        mPage = 0;
    }

    void index(size_t i)
    {
        mPage = i / mVisibleEntries;
        mIndex = i - mPage * mVisibleEntries;
    }

    size_t visibleEntries(void)
    {
        return mVisibleEntries;
    }

    void selectRow(size_t i, bool selected)
    {
        mCells.at(i)->selected(selected);
    }

protected:
    int    mx;
    int    my;
    u16    mw;
    u16    mh;
    size_t mVisibleEntries;
    size_t mIndex;
    int    mPage;
    std::vector
    <IClickable<T>*> mCells;
};

#endif
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

#ifndef ICLICKABLE_HPP
#define ICLICKABLE_HPP

#include <string>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

template<typename T>
class IClickable
{
public:
    IClickable(int x, int y, u16 w, u16 h, T colorBg, T colorText, const std::string& message, bool centered)
    : mx(x), my(y), mw(w), mh(h), mColorBg(colorBg), mColorText(colorText), mText(message), mCentered(centered)
    {
        mOldPressed = false;
    }
    
    virtual ~IClickable(void) { }

    virtual void draw(void) = 0;
    virtual void draw(float size) = 0;
    virtual bool held(void) = 0;
    virtual bool released(void) = 0;
    
    void invertColors(void)
    {
        T tmp = mColorBg;
        mColorBg = mColorText;
        mColorText = tmp;
    }

    void setColors(T bg, T text)
    {
        mColorBg = bg;
        mColorText = text;
    }

    std::string text(void)
    {
        return mText;
    }

    void text(const std::string& v)
    {
        mText = v;
    }

protected: 
    int         mx;
    int         my;
    u16         mw;
    u16         mh;
    T           mColorBg;
    T           mColorText;
    std::string mText;
    bool        mCentered;
    bool        mOldPressed;
};

#endif
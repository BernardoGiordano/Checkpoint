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

#ifndef KEYBOARDMANAGER_HPP
#define KEYBOARDMANAGER_HPP

#include <locale>
#include <string>
#include <switch.h>
#include <utility>
#include <vector>
#include "clickable.hpp"
#include "SDLHelper.hpp"
#include "util.hpp"

class HbkbdButton : public Clickable 
{
public:
    HbkbdButton(u32 x, u32 y, u16 w, u16 h, SDL_Color colorBg, SDL_Color colorText, const std::string& message, bool centered)
    : Clickable(x, y, w, h, colorBg, colorText, message, centered)
    {
        mSelected = false;
    }

    void selected(bool v)
    {
        mSelected = v;
    }

    void draw(void)
    {
        Clickable::draw();
        // outline
        if (mSelected)
        {
            SDL_Color color = SDL_MakeColour(138, 138, 138, 255);
            static const size_t size = 2;
            SDL_DrawRect(mx - size, my - size, mw + 2*size, size, color); // top
            SDL_DrawRect(mx - size, my, size, mh, color); // left
            SDL_DrawRect(mx + mw, my, size, mh, color); // right
            SDL_DrawRect(mx - size, my + mh, mw + 2*size, size, color); // bottom
        }
    }

protected:
    bool mSelected;
};

// hardcoded, but it's not going to change often
#define INDEX_BACK 44
#define INDEX_RETURN 45
#define INDEX_OK 46
#define INDEX_CAPS 47
#define INDEX_SPACE 48

class KeyboardManager
{
public:
    static KeyboardManager& get(void)
    {
        static KeyboardManager mSingleton;
        return mSingleton;
    }

    KeyboardManager(KeyboardManager const&) = delete;
    void operator=(KeyboardManager const&) = delete;

    std::pair<bool, std::string> keyboard(const std::string& suggestion);

    static const size_t CUSTOM_PATH_LEN = 49;

private:
    KeyboardManager(void);
    virtual ~KeyboardManager(void);

    void hid(size_t& currentEntry);
    bool logic(std::string& str, size_t i);

    const u32 buttonSpacing = 4;
    const u32 normalWidth = 92;
    const u32 bigWidth = 116;
    const u32 height = 60;
    const u32 margintb = 20;
    const u32 marginlr = 54;
    const u32 starty = 720 - 356 + margintb;

    const std::string letters = "1234567890@qwertyuiop+asdfghjkl_:zxcvbnm,.-/";
    std::vector<HbkbdButton*> buttons;
    size_t prevSelectedButtonIndex;
};

#endif
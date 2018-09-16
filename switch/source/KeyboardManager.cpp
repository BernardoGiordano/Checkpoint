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

#include "KeyboardManager.hpp"

void KeyboardManager::hid(size_t& currentEntry)
{
    static const size_t columns = 11;

    u64 kHeld = hidKeysHeld(CONTROLLER_P1_AUTO);
    bool sleep = false;

    if (kHeld & KEY_LEFT)
    {
        switch (currentEntry)
        {
            case 0: // 1
            case columns: // q
            case columns*2: // a
            case columns*3: // z
            case INDEX_CAPS:
                break;
            case INDEX_BACK: // back -> @
                currentEntry = columns-1;
                break;
            case INDEX_RETURN: // return -> +
                currentEntry = columns*2-1;
                break;
            case INDEX_OK: // OK -> space
                currentEntry = INDEX_SPACE;
                break;
            case INDEX_SPACE: // space -> caps
                currentEntry = INDEX_CAPS;
                break;
            default:
                currentEntry--;
        }
        sleep = true;
    }
    else if (kHeld & KEY_RIGHT)
    {
        switch (currentEntry)
        {
            case 10: // @ -> back
                currentEntry = INDEX_BACK;
                break;
            case 21: // + -> return
            case 32: // : -> return
                currentEntry = INDEX_RETURN;
                break;
            case 43: // /-> OK
            case INDEX_SPACE: // space -> OK
                currentEntry = INDEX_OK;
                break; 
            case INDEX_CAPS: // caps -> space
                currentEntry = INDEX_SPACE;
                break;
            case INDEX_BACK:
            case INDEX_RETURN:
            case INDEX_OK:
                break;
            default:
                currentEntry++;
        }
        sleep = true;
    }
    else if (kHeld & KEY_UP)
    {
        switch (currentEntry)
        {
            case 0 ... 10: // 1 to @
            case INDEX_BACK:
                break;
            case INDEX_CAPS: // caps -> x
                currentEntry = 34;
                break;
            case INDEX_SPACE: // space -> .
                currentEntry = 41;
                break;
            case INDEX_RETURN: // return -> back
                currentEntry = INDEX_BACK;
                break;
            case INDEX_OK: // OK -> return
                currentEntry = INDEX_RETURN;
                break;
            default:
                currentEntry -= 11;
        }
        sleep = true;
    }
    else if (kHeld & KEY_DOWN)
    {
        switch (currentEntry)
        {
            case INDEX_CAPS:
            case INDEX_SPACE:
            case INDEX_OK:
                break;
            case 33 ... 35: // z x c -> caps
                currentEntry = INDEX_CAPS;
                break;
            case 36 ... 43: // v b n m , . - / -> space 
                currentEntry = INDEX_SPACE;
                break;
            case INDEX_BACK: // back -> return
                currentEntry = INDEX_RETURN;
                break;
            case INDEX_RETURN: // return -> OK
                currentEntry = INDEX_OK;
                break;
            default:
                currentEntry += 11;
        }
        sleep = true;
    }

    if (sleep)
    {
        svcSleepThread(FASTSCROLL_WAIT);
    }
}

KeyboardManager::KeyboardManager(void)
{
    buttons.clear();

    // fill with the above characters
    for (size_t i = 0; i < 4; i++)
    {
        for (size_t j = 0; j < 11; j++)
        {
            HbkbdButton* button = new HbkbdButton(
                marginlr + (buttonSpacing + normalWidth) * j, 
                starty + (buttonSpacing + height) * i,
                normalWidth,
                height, 
                COLOR_GREY_DARK, 
                COLOR_WHITE, 
                letters.substr(i*11+j, 1),
                true
            );
            buttons.push_back(button);
        }
    }

    HbkbdButton* backspace = new HbkbdButton(
        marginlr + (buttonSpacing + normalWidth) * 11, 
        starty,
        bigWidth,
        height, 
        COLOR_WHITE,
        COLOR_BLACK,
        "back",
        true
    );
    buttons.push_back(backspace);

    HbkbdButton* returnb = new HbkbdButton(
        marginlr + (buttonSpacing + normalWidth) * 11, 
        starty + height + 4,
        bigWidth,
        height * 2 + 4, 
        COLOR_GREY_MEDIUM, 
        COLOR_GREY_LIGHT,  
        "return",
        true
    );
    buttons.push_back(returnb);

    HbkbdButton* ok = new HbkbdButton(
        marginlr + (buttonSpacing + normalWidth) * 11, 
        starty + height*3 + 4*3,
        bigWidth,
        height * 2 + 4, 
        COLOR_GREEN, 
        COLOR_BLACK, 
        "OK",
        true
    );
    buttons.push_back(ok);

    HbkbdButton* caps = new HbkbdButton(
        marginlr + buttonSpacing + normalWidth, 
        starty + height*4 + 4*4,
        normalWidth,
        height, 
        COLOR_GREY_MEDIUM, 
        COLOR_WHITE, 
        "caps",
        true
    );
    buttons.push_back(caps);

    HbkbdButton* spacebar = new HbkbdButton(
        marginlr + (buttonSpacing + normalWidth) * 3, 
        starty + height*4 + 4*4,
        normalWidth*8 + buttonSpacing*7,
        height, 
        COLOR_GREY_MEDIUM, 
        COLOR_WHITE, 
        "space",
        true
    );
    buttons.push_back(spacebar);

    // set OK button as selected
    buttons.at(INDEX_OK)->selected(true);
    buttons.at(INDEX_OK)->invertColors();
    prevSelectedButtonIndex = INDEX_OK;
}

KeyboardManager::~KeyboardManager(void)
{
    for (size_t i = 0; i < buttons.size(); i++)
    {
        delete buttons[i];
    }
}

bool KeyboardManager::logic(std::string& str, size_t i)
{
    if (buttons.at(i)->text().compare("caps") == 0)
    {
        std::locale loc;
        bool islower = std::islower(buttons.at(11)->text()[0], loc);
        for (size_t t = 0; t < letters.length(); t++)
        {
            std::string l = islower ? std::string(1, std::toupper(letters[t], loc)) : std::string(1, std::tolower(letters[t], loc));
            buttons.at(t)->text(l);
        }
    }
    else if (buttons.at(i)->text().compare("back") == 0)
    {
        if (!str.empty())
        {
            str.erase(str.length() - 1);
        }
    }
    else if (buttons.at(i)->text().compare("space") == 0)
    {
        if (str.length() < CUSTOM_PATH_LEN)
        {
            str.append(" ");
        }
    }
    else if (buttons.at(i)->text().compare("return") == 0)
    {
        //str.append("\n");
    }
    else if (buttons.at(i)->text().compare("OK") == 0)
    {
        return true;
    }
    else if (str.length() < CUSTOM_PATH_LEN)
    {
        str.append(buttons.at(i)->text());
    }

    return false;
}

std::pair<bool, std::string> KeyboardManager::keyboard(const std::string& suggestion)
{
    size_t index;
    std::string str;

    while (appletMainLoop() && !(hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B))
    {
        hidScanInput();
        index = prevSelectedButtonIndex;

        // handle keys
        hid(index);
        if (index != prevSelectedButtonIndex)
        {
            buttons.at(prevSelectedButtonIndex)->selected(false);
            buttons.at(prevSelectedButtonIndex)->invertColors();
            prevSelectedButtonIndex = index;
            buttons.at(index)->selected(true);
            buttons.at(prevSelectedButtonIndex)->invertColors();          
        }

        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A)
        {
            bool ret = logic(str, index);
            if (ret)
            {
                return str.empty() ? std::make_pair(true, suggestion) : std::make_pair(true, str);
            }
        }

        framebuf = gfxGetFramebuffer(&framebuf_width, NULL);
        memset(framebuf, 51, gfxGetFramebufferSize());

        rectangle(marginlr, 140, 1280 - marginlr*2, 84, COLOR_GREY_MEDIUM);
        rectangle(0, starty - margintb, 1280, 356, COLOR_GREY_DARKER);

        u32 texth, counter_width;
        std::string counter = StringUtils::format("Custom name length: %d/%d", str.empty() ? suggestion.length() : str.length(), CUSTOM_PATH_LEN);
        GetTextDimensions(7, " ", NULL, &texth);
        GetTextDimensions(4, counter.c_str(), &counter_width, NULL);
        DrawText(4, 1280 - marginlr - counter_width, 236, COLOR_WHITE, counter.c_str());
        if (str.empty())
        {
            DrawText(7, marginlr*2, 140 + (84 - texth) / 2, COLOR_GREY_LIGHT, suggestion.c_str());            
        }
        else
        {
            DrawText(7, marginlr*2, 140 + (84 - texth) / 2, COLOR_WHITE, str.c_str());   
        }

        for (size_t i = 0, sz = buttons.size(); i < sz; i++)
        {
            if (buttons.at(i)->released())
            {
                bool ret = logic(str, i);
                if (ret)
                {
                    return str.empty() ? std::make_pair(true, suggestion) : std::make_pair(true, str);
                }
            }

            // selection logic
            if (buttons.at(i)->held() && i != prevSelectedButtonIndex)
            {
                buttons.at(prevSelectedButtonIndex)->selected(false);
                buttons.at(prevSelectedButtonIndex)->invertColors();
                prevSelectedButtonIndex = i;
                buttons.at(i)->selected(true);
                buttons.at(i)->invertColors();
            }

            buttons.at(i)->draw();
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    return std::make_pair(false, suggestion);
}

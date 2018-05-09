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

#include "hbkbd.hpp"

static const u32 buttonSpacing = 4;
static const u32 normalWidth = 92;
static const u32 bigWidth = 116;
static const u32 height = 60;
static const u32 margintb = 20;
static const u32 marginlr = 54;
static const u32 starty = 720 - 356 + margintb;

static const std::string letters = "1234567890@qwertyuiop+asdfghjkl_:zxcvbnm,.-/";
static std::vector<Clickable*> buttons;

void hbkbd::init(void)
{
    buttons.clear();

    // fill with the above characters
    for (size_t i = 0; i < 4; i++)
    {
        for (size_t j = 0; j < 11; j++)
        {
            Clickable* button = new Clickable(
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

    Clickable* backspace = new Clickable(
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

    Clickable* returnb = new Clickable(
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

    Clickable* ok = new Clickable(
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

    Clickable* caps = new Clickable(
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

    Clickable* spacebar = new Clickable(
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
}

void hbkbd::exit(void)
{
    for (size_t i = 0, sz = buttons.size(); i < sz; i++)
    {
        delete buttons[i];
    }
}

std::string hbkbd::keyboard(const std::string& suggestion)
{
    std::string str;
    while (appletMainLoop() && !(hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B))
    {
        hidScanInput();

        framebuf = gfxGetFramebuffer(&framebuf_width, NULL);
        memset(framebuf, 51, gfxGetFramebufferSize());

        rectangle(marginlr, 140, 1280 - marginlr*2, 84, COLOR_GREY_MEDIUM);
        rectangle(0, starty - margintb, 1280, 356, COLOR_GREY_DARKER);

        if (str.empty())
        {
            DrawText(font24, marginlr*2, 160, COLOR_GREY_LIGHT, suggestion.c_str());            
        }
        else
        {
            DrawText(font24, marginlr*2, 160, COLOR_WHITE, str.c_str());   
        }

        for (size_t i = 0, sz = buttons.size(); i < sz; i++)
        {
            if (buttons.at(i)->released())
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
                    return str.empty() ? suggestion : str;
                }
                else if (str.length() < CUSTOM_PATH_LEN)
                {
                    str.append(buttons.at(i)->text());
                }
            }
            buttons.at(i)->draw();
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }

    return suggestion;
}
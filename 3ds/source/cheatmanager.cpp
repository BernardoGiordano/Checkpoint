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

#include "cheatmanager.hpp"

#define SELECTED_MAGIC "\uE071 "

static bool mLoaded = false;
static nlohmann::json mCheats;
static bool multiSelected = false;

void CheatManager::init(void)
{
    Gui::updateButtons();

    if (io::fileExists("/3ds/Checkpoint/cheats.json"))
    {
        const std::string path = "/3ds/Checkpoint/cheats.json";
        FILE* in = fopen(path.c_str(), "rt");
        if (in != NULL)
        {
            mCheats = nlohmann::json::parse(in, nullptr, false);
            fclose(in);
            mLoaded = true;
        }
    }
    else
    {
        const std::string path = "romfs:/cheats/cheats.json.bz2";
        // load compressed archive in memory
        FILE* f = fopen(path.c_str(), "rb");
        if (f != NULL)
        {
            fseek(f, 0, SEEK_END);
            u32 size = ftell(f);
            unsigned int destLen = 2*1024*1024;
            char* s = new char[size];
            char* d = new char[destLen]();
            rewind(f);
            fread(s, 1, size, f);
            
            int r = BZ2_bzBuffToBuffDecompress(d, &destLen, s, size, 0, 0);
            if (r == BZ_OK)
            {
                mCheats = nlohmann::json::parse(d);
                mLoaded = true;
            }

            delete[] s;
            delete[] d;
            fclose(f);
        }
    }
    
    Gui::updateButtons();
}

void CheatManager::exit(void)
{

}

bool CheatManager::loaded(void)
{
    return mLoaded;
}

bool CheatManager::availableCodes(const std::string& key)
{
    return mCheats.find(key) != mCheats.end();
}

void CheatManager::manageCheats(const std::string& key)
{
    std::string existingCheat = "";
    FILE* f = fopen(("/cheats/" + key + ".txt").c_str(), "r");
    if (f != NULL)
    {
        fseek(f, 0, SEEK_END);
        u32 size = ftell(f);
        char* s = new char[size];
        rewind(f);
        fread(s, 1, size, f);
        existingCheat = std::string(s);
        delete[] s;
        fclose(f);
    }

    size_t i = 0;
    size_t currentIndex = i;
    Scrollable* s = new Scrollable(2, 2, 396, 220, 11);
    for (auto it = mCheats[key].begin(); it != mCheats[key].end(); ++it)
    {
        std::string value = it.key();
        if (existingCheat.find(value) != std::string::npos)
        {
            value = SELECTED_MAGIC + value;
        }
        s->push_back(COLOR_GREY_DARKER, COLOR_WHITE, value, i == 0);
        i++;
    }

    const float scale = 0.47f;
    C2D_Text multiSelectText, multiDeselectText;
    C2D_TextParse(&multiSelectText, g_dynamicBuf, "\uE003 to select all cheats");
    C2D_TextParse(&multiDeselectText, g_dynamicBuf, "\uE003 to deselect all cheats");
    C2D_TextOptimize(&multiSelectText);
    C2D_TextOptimize(&multiDeselectText);

    while(aptMainLoop())
    {
        hidScanInput();

        if (hidKeysDown() & KEY_B)
        {
            break;
        }

        if (hidKeysDown() & KEY_A)
        {
            std::string cellName = s->cellName(s->index());
            if (cellName.find(SELECTED_MAGIC, 0) == 0)
            {
                // cheat was already selected
                cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
            }
            else
            {
                cellName = SELECTED_MAGIC + cellName;
            }
            s->c2dText(s->index(), cellName);
        }

        if (hidKeysDown() & KEY_Y)
        {
            if (multiSelected)
            {
                for (size_t i = 0; i < s->size(); i++)
                {
                    std::string cellName = s->cellName(i);
                    if (cellName.find(SELECTED_MAGIC, 0) == 0)
                    {
                        cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
                        s->c2dText(i, cellName);
                    }          
                }
                multiSelected = false;
            }
            else
            {
                for (size_t i = 0; i < s->size(); i++)
                {
                    std::string cellName = s->cellName(i);
                    if (cellName.find(SELECTED_MAGIC, 0) != 0)
                    {
                        cellName = SELECTED_MAGIC + cellName;
                        s->c2dText(i, cellName); 
                    }          
                }
                multiSelected = true;
            }   
        }

        s->updateSelection();
        s->selectRow(currentIndex, false);
        s->selectRow(s->index(), true);
        currentIndex = s->index();

        C2D_Text page;
        C2D_TextParse(&page, g_dynamicBuf, StringUtils::format("%d/%d", s->index() + 1, s->size()).c_str());
        C2D_TextOptimize(&page);
        C2D_TextBufClear(g_dynamicBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_SceneBegin(g_top);
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_GREY_DARK);
        s->draw(true);
        C2D_DrawText(&page, C2D_WithColor, ceilf(396 - page.width * scale), 224, 0.5f, scale, scale, COLOR_WHITE);
        C2D_DrawText(multiSelected ? &multiDeselectText : &multiSelectText, C2D_WithColor, 4, 224, 0.5f, scale, scale, COLOR_WHITE);
        Gui::frameEnd();
    }

    Gui::draw();
    if (Gui::askForConfirmation("Do you want to store\nthe cheat file?"))
    {
        save(key, s);
    }

    delete s;
}

void CheatManager::save(const std::string& key, Scrollable* s)
{
    std::string cheatFile = "";
    for (size_t i = 0; i < s->size(); i++)
    {
        std::string cellName = s->cellName(i);
        if (cellName.find(SELECTED_MAGIC, 0) == 0)
        {
            cellName = cellName.substr(strlen(SELECTED_MAGIC), cellName.length());
            cheatFile += cellName + "\n";
            for (auto &it : mCheats[key][cellName])
            {
                cheatFile += it.get<std::string>() + "\n";
            }
            cheatFile += "\n";
        }
    }

    FILE* f = fopen(("/cheats/" + key + ".txt").c_str(), "w");
    if (f != NULL)
    {
        fwrite(cheatFile.c_str(), 1, cheatFile.length(), f);
        fclose(f);
    }
    else
    {
        Gui::showError(errno, "Failed to write cheat file\nto the sd card");
    }
}
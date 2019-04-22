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

static bool mLoaded = false;
static nlohmann::json mCheats;

void CheatManager::init(void)
{
    std::ifstream i("romfs:/cheats/cheats.json");
    i >> mCheats;
    i.close();
    mLoaded = true;
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
    size_t i = 0;
    size_t currentIndex = i;
    Scrollable* s = new Scrollable(2, 10, 396, 220, 11);
    for (auto it = mCheats[key].begin(); it != mCheats[key].end(); ++it)
    {
        s->push_back(COLOR_GREY_DARKER, COLOR_WHITE, it.key(), i == 0);
        i++;
    }

    while(aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & KEY_B)
        {
            break;
        }
        s->updateSelection();
        s->selectRow(currentIndex, false);
        s->selectRow(s->index(), true);
        currentIndex = s->index();

        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, COLOR_GREY_DARK);
        C2D_TextBufClear(g_dynamicBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_SceneBegin(g_top);
        s->draw(true);
        Gui::frameEnd();
    }

    delete s;
}
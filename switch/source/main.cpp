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

#include "main.hpp"

u128 g_currentUId = 0;
u8 g_currentUserIndex = 0;

int main(int argc, char** argv)
{
    Result res = servicesInit();
    if (R_FAILED(res))
    {
        servicesExit();
        return res;
    }

    res = Threads::create((ThreadFunc)loadTitles);
    if (R_FAILED(res))
    {
        Gui::createError(res, "Failed to create thread.");
    }

    int selectionTimer = 0;
    while(appletMainLoop() && !(hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS))
    {
        // get the user IDs
        std::vector<u128> userIds = Account::ids();
        // set g_currentUId to a default user in case we loaded at least one user
        if (g_currentUId == 0 && !userIds.empty()) g_currentUId = userIds.at(0);

        hidScanInput();
        u32 kdown = hidKeysDown(CONTROLLER_P1_AUTO);
        if (kdown & KEY_ZL)
        {
            ++g_currentUserIndex %= userIds.size();
            g_currentUId = userIds.at(g_currentUserIndex);
            Gui::index(TITLES, 0);
        }
        if (kdown & KEY_ZR)
        {
            g_currentUserIndex = g_currentUserIndex - 1 < 0 ? userIds.size() - 1 : g_currentUserIndex - 1;
            g_currentUId = userIds.at(g_currentUserIndex);
            Gui::index(TITLES, 0);
        }

        // handle touchscreen
        touchPosition touch;
        hidTouchRead(&touch, 0);
        for (u8 i = 0; i < userIds.size(); i++)
        {
            if (!Gui::backupScroll() &&
                hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH &&
                touch.px >= u32(1280 - (USER_ICON_SIZE + 4) * (i+1)) &&
                touch.px <= u32(1280 - (USER_ICON_SIZE + 4) * i) &&
                touch.py >= 32 && touch.py <= 32 + USER_ICON_SIZE)
            {
                g_currentUserIndex = i;
                g_currentUId = userIds.at(g_currentUserIndex);
                Gui::index(TITLES, 0);
            }
        }

        if (kdown & KEY_A)
        {
            Gui::backupScroll(true);
            Gui::updateButtonsColor();
            Gui::entryType(CELLS);
        }
        
        if (kdown & KEY_B)
        {
            Gui::index(CELLS, 0);
            Gui::backupScroll(false);
            Gui::updateButtonsColor();
            Gui::entryType(TITLES);
            Gui::clearSelectedEntries();
        }

        if (kdown & KEY_X)
        {
            if (Gui::backupScroll())
            {
                size_t index = Gui::index(CELLS);
                if (index > 0 && Gui::askForConfirmation("Delete selected backup?"))
                {
                    Title title;
                    getTitle(title, g_currentUId, Gui::index(TITLES));
                    std::string path = title.fullPath(index);
                    io::deleteFolderRecursively(path.c_str());
                    refreshDirectories(title.id());
                    Gui::index(CELLS, index - 1);              
                }
            }
        }

        if (kdown & KEY_Y && !(Gui::backupScroll()))
        {
            Gui::addSelectedEntry(Gui::index(TITLES));
        }
        
        if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_Y && !(Gui::backupScroll()))
        {
            selectionTimer++;
        }
        else
        {
            selectionTimer = 0;
        }
        
        if (selectionTimer > 45)
        {
            Gui::clearSelectedEntries();
            for (size_t i = 0, sz = getTitleCount(g_currentUId); i < sz; i++)
            {
                Gui::addSelectedEntry(i);
            }
            selectionTimer = 0;
        }

        if (Gui::isBackupReleased() || (kdown & KEY_L && Gui::backupScroll()))
        {
            if (Gui::multipleSelectionEnabled())
            {
                Gui::resetIndex(CELLS);
                std::vector<size_t> list = Gui::selectedEntries();
                for (size_t i = 0, sz = list.size(); i < sz; i++)
                {
                    io::backup(list.at(i), g_currentUId);
                }
                Gui::clearSelectedEntries();
            }
            else
            {
                io::backup(Gui::index(TITLES), g_currentUId);
            }
        }
        
        if (Gui::isRestoreReleased() || (kdown & KEY_R && Gui::backupScroll()))
        {
            if (Gui::multipleSelectionEnabled())
            {
                Gui::clearSelectedEntries();
            }
            else
            {
                io::restore(Gui::index(TITLES), g_currentUId);
            }
        }

        Gui::updateSelector();
        Gui::draw(g_currentUId);
    }

    Threads::destroy();
    servicesExit();
    return 0;
}
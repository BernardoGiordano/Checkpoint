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

    loadTitles();

    int selectionTimer = 0;
    while(appletMainLoop() && !(hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS))
    {
        // get the user IDs
        std::vector<u128> userIds = Account::ids();
        // set g_currentUId to a default user in case we loaded at least one user
        if (g_currentUId == 0 && !userIds.empty()) g_currentUId = userIds.at(0);

        hidScanInput();
        u32 kdown = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 kheld = hidKeysHeld(CONTROLLER_P1_AUTO);
        if (kdown & KEY_ZL)
        {
            ++g_currentUserIndex %= userIds.size();
            g_currentUId = userIds.at(g_currentUserIndex);
            Gui::index(TITLES, 0);
            Gui::index(CELLS, 0);
            Gui::setPKSMBridgeFlag(false);
        }
        if (kdown & KEY_ZR)
        {
            g_currentUserIndex = g_currentUserIndex - 1 < 0 ? userIds.size() - 1 : g_currentUserIndex - 1;
            g_currentUId = userIds.at(g_currentUserIndex);
            Gui::index(TITLES, 0);
            Gui::index(CELLS, 0);
            Gui::setPKSMBridgeFlag(false);
        }
        // handle PKSM bridge
        if (Configuration::getInstance().isPKSMBridgeEnabled())
        {
            Title title;
            getTitle(title, g_currentUId, Gui::index(TITLES));
            if (!Gui::getPKSMBridgeFlag())
            {
                if ((kheld & KEY_L) && (kheld & KEY_R) && isPKSMBridgeTitle(title.id()))
                {
                    Gui::setPKSMBridgeFlag(true);
                    Gui::updateButtonsColor();
                }
            }
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
                Gui::setPKSMBridgeFlag(false);
            }
        }

        // Handle touching the backup list
        if ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_TOUCH &&
            (int)touch.px > 540 &&
            (int)touch.px < 1046 &&
            (int)touch.py > 462 &&
            (int)touch.py < 692))
        {
            // Activate backup list only if multiple selections are enabled
            if (!Gui::multipleSelectionEnabled())
            {
                Gui::backupScroll(true);
                Gui::updateButtonsColor();
                Gui::entryType(CELLS);
            }
        }

        // Handle pressing A
        // Backup list active:   Backup/Restore
        // Backup list inactive: Activate backup list only if multiple
        //                         selections are enabled
        if (kdown & KEY_A)
        {
            // If backup list is active...
            if (Gui::backupScroll())
            {
                // If the "New..." entry is selected...
                if (0 == Gui::index(CELLS))
                {
                    if (!Gui::getPKSMBridgeFlag())
                    {
                        io::backup(Gui::index(TITLES), g_currentUId);
                    }
                }
                else
                {
                    if (Gui::getPKSMBridgeFlag())
                    {
                        recvFromPKSMBridge(Gui::index(TITLES), g_currentUId);
                    }
                    else
                    {
                         io::restore(Gui::index(TITLES), g_currentUId);
                    }
                }
            }
            else
            {
                // Activate backup list only if multiple selections are not enabled
                if (!Gui::multipleSelectionEnabled())
                {
                    Gui::backupScroll(true);
                    Gui::updateButtonsColor();
                    Gui::entryType(CELLS);
                }
            }
        }

        // Handle pressing B
        if (kdown & KEY_B ||
            (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_TOUCH &&
            (int)touch.px > 0 &&
            (int)touch.px < 532 &&
            (int)touch.py > 28 &&
            (int)touch.py < 692))
        {
            Gui::index(CELLS, 0);
            Gui::backupScroll(false);
            Gui::entryType(TITLES);
            Gui::clearSelectedEntries();
            Gui::setPKSMBridgeFlag(false);
            Gui::updateButtonsColor(); // Do this last
        }

         // Handle pressing X
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
                    io::deleteFolderRecursively((path + "/").c_str());
                    refreshDirectories(title.id());
                    Gui::index(CELLS, index - 1);
                }
            }
        }

        // Handle pressing Y
        // Backup list active:   Deactivate backup list, select title, and
        //                         enable backup button
        // Backup list inactive: Select title and enable backup button
        if (kdown & KEY_Y)
        {
            if (Gui::backupScroll())
            {
                Gui::index(CELLS, 0);
                Gui::backupScroll(false);
            }
            Gui::entryType(TITLES);
            Gui::addSelectedEntry(Gui::index(TITLES));
            Gui::setPKSMBridgeFlag(false);
            Gui::updateButtonsColor(); // Do this last
        }

        // Handle holding Y
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

        // Handle pressing/touching L
        if (Gui::isBackupReleased() || (kdown & KEY_L))
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
                Gui::updateButtonsColor();
            }
            else if (Gui::backupScroll())
            {
                if (Gui::getPKSMBridgeFlag())
                {
                    sendToPKSMBrigde(Gui::index(TITLES), g_currentUId);
                }
                else
                {
                    io::backup(Gui::index(TITLES), g_currentUId);
                }
            }
        }

        // Handle pressing/touching R
        if (Gui::isRestoreReleased() || (kdown & KEY_R))
        {
            if (Gui::multipleSelectionEnabled())
            {
                Gui::clearSelectedEntries();
                Gui::updateButtonsColor();
            }
            else if (Gui::backupScroll())
            {
                if (Gui::getPKSMBridgeFlag())
                {
                    recvFromPKSMBridge(Gui::index(TITLES), g_currentUId);
                }
                else
                {
                        io::restore(Gui::index(TITLES), g_currentUId);
                }
            }
        }

        Gui::updateSelector();
        Gui::draw(g_currentUId);
    }

    servicesExit();
    return 0;
}
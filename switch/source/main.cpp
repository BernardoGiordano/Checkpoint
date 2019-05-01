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

#include "main.hpp"

u128 g_currentUId = 0;
bool g_backupScrollEnabled = 0;

int main(int argc, char** argv)
{
    Result res = servicesInit();
    if (R_FAILED(res))
    {
        servicesExit();
        return res;
    }

    loadTitles();
    // get the user IDs
    std::vector<u128> userIds = Account::ids();
    // set g_currentUId to a default user in case we loaded at least one user
    if (g_currentUId == 0) g_currentUId = userIds.at(0);

    int selectionTimer = 0;
    while(appletMainLoop() && !(hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS))
    {
        hidScanInput();
        u32 kdown = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 kheld = hidKeysHeld(CONTROLLER_P1_AUTO);
        if (kdown & KEY_ZL || kdown & KEY_ZR)
        {
            while((g_currentUId = Account::selectAccount()) == 0);
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
                    Gui::updateButtons();
                }
            }
        }

        // handle touchscreen
        touchPosition touch;
        hidTouchRead(&touch, 0);
        if (!g_backupScrollEnabled &&
            hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH &&
            touch.px >= 1200 && touch.px <= 1200 + USER_ICON_SIZE &&
            touch.py >= 626 && touch.py <= 626 + USER_ICON_SIZE)
        {
            while ((g_currentUId = Account::selectAccount()) == 0);
            Gui::index(TITLES, 0);
            Gui::index(CELLS, 0);
            Gui::setPKSMBridgeFlag(false);
        }

        // Handle touching the backup list
        if ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_TOUCH &&
            (int)touch.px > 538 &&
            (int)touch.px < 952 &&
            (int)touch.py > 276 &&
            (int)touch.py < 656))
        {
            // Activate backup list only if multiple selections are enabled
            if (!MS::multipleSelectionEnabled())
            {
                g_backupScrollEnabled = true;
                Gui::updateButtons();
                Gui::entryType(CELLS);
            }
        }

        // Handle pressing A
        // Backup list active:   Backup/Restore
        // Backup list inactive: Activate backup list only if multiple
        //                       selections are enabled
        if (kdown & KEY_A)
        {
            // If backup list is active...
            if (g_backupScrollEnabled)
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
                if (!MS::multipleSelectionEnabled())
                {
                    g_backupScrollEnabled = true;
                    Gui::updateButtons();
                    Gui::entryType(CELLS);
                }
            }
        }

        // Handle pressing B
        if (kdown & KEY_B ||
            (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_TOUCH &&
            (int)touch.px >= 0 &&
            (int)touch.px <= 532 &&
            (int)touch.py >= 0 &&
            (int)touch.py <= 664))
        {
            Gui::index(CELLS, 0);
            g_backupScrollEnabled = false;
            Gui::entryType(TITLES);
            MS::clearSelectedEntries();
            Gui::setPKSMBridgeFlag(false);
            Gui::updateButtons(); // Do this last
        }

        // Handle pressing X
        if (kdown & KEY_X)
        {
            if (g_backupScrollEnabled)
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
        //                       enable backup button
        // Backup list inactive: Select title and enable backup button
        if (kdown & KEY_Y)
        {
            if (g_backupScrollEnabled)
            {
                Gui::index(CELLS, 0);
                g_backupScrollEnabled = false;
            }
            Gui::entryType(TITLES);
            MS::addSelectedEntry(Gui::index(TITLES));
            Gui::setPKSMBridgeFlag(false);
            Gui::updateButtons(); // Do this last
        }

        // Handle holding Y
        if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_Y && !(g_backupScrollEnabled))
        {
            selectionTimer++;
        }
        else
        {
            selectionTimer = 0;
        }

        if (selectionTimer > 45)
        {
            MS::clearSelectedEntries();
            for (size_t i = 0, sz = getTitleCount(g_currentUId); i < sz; i++)
            {
                MS::addSelectedEntry(i);
            }
            selectionTimer = 0;
        }

        // Handle pressing/touching L
        if (Gui::isBackupReleased() || (kdown & KEY_L))
        {
            if (MS::multipleSelectionEnabled())
            {
                Gui::resetIndex(CELLS);
                std::vector<size_t> list = MS::selectedEntries();
                for (size_t i = 0, sz = list.size(); i < sz; i++)
                {
                    io::backup(list.at(i), g_currentUId);
                }
                MS::clearSelectedEntries();
                Gui::updateButtons();
                Gui::showInfo("Progress correctly saved to disk.");
            }
            else if (g_backupScrollEnabled)
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
            if (g_backupScrollEnabled)
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

        if (Gui::isCheatReleased() && CheatManager::loaded())
        {
            if (MS::multipleSelectionEnabled())
            {
                MS::clearSelectedEntries();
                Gui::updateButtons();
            }
            else
            {
                Title title;
                getTitle(title, g_currentUId, Gui::index(TITLES));
                std::string key = StringUtils::format("%016llX", title.id());
                if (CheatManager::availableCodes(key))
                {
                    CheatManager::manageCheats(key);
                }
                else
                {
                    Gui::showInfo("No available cheat codes for this title.");
                }              
            }
        }

        Gui::updateSelector();
        Gui::draw();

        // poll server
        Configuration::getInstance().pollServer();
    }

    servicesExit();
    return 0;
}
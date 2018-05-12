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

#include <switch.h>
#include "thread.hpp"
#include "title.hpp"
#include "util.hpp"
#include "io.hpp"

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
        hidScanInput();
        u32 kdown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kdown & KEY_A || kdown & KEY_RIGHT)
        {
            Gui::backupScroll(true);
            Gui::updateButtonsColor();
            hid::entryType(CELLS);
        }
        
        if (kdown & KEY_B || kdown & KEY_LEFT)
        {
            Gui::backupScroll(false);
            Gui::updateButtonsColor();
            hid::entryType(TITLES);
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
                    getTitle(title, Gui::index(TITLES));
                    std::vector<std::string> list = title.saves();
                    std::string path = title.path() + "/" + list.at(index);
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
        
        if (selectionTimer > 90)
        {
            Gui::clearSelectedEntries();
            for (size_t i = 0, sz = getTitleCount(); i < sz; i++)
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
                    io::backup(list.at(i));
                }
                Gui::clearSelectedEntries();
            }
            else
            {
                io::backup(Gui::index(TITLES));
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
                io::restore(Gui::index(TITLES));
            }
        }

        Gui::updateSelector();
        Gui::draw();
    }

    Threads::destroy();
    servicesExit();
    return 0;
}
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

#include "thread.hpp"
#include "util.hpp"

int main() {
    servicesInit();

    int selectionTimer = 0;
    int refreshTimer = 0;
    Threads::create((ThreadFunc)Threads::titles);

    u32 kHeld, kDown = hidKeysDown();
    while (aptMainLoop() && !(kDown & KEY_START)) {
        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();

        // Handle pressing A
        // Backup list active:   Backup/Restore
        // Backup list inactive: Activate backup list only if multiple
        //                       selections are enabled
        if (kDown & KEY_A)
        {
            // If backup list is active...
            if (Gui::bottomScroll())
            {
                // If the "New..." entry is selected...
                if (0 == Gui::scrollableIndex())
                {
                    io::backup(Gui::index());
                }
                else
                {
                    io::restore(Gui::index());
                }
            }
            else
            {
                // Activate backup list only if multiple selections are not enabled
                if (!Gui::multipleSelectionEnabled())
                {
                    Gui::bottomScroll(true);
                    Gui::updateButtonsColor();
                }
            }
        }

        if (kDown & KEY_B)
        {
            Gui::bottomScroll(false);
            Gui::clearSelectedEntries();
            Gui::resetScrollableIndex();
            Gui::updateButtonsColor();
        }

        if (kDown & KEY_X)
        {
            if (Gui::bottomScroll())
            {
                bool isSaveMode = Archive::mode() == MODE_SAVE;
                size_t index = Gui::scrollableIndex();
                // avoid actions if X is pressed on "New..."
                if (index > 0 && Gui::askForConfirmation("Delete selected backup?"))
                {
                    Title title;
                    getTitle(title, Gui::index());
                    std::u16string path = isSaveMode ? title.fullSavePath(index) : title.fullExtdataPath(index);
                    io::deleteBackupFolder(path);
                    refreshDirectories(title.id());
                    Gui::scrollableIndex(index - 1);
                }
            }
            else
            {
                Gui::resetIndex();
                Archive::mode(Archive::mode() == MODE_SAVE ? MODE_EXTDATA : MODE_SAVE);
                Gui::clearSelectedEntries();
                Gui::resetScrollableIndex();
            }
        }

        if (kDown & KEY_Y)
        {
            if (Gui::bottomScroll())
            {
                Gui::resetScrollableIndex();
                Gui::bottomScroll(false);
            }
            Gui::addSelectedEntry(Gui::index());
            Gui::updateButtonsColor(); // Do this last
        }

        if (kHeld & KEY_Y)
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

        if (kHeld & KEY_B)
        {
            refreshTimer++;
        }
        else
        {
            refreshTimer = 0;
        }

        if (refreshTimer > 90)
        {
            Gui::resetIndex();
            Gui::clearSelectedEntries();
            Gui::resetScrollableIndex();
            Threads::create((ThreadFunc)Threads::titles);
            refreshTimer = 0;
        }

        if (Gui::isBackupReleased() || (kDown & KEY_L))
        {
            if (Gui::multipleSelectionEnabled())
            {
                Gui::resetScrollableIndex();
                std::vector<size_t> list = Gui::selectedEntries();
                for (size_t i = 0, sz = list.size(); i < sz; i++)
                {
                    io::backup(list.at(i));
                }
                Gui::clearSelectedEntries();
                Gui::updateButtonsColor();
            }
            else if (Gui::bottomScroll())
            {
                io::backup(Gui::index());
            }
        }

        if (Gui::isRestoreReleased() || (kDown & KEY_R))
        {
            if (Gui::multipleSelectionEnabled())
            {
                Gui::clearSelectedEntries();
                Gui::updateButtonsColor();
            }
            else if (Gui::bottomScroll())
            {
                io::restore(Gui::index());
            }
        }

        Gui::updateSelector();
        Gui::draw();
    }

    Threads::destroy();
    servicesExit();
}

/*  This file is part of Checkpoint
>	Copyright (C) 2017/2018 Bernardo Giordano
>
>   This program is free software: you can redistribute it and/or modify
>   it under the terms of the GNU General Public License as published by
>   the Free Software Foundation, either version 3 of the License, or
>   (at your option) any later version.
>
>   This program is distributed in the hope that it will be useful,
>   but WITHOUT ANY WARRANTY; without even the implied warranty of
>   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
>   GNU General Public License for more details.
>
>   You should have received a copy of the GNU General Public License
>   along with this program.  If not, see <http://www.gnu.org/licenses/>.
>   See LICENSE for information.
*/

#include "common.h"

int main() {
	servicesInit();
	
	int selectionTimer = 0;
	int refreshTimer = 0;
	createThread((ThreadFunc)threadLoadTitles);

	while (aptMainLoop() && !(hidKeysDown() & KEY_START)) {
		hidScanInput();
		
		if (hidKeysDown() & KEY_A)
		{
			GUI_setBottomScroll(true);
			GUI_updateButtonsColor();
			setEntryType(CELLS);
		}
		
		if (hidKeysDown() & KEY_B)
		{
			GUI_setBottomScroll(false);
			GUI_updateButtonsColor();
			setEntryType(TITLES);
			GUI_clearSelectedEntries();
		}
		
		if (hidKeysDown() & KEY_X)
		{
			if (GUI_getBottomScroll())
			{
				bool isSaveMode = getMode() == MODE_SAVE;
				size_t index = GUI_getScrollableIndex();
				// avoid actions if X is pressed on "New..."
				if (index > 0)
				{
					if (GUI_askForConfirmation("Delete selected backup?"))
					{
						Title title;
						getTitle(title, GUI_getFullIndex());
						std::vector<std::u16string> list = isSaveMode ? title.getDirectories() : title.getExtdatas();
						std::u16string basepath = isSaveMode ? title.getBackupPath() : title.getExtdataPath();
						deleteBackupFolder(basepath + u8tou16("/") + list.at(index));
						refreshDirectories(title.getId());
						GUI_setScrollableIndex(index - 1);
					}			
				}
			}
			else
			{
				GUI_resetIndex();
				setMode(getMode() == MODE_SAVE ? MODE_EXTDATA : MODE_SAVE);
				GUI_clearSelectedEntries();
			}
		}
		
		if (hidKeysDown() & KEY_Y)
		{
			GUI_addSelectedEntry(GUI_getFullIndex());
		}
		
		if (hidKeysHeld() & KEY_Y)
		{
			selectionTimer++;
		}
		else
		{
			selectionTimer = 0;
		}
		
		if (selectionTimer > 90)
		{
			GUI_clearSelectedEntries();
			for (size_t i = 0, sz = getTitlesCount(); i < sz; i++)
			{
				GUI_addSelectedEntry(i);
			}
			selectionTimer = 0;
		}

		if (hidKeysHeld() & KEY_B)
		{
			refreshTimer++;
		}
		else
		{
			refreshTimer = 0;
		}

		if (refreshTimer > 90)
		{
			createThread((ThreadFunc)threadLoadTitles);
			refreshTimer = 0;
		}
		
		if (GUI_isBackupReleased())
		{
			if (GUI_multipleSelectionEnabled())
			{
				GUI_resetDirectoryListIndex();
				std::vector<size_t> list = GUI_getSelectedEntries();
				for (size_t i = 0, sz = list.size(); i < sz; i++)
				{
					backup(list.at(i));
				}
				GUI_clearSelectedEntries();
			}
			else
			{
				backup(GUI_getFullIndex());
			}
		}
		
		if (GUI_isRestoreReleased())
		{
			if (GUI_multipleSelectionEnabled())
			{
				GUI_clearSelectedEntries();
			}
			else
			{
				restore(GUI_getFullIndex());
			}
		}
		
		GUI_updateSelector();
		GUI_draw();
	}
	
	destroyThreads();
	servicesExit();
}
/*  This file is part of Checkpoint
>	Copyright (C) 2017 Bernardo Giordano
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

static Gui* menu;

void createInfo(std::string title, std::string message)
{
	menu->createInfo(title, message);
}

void createError(Result res, std::string message)
{
	menu->createError(res, message);
}

int main() {
	servicesInit();
	
	int selectionTimer = 0;
	menu = new Gui();
	
	createThread((ThreadFunc)threadLoadTitles);

	while (aptMainLoop() && !(hidKeysDown() & KEY_START)) {
		hidScanInput();
		
		if (hidKeysDown() & KEY_A)
		{
			menu->setBottomScroll(true);
			menu->updateButtonsColor();
			setEntryType(CELLS);
		}
		
		if (hidKeysDown() & KEY_B)
		{
			menu->setBottomScroll(false);
			menu->updateButtonsColor();
			setEntryType(TITLES);
			clearSelectedEntries();
		}
		
		if (hidKeysDown() & KEY_X)
		{
			menu->resetIndex();
			setMode(getMode() == MODE_SAVE ? MODE_EXTDATA : MODE_SAVE);
			clearSelectedEntries();
		}
		
		if (hidKeysDown() & KEY_Y)
		{
			addSelectedEntry(menu->getNormalizedIndex());
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
			clearSelectedEntries();
			for (size_t i = 0, sz = getTitlesCount(); i < sz; i++)
			{
				addSelectedEntry(i);
			}
			selectionTimer = 0;
		}
		
		if (menu->isBackupReleased())
		{
			if (multipleSelectionEnabled())
			{
				resetDirectoryListIndex();
				std::vector<size_t> list = getSelectedEntries();
				for (size_t i = 0, sz = list.size(); i < sz; i++)
				{
					backup(list.at(i));
				}
				clearSelectedEntries();
			}
			else
			{
				backup(menu->getNormalizedIndex());
			}
		}
		
		if (menu->isRestoreReleased())
		{
			if (multipleSelectionEnabled())
			{
				clearSelectedEntries();
			}
			else
			{
				restore(menu->getNormalizedIndex());
			}
		}
		
		menu->updateSelector();
		menu->draw();
	}
	
	delete menu;
	
	destroyThreads();
	servicesExit();
}
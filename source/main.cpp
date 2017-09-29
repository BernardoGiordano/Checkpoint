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
	
	menu = new Gui();
	
	createThread((ThreadFunc)threadLoadTitles);
	createThread((ThreadFunc)threadDownloadFilter);

	while (aptMainLoop() && !(hidKeysDown() & KEY_START)) {
		hidScanInput();
		
		if (hidKeysDown() & KEY_A)
		{
			menu->setBottomScroll(!menu->getBottomScroll());
			menu->updateButtonsColor();
			setEntryType(getEntryType() == TITLES ? CELLS : TITLES);
		}
		
		if (hidKeysDown() & KEY_B)
		{
			menu->setBottomScroll(false);
			menu->updateButtonsColor();
			setEntryType(TITLES);
		}
		
		if (hidKeysDown() & KEY_X)
		{
			setMode(getMode() == MODE_SAVE ? MODE_EXTDATA : MODE_SAVE);
		}
		
		if (menu->isBackupReleased())
		{
			backup(menu->getNormalizedIndex());
		}
		
		if (menu->isRestoreReleased())
		{
			restore(menu->getNormalizedIndex());
		}
		
		menu->updateSelector();
		menu->draw();
	}
	
	delete menu;
	
	destroyThreads();
	servicesExit();
}
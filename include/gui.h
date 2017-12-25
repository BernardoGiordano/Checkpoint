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

#ifndef GUI_H
#define GUI_H

#include "common.h"

#define TEXTURE_CHECKPOINT 1
#define TEXTURE_CHECKBOX 2
#define TEXTURE_TWLCARD	3
#define TEXTURE_NOICON 4
#define TEXTURE_FIRST_FREE_INDEX 5

#define COLOR_BACKGROUND ABGR8(255, 51, 51, 51)
#define COLOR_BARS RGBA8(150, 150, 150, 255)
#define WHITE RGBA8(255, 255, 255, 255)
#define GREYISH RGBA8(200, 200, 200, 255)
#define BLACK RGBA8(0, 0, 0, 255)
#define BLUE RGBA8(0, 0, 255, 255)
#define RED RGBA8(255, 0, 0, 255)
#define GREEN RGBA8(0, 255, 0, 255)

bool askForConfirmation(std::string text);

size_t getScrollableIndex(void);

std::vector<size_t> getSelectedEntries(void);
bool multipleSelectionEnabled(void);
void clearSelectedEntries(void);
void addSelectedEntry(size_t index);
void resetDirectoryListIndex(void);
void setScrollableIndex(size_t index);

void drawCopy(std::u16string src, u32 offset, u32 size);

class Gui
{
public:
	Gui(void);
	void createInfo(std::string title, std::string message);
	void createError(Result res, std::string message);
	bool getBottomScroll(void);
	size_t getNormalizedIndex(void);
	bool isBackupReleased(void);
	bool isRestoreReleased(void);
	void setBottomScroll(bool enable);
	void updateButtonsColor(void);
	void updateSelector(void);
	void resetIndex(void);
	
	void draw(void);

private:
	void drawSelector(void);
	int getSelectorX(size_t index);
	int getSelectorY(size_t index);
	
	size_t index;
	int page;
	const size_t rowlen = 4;
	const size_t collen = 8;
	
	bool bottomScrollEnabled;
};

#endif
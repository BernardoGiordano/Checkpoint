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

#ifndef GUI_H
#define GUI_H

#include "common.h"

#define TEXTURE_CHECKPOINT 1
#define TEXTURE_CHECKBOX 2
#define TEXTURE_TWLCARD	3
#define TEXTURE_NOICON 4
#define TEXTURE_FIRST_FREE_INDEX 5

#define COLOR_BACKGROUND ABGR8(255, 51, 51, 51)
#define COLOR_BARS RGBA8(88, 88, 88, 255)
#define WHITE RGBA8(255, 255, 255, 255)
#define GREYISH RGBA8(157, 157, 157, 255)
#define BLACK RGBA8(0, 0, 0, 255)
#define BLUE RGBA8(0, 0, 255, 255)
#define RED RGBA8(255, 0, 0, 255)
#define GREEN RGBA8(0, 255, 0, 255)

void GUI_init(void);
bool GUI_getBottomScroll(void);
size_t GUI_getFullIndex(void);
bool GUI_isBackupReleased(void);
bool GUI_isRestoreReleased(void);
void GUI_setBottomScroll(bool enable);
void GUI_updateButtonsColor(void);
void GUI_updateSelector(void);
void GUI_resetIndex(void);
void GUI_draw(void);

bool GUI_askForConfirmation(std::string text);
void GUI_drawCopy(std::u16string src, u32 offset, u32 size);
void createInfo(std::string title, std::string message);
void createError(Result res, std::string message);

size_t GUI_getScrollableIndex(void);
std::vector<size_t> GUI_getSelectedEntries(void);
bool GUI_multipleSelectionEnabled(void);
void GUI_clearSelectedEntries(void);
void GUI_addSelectedEntry(size_t index);
void GUI_resetDirectoryListIndex(void);
void GUI_setScrollableIndex(size_t index);

#endif
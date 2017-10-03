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

#include "swkbd.h"

std::u16string getPath(std::string suggestion)
{
    static SwkbdState swkbd;
    SwkbdButton button = SWKBD_BUTTON_NONE;
    char buf[CUSTOM_PATH_LEN] = {0};
    
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, CUSTOM_PATH_LEN - 1); 
	swkbdSetHintText(&swkbd, "Choose a name for your backup.");
	swkbdSetInitialText(&swkbd, suggestion.c_str());
	
    button = swkbdInputText(&swkbd, buf, CUSTOM_PATH_LEN);
    buf[CUSTOM_PATH_LEN - 1] = '\0';
    
    if (button == SWKBD_BUTTON_CONFIRM)
    {
        return removeForbiddenCharacters(u8tou16(buf));
    }
    
    return u8tou16(" ");
}
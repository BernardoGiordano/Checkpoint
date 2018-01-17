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

#ifndef CLICKABLE_H
#define CLICKABLE_H

#include "common.h"

class Clickable
{
public:
	Clickable(int x, int y, u32 w, u32 h, u32 color, u32 colorMessage, std::string message, bool centered);
	bool isHeld(void);
	bool isReleased(void);
	void invertColors(void);
	void setColors(u32 bg, u32 text);
	std::string getMessage();
	
	void draw(void);
	void drawStatic(void);

private:
	int x;
	int y;
	u32 w;
	u32 h;
	u32 color;
	u32 colorMessage;
	std::string message;
	bool centered;
	
	bool oldpressed;
};

#endif
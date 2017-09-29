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

#include "clickable.h"

Clickable::Clickable(int _x, int _y, u32 _w, u32 _h, u32 _color, u32 _colorMessage, std::string _message, bool _centered)
{
	x = _x;
	y = _y;
	w = _w;
	h = _h;
	color = _color;
	colorMessage = _colorMessage;
	message = _message;
	centered = _centered;
	oldpressed = false;
}

std::string Clickable::getMessage(void)
{
	return message;
}

bool Clickable::isHeld(void)
{
	touchPosition touch;
	hidTouchRead(&touch);
	return ((hidKeysHeld() & KEY_TOUCH) && touch.px > x && touch.px < x+w && touch.py > y && touch.py < y+h);
}

bool Clickable::isReleased(void)
{
	touchPosition touch;
	hidTouchRead(&touch);	
	bool on = touch.px > x && touch.px < x+w && touch.py > y && touch.py < y+h;
	
	if (on)
	{
		oldpressed = true;
	}
	else
	{
		if (oldpressed && !(touch.px > 0 || touch.py > 0))
		{
			oldpressed = false;			
			return true;
		}
		oldpressed = false;
	}
	
	return false;
}

void Clickable::invertColors(void)
{
	u32 tmp = color;
	color = colorMessage;
	colorMessage = tmp;
}

void Clickable::setColors(u32 bg, u32 text)
{
	color = bg;
	colorMessage = text;
}

void Clickable::draw(void)
{
	static const float size = 0.7f;
	static const float messageHeight = pp2d_get_text_height(message.c_str(), size, size);
	static const float messageWidth = centered ? pp2d_get_text_width(message.c_str(), size, size) : w - 8;
	const u32 bgColor = isHeld() ? colorMessage : color;
	const u32 msgColor = bgColor == color ? colorMessage : color;
	
	pp2d_draw_rectangle(x, y, w, h, bgColor);
	pp2d_draw_text(x + (w - messageWidth)/2, y + (h - messageHeight)/2, size, size, msgColor, message.c_str());
}

void Clickable::drawStatic(void)
{
	static const float size = 0.5f;
	static const float messageHeight = pp2d_get_text_height(message.c_str(), size, size);
	static const float messageWidth = centered ? pp2d_get_text_width(message.c_str(), size, size) : w - 8;
	
	pp2d_draw_rectangle(x, y, w, h, color);
	pp2d_draw_text(x + (w - messageWidth)/2, y + (h - messageHeight)/2, size, size, colorMessage, message.c_str());
}
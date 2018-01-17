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

#include "info.h"

void Info::init(std::string _title, std::string _message, int _ttl, Info_t _type)
{
	title = _title;
	message = _message;
	width = 240;
	height = 70;
	x = (TOP_WIDTH - width) / 2;
	y = SCREEN_HEIGHT - height - 9;
	ttl = _ttl;
	type = _type;
	res = 0;
}

void Info::init(Result _res, std::string _message, int _ttl, Info_t _type)
{
	title = "";
	message = _message;
	width = 240;
	height = 70;
	x = (TOP_WIDTH - width) / 2;
	y = SCREEN_HEIGHT - height - 9;
	ttl = _ttl;
	type = _type;
	res = _res;
}

void Info::draw(void)
{
	static const float size = 0.6f;
	
	if ((type == TYPE_INFO && ttl > 0) || (type == TYPE_ERROR && ttl > 0 && res != 0))
	{
		char buf[30];
		float w, hres;
		float hmessage = pp2d_get_text_height_wrap(message.c_str(), 0.46, 0.46, width - 20);
		int normalizedTransparency = ttl > 255 ? 255 : ttl;
		u32 color = 0, bordercolor = 0, bgcolor = 0;
		
		if (type == TYPE_ERROR)
		{
			color = RGBA8(255, 255, 255, normalizedTransparency);
			bordercolor = RGBA8(255, 0, 0, normalizedTransparency);
			bgcolor = RGBA8(0, 0, 0, normalizedTransparency);
			sprintf(buf, "Error: %08lX", res);
			pp2d_get_text_size(&w, &hres, size, size, buf);	
		}
		else if (type == TYPE_INFO)
		{
			color = RGBA8(0, 0, 255, normalizedTransparency);
			bordercolor = RGBA8(0, 255, 0, normalizedTransparency);
			bgcolor = RGBA8(255, 255, 255, normalizedTransparency);
			pp2d_get_text_size(&w, &hres, size, size, title.c_str());
		}
		
		const int wrapWidth = width - 20;
		const float spacing = (height - hres - hmessage) / 3;
		
		pp2d_draw_rectangle(x - 2, y - 2, width + 4, height + 4, bordercolor);
		pp2d_draw_rectangle(x, y, width, height, bgcolor);
		pp2d_draw_text(x + (width - w) / 2, y + spacing, size, size, color, type == TYPE_INFO ? title.c_str() : buf);
		pp2d_draw_text_wrap(x + 10, y + 2*spacing + hres, 0.46, 0.46, color, wrapWidth, message.c_str());
		
		ttl--;
	}
}
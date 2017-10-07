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

#include "messagebox.h"

MessageBox::MessageBox(u32 _colorbg, u32 _colormessage, gfxScreen_t _screen)
{
	colorbg = _colorbg;
	colormessage = _colormessage;
	screen = _screen;
	messageList.clear();
}

void MessageBox::push_message(std::string newmessage)
{
	messageList.push_back(newmessage);
}

void MessageBox::clear(void)
{
	messageList.clear();
}

bool MessageBox::isEmpty(void)
{
	return messageList.empty();
}

void MessageBox::draw(void)
{
	static const float size = 0.6f;
	static const float spacingFromEdges = 10;
	static const u32 screenw = screen == GFX_TOP ? 400 : 320;
	static const float mh = pp2d_get_text_height("", size, size);
	const float h = mh*messageList.size() + 2*spacingFromEdges;
	
	float w = 0;
	for (size_t i = 0, sz = messageList.size(); i < sz; i++)
	{
		float nw = pp2d_get_text_width(messageList.at(i).c_str(), size, size);
		if (nw > w)
		{
			w = nw;
		}
	}
	w += 2 * spacingFromEdges; 
	
	const int x = (screenw - w) / 2;
	const int y = (SCREEN_HEIGHT - h) / 2;
	
	pp2d_draw_rectangle(x - 2, y - 2, w + 4, h + 4, BLACK);
	pp2d_draw_rectangle(x, y, w, h, colorbg);
	
	for (size_t i = 0, sz = messageList.size(); i < sz; i++)
	{
		pp2d_draw_text_center(screen, y + spacingFromEdges + mh*i, size, size, colormessage, messageList.at(i).c_str());
	}
}
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

#include "util.h"

void servicesExit(void)
{
	pxiDevExit();
	archiveExit();
	amExit();
	srvExit();
	hidExit();
	pp2d_exit();
	sdmcExit();
	romfsExit();
}

void servicesInit(void)
{
	Result res = 0;
	romfsInit();
	sdmcInit();
	pp2d_init();
	hidInit();
	srvInit();
	amInit();
	pxiDevInit();
	
	res = archiveInit();
	if (R_FAILED(res))
	{
		createError(res, "SDMC archive init failed.");
	}
	
	mkdir("sdmc:/3ds", 777);
	mkdir("sdmc:/3ds/Checkpoint", 777);
	mkdir("sdmc:/3ds/Checkpoint/saves", 777);
	mkdir("sdmc:/3ds/Checkpoint/extdata", 777);
	
	pp2d_set_screen_color(GFX_TOP, COLOR_BACKGROUND);
	pp2d_set_screen_color(GFX_BOTTOM, COLOR_BACKGROUND);
	pp2d_load_texture_png(TEXTURE_CHECKBOX, "romfs:/checkbox.png");
	pp2d_load_texture_png(TEXTURE_CHECKPOINT, "romfs:/checkpoint.png");
	pp2d_load_texture_png(TEXTURE_TWLCARD, "romfs:/twlcart.png");
}

bool askForConfirmation(std::string text)
{
	Clickable buttonYes(40, 90, 100, 60, WHITE, BLACK, "   \uE000 Yes", true);
	Clickable buttonNo(200, 90, 100, 60, WHITE, BLACK, "   \uE001 No", true);
	MessageBox message(COLOR_BARS, WHITE, GFX_TOP);
	message.push_message(text);
	
	while(aptMainLoop() && !(buttonNo.isReleased() || hidKeysDown() & KEY_B))
	{
		hidScanInput();
		if (buttonYes.isReleased() || hidKeysDown() & KEY_A)
		{
			return true;
		}
		
		pp2d_begin_draw(GFX_TOP, GFX_LEFT);
			pp2d_draw_rectangle(0, 0, 400, 19, COLOR_BARS);
			pp2d_draw_rectangle(0, 221, 400, 19, COLOR_BARS);
			
			message.draw();
			
			pp2d_draw_on(GFX_BOTTOM, GFX_LEFT);
			pp2d_draw_rectangle(0, 0, 320, 19, COLOR_BARS);
			pp2d_draw_rectangle(0, 221, 320, 19, COLOR_BARS);
			
			pp2d_draw_rectangle(38, 88, 104, 64, GREYISH);
			pp2d_draw_rectangle(198, 88, 104, 64, GREYISH);
			buttonYes.draw();
			buttonNo.draw();
		pp2d_end_draw();
	}
	
	return false;
}
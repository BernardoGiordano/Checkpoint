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

void copyFilter(void)
{
	FILE *fptr = fopen("romfs:/filter.txt", "rt");
	if (fptr == NULL)
		return;
	fseek(fptr, 0, SEEK_END);
	u32 size = ftell(fptr);
	u8* buf = (u8*)malloc(size);
	memset(buf, 0, size);
	rewind(fptr);
	fread(buf, size, 1, fptr);
	fclose(fptr);
	
	FILE *file = fopen("sdmc:/3ds/Checkpoint/filter.txt", "wb");
	fwrite(buf, 1, size, file);
	fclose(file);
}

void servicesExit(void)
{
	archiveExit();
	amExit();
	srvExit();
	hidExit();
	pp2d_exit();
	romfsExit();
	sdmcExit();
}

void servicesInit(void)
{
	Result res = 0;
	
	sdmcInit();
	romfsInit();
	pp2d_init();
	hidInit();
	srvInit();
	amInit();
	
	res = archiveInit();
	if (R_FAILED(res))
	{
		createError(res, "SDMC archive init failed.");
	}
	
	mkdir("sdmc:/3ds", 777);
	mkdir("sdmc:/3ds/Checkpoint", 777);
	mkdir("sdmc:/3ds/Checkpoint/saves", 777);
	
	pp2d_set_screen_color(GFX_TOP, COLOR_BACKGROUND);
	pp2d_set_screen_color(GFX_BOTTOM, COLOR_BACKGROUND);
}
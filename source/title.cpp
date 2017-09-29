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

#include "title.h"

static bool checkHigh(u64 id);
static void loadTextureIcon(smdh_s *smdh, size_t i);

static std::vector<Title> titles;
static std::vector<u32> filter;

bool Title::load(u64 _id, FS_MediaType _media)
{
	static size_t index = 1;
	id = _id;
	media = _media;

	smdh_s *smdh = loadSMDH(getLowId(), getHighId(), media);
	if (smdh == NULL)
	{
		return false;
	}
	
	shortDescription = removeForbiddenCharacters((char16_t*)smdh->applicationTitles[1].shortDescription);
	longDescription = (char16_t*)smdh->applicationTitles[1].longDescription;
	backupPath = u8tou16("/3ds/Checkpoint/saves/") + shortDescription;

	loadTextureIcon(smdh, index);
	textureId = index;
	index++;

	if (!directoryExist(getArchiveSDMC(), backupPath))
	{
		Result res = createDirectory(getArchiveSDMC(), backupPath);
		if (R_FAILED(res))
		{
			createError(res, "Failed to create backup directory.");
		}
	}
	
	refreshDirectories();
	
	delete smdh;
	return true;
}

std::string Title::getMediatypeString(void)
{
	switch(media)
	{
		case MEDIATYPE_SD: return "SD Card";
		case MEDIATYPE_GAME_CARD: return "Cartridge";
		case MEDIATYPE_NAND: return "NAND";
		default: return " ";
	}
	
	return " ";
}

std::string Title::getShortDescription(void)
{
	return u16tou8(shortDescription);
}

std::string Title::getLongDescription(void)
{
	return u16tou8(longDescription);
}

std::u16string Title::getBackupPath(void)
{
	return backupPath;
}

std::vector<std::u16string> Title::getDirectories(void)
{
	return directories;
}

void Title::refreshDirectories(void)
{
	directories.clear();
	
	Directory list(getArchiveSDMC(), backupPath);
	if (list.getLoaded())
	{
		for (size_t i = 0, sz = list.getCount(); i < sz; i++)
		{
			if (list.isFolder(i))
			{
				directories.push_back(list.getItem(i));
			}
		}
		
		std::sort(directories.begin(), directories.end());
		directories.insert(directories.begin(), u8tou16("New..."));
	}
	else
	{
		createError(list.getError(), "Couldn't retrieve the directory list for the title " + getShortDescription());
	}
}

u32 Title::getHighId(void)
{
	return (u32)(id >> 32);
}

u32 Title::getLowId(void)
{
	return (u32)id;
}

u32 Title::getUniqueId(void)
{
	return (getLowId() >> 8);
}

u64 Title::getId(void)
{
	return id;
}

FS_MediaType Title::getMediaType(void)
{
	return media;
}

size_t Title::getTextureId(void)
{
	return textureId;
}

static bool checkHigh(u64 id)
{
	u32 high = id >> 32;
	return (high == 0x00040000 || high == 0x00040002);
}

void loadFilter(void)
{
	Result res;
	u64 size = getFileSize(getArchiveSDMC(), u8tou16(PATH_FILTER));
	if (size == 0 || size % 16 != 0)
	{
		return;
	}
	
	FSStream stream(getArchiveSDMC(), u8tou16(PATH_FILTER), FS_OPEN_READ);
	if (stream.getLoaded())
	{
		u8 buf[size];
		res = stream.read(buf);
		if (R_SUCCEEDED(res))
		{
			filter.reserve(size/16);
			for (u32 i = 0, sz = size/16; i < sz; i++)
			{
				char tmp[9];
				memcpy(tmp, buf + i*16 + 8, 8);
				tmp[8] = '\0';
				
				std::string titleId(tmp);
				u32 id = std::stoul(titleId, nullptr, 16);
				filter.push_back(id);
			}
		}
	}
	stream.close();
}

static bool checkFilter(u32 low)
{
	for (u32 i = 0; i < filter.size(); i++)
	{
		if (low == filter.at(i))
		{
			return true;
		}
	}
	
	return false;
}

void loadTitles(void)
{
	u32 count;
	AM_GetTitleCount(MEDIATYPE_SD, &count);
	titles.reserve(count);

	u64 ids[count];
	AM_GetTitleList(NULL, MEDIATYPE_SD, count, ids);

	for (u32 i = 0; i < count; i++)
	{
		if (checkHigh(ids[i]) && !checkFilter((u32)ids[i]))
		{
			Title title;
			if (title.load(ids[i], MEDIATYPE_SD))
			{
				titles.push_back(title);
				std::sort(titles.begin(), titles.end(), [](Title l, Title r) {
					return l.getShortDescription() < r.getShortDescription();
				});
			}
		}
	}
	
	AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
	if (count > 0)
	{
		AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, count, ids);	
		if (checkHigh(ids[0]))
		{
			Title title;
			if (title.load(ids[0], MEDIATYPE_GAME_CARD))
			{
				titles.insert(titles.begin(), title);
			}
		}
	}
}

void getTitle(Title &dst, int i)
{
	if (i < getTitlesCount())
		dst = titles.at(i);
}

int getTitlesCount(void)
{
	return titles.size();
}

size_t getTextureId(int i)
{
	return titles.at(i).getTextureId();
}

static void loadTextureIcon(smdh_s *smdh, size_t i) {
	static const u32 width = 48, height = 48;
	u32 *image = (u32*)malloc(width*height*sizeof(u32));

	for (u32 x = 0; x < width; x++)
	{
		for (u32 y = 0; y < height; y++)
		{
			unsigned int dest_pixel = (x + y*width);
			unsigned int source_pixel = (((y >> 3) * (width >> 3) + (x >> 3)) << 6) + ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));

			u8 r = ((smdh->bigIconData[source_pixel] >> 11) & 0b11111) << 3;
			u8 g = ((smdh->bigIconData[source_pixel] >> 5) & 0b111111) << 2;
			u8 b = (smdh->bigIconData[source_pixel] & 0b11111) << 3;
			u8 a = 0xFF;

			image[dest_pixel] = (r << 24) | (g << 16) | (b << 8) | a;
		}
	}

	pp2d_load_texture_memory(i, (u8*)image, (u32)width, (u32)height);

	free(image);
}

void refreshDirectories(size_t i)
{
	titles.at(i).refreshDirectories();
}
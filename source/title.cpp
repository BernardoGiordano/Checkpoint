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

static std::vector<Title> titleSaves;
static std::vector<Title> titleExtdatas;

bool Title::load(u64 _id, FS_MediaType _media)
{
	bool loadTitle = false;
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
	extdataPath = u8tou16("/3ds/Checkpoint/extdata/") + shortDescription;

	accessibleSave = isSaveAccessible(getMediaType(), getLowId(), getHighId());
	accessibleExtdata = isExtdataAccessible(getExtdataId());
	
	if (accessibleSave)
	{
		loadTitle = true;
		if (!directoryExist(getArchiveSDMC(), backupPath))
		{
			Result res = createDirectory(getArchiveSDMC(), backupPath);
			if (R_FAILED(res))
			{
				createError(res, "Failed to create backup directory.");
			}
		}		
	}

	if (accessibleExtdata)
	{
		loadTitle = true;
		if (!directoryExist(getArchiveSDMC(), extdataPath))
		{
			Result res = createDirectory(getArchiveSDMC(), extdataPath);
			if (R_FAILED(res))
			{
				createError(res, "Failed to create backup directory.");
			}
		}
	}
	
	if (loadTitle)
	{
		loadTextureIcon(smdh, index);
		textureId = index;
		index++;
	}
	
	delete smdh;
	
	refreshDirectories();
	
	return loadTitle;
}

bool Title::getAccessibleSave(void)
{
	return accessibleSave;
}

bool Title::getAccessibleExtdata(void)
{
	return accessibleExtdata;
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

std::u16string Title::getExtdataPath(void)
{
	return extdataPath;
}

std::vector<std::u16string> Title::getDirectories(void)
{
	return directories;
}

std::vector<std::u16string> Title::getExtdatas(void)
{
	return extdatas;
}

void Title::refreshDirectories(void)
{
	directories.clear();
	extdatas.clear();
	
	if (getAccessibleSave())
	{
		// save backups
		Directory savelist(getArchiveSDMC(), backupPath);
		if (savelist.getLoaded())
		{
			for (size_t i = 0, sz = savelist.getCount(); i < sz; i++)
			{
				if (savelist.isFolder(i))
				{
					directories.push_back(savelist.getItem(i));
				}
			}
			
			std::sort(directories.begin(), directories.end());
			directories.insert(directories.begin(), u8tou16("New..."));
		}
		else
		{
			createError(savelist.getError(), "Couldn't retrieve the directory list for the title " + getShortDescription());
		}
	}
	
	if (getAccessibleExtdata())
	{
		// extdata backups
		Directory extlist(getArchiveSDMC(), extdataPath);
		if (extlist.getLoaded())
		{
			for (size_t i = 0, sz = extlist.getCount(); i < sz; i++)
			{
				if (extlist.isFolder(i))
				{
					extdatas.push_back(extlist.getItem(i));
				}
			}
			
			std::sort(extdatas.begin(), extdatas.end());
			extdatas.insert(extdatas.begin(), u8tou16("New..."));
		}
		else
		{
			createError(extlist.getError(), "Couldn't retrieve the extdata list for the title " + getShortDescription());
		}
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

u32 Title::getExtdataId(void)
{
	u32 low = getLowId();
	switch(low)
	{
		case 0x00055E00: return 0x0000055D; // Pokémon Y
		case 0x0011C400: return 0x000011C5; // Pokémon Omega Ruby
		case 0x00175E00: return 0x00001648; // Pokémon Moon
	}
	
	return low >> 8;
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

void loadTitles(void)
{
	u32 count = 0;
	AM_GetTitleCount(MEDIATYPE_SD, &count);
	titleSaves.reserve(count + 1);
	titleExtdatas.reserve(count + 1);

	u64 ids[count];
	AM_GetTitleList(NULL, MEDIATYPE_SD, count, ids);

	for (u32 i = 0; i < count; i++)
	{
		if (checkHigh(ids[i]))
		{
			Title title;
			if (title.load(ids[i], MEDIATYPE_SD))
			{
				if (title.getAccessibleSave())
				{
					titleSaves.push_back(title);
					std::sort(titleSaves.begin(), titleSaves.end(), [](Title l, Title r) {
						return l.getShortDescription() < r.getShortDescription();
					});
				}
				
				if (title.getAccessibleExtdata())
				{
					titleExtdatas.push_back(title);
					std::sort(titleExtdatas.begin(), titleExtdatas.end(), [](Title l, Title r) {
						return l.getShortDescription() < r.getShortDescription();
					});
				}
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
				if (title.getAccessibleSave())
				{
					titleSaves.insert(titleSaves.begin(), title);
				}
				
				if (title.getAccessibleExtdata())
				{
					titleExtdatas.insert(titleExtdatas.begin(), title);
				}
			}
		}
	}
}

void getTitle(Title &dst, int i)
{
	const Mode_t mode = getMode();
	if (i < getTitlesCount())
	{
		dst = mode == MODE_SAVE ? titleSaves.at(i) : titleExtdatas.at(i);
	}	
}

int getTitlesCount(void)
{
	const Mode_t mode = getMode();
	return mode == MODE_SAVE ? titleSaves.size() : titleExtdatas.size();
}

size_t getTextureId(int i)
{
	const Mode_t mode = getMode();
	return mode == MODE_SAVE ? titleSaves.at(i).getTextureId() : titleExtdatas.at(i).getTextureId();
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
	const Mode_t mode = getMode();
	if (mode == MODE_SAVE)
	{
		titleSaves.at(i).refreshDirectories();
	}
	else
	{
		titleExtdatas.at(i).refreshDirectories();
	}
}
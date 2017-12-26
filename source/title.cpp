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

static void exportTitleListCache(std::vector<Title> list, const std::u16string path);
static void importTitleListCache(void);

void Title::load(void)
{
	id = 0xFFFFFFFFFFFFFFFF;
	media = MEDIATYPE_SD;
	card = CARD_CTR;
	memset(productCode, 0, 16);
	shortDescription = u8tou16(" ");
	longDescription = u8tou16(" ");
	backupPath = u8tou16(" ");
	extdataPath = u8tou16(" ");
	textureId = TEXTURE_NOICON;
	accessibleSave = false;
	accessibleExtdata = false;
	directories.clear();
	extdatas.clear();
}

bool Title::load(u64 _id, FS_MediaType _media, FS_CardType _card)
{
	bool loadTitle = false;
	static size_t index = TEXTURE_FIRST_FREE_INDEX;
	id = _id;
	media = _media;
	card = _card;

	if (card == CARD_CTR)
	{
		smdh_s *smdh = loadSMDH(getLowId(), getHighId(), media);
		if (smdh == NULL)
		{
			return false;
		}
		
		char unique[12] = {0};
		sprintf(unique, "0x%05X ", (unsigned int)getUniqueId());
		
		shortDescription = removeForbiddenCharacters((char16_t*)smdh->applicationTitles[1].shortDescription);
		longDescription = (char16_t*)smdh->applicationTitles[1].longDescription;
		backupPath = u8tou16("/3ds/Checkpoint/saves/") + u8tou16(unique) + shortDescription;
		extdataPath = u8tou16("/3ds/Checkpoint/extdata/") + u8tou16(unique) + shortDescription;
		AM_GetTitleProductCode(media, id, productCode);
		
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
	}
	else
	{
		u8* headerData = new u8[0x3B4];
		Result res = FSUSER_GetLegacyRomHeader(media, 0LL, headerData);
		if (R_FAILED(res))
		{
			delete[] headerData;
			return false;
		}
		
		char _cardTitle[14] = {0};
		char _gameCode[6] = {0};
		
		std::copy(headerData, headerData + 12, _cardTitle);
		std::copy(headerData + 12, headerData + 16, _gameCode);
		_cardTitle[13] = '\0';
		_gameCode[5] = '\0';
		
		res = SPIGetCardType(&cardType, (_gameCode[0] == 'I') ? 1 : 0);
		if (R_FAILED(res))
		{
			delete[] headerData;
			return false;
		}
		
		delete[] headerData;
		
		shortDescription = removeForbiddenCharacters(u8tou16(_cardTitle));
		longDescription = shortDescription;
		backupPath = u8tou16("/3ds/Checkpoint/saves/") + u8tou16(_gameCode) + u8tou16(" ") + shortDescription;
		extdataPath = backupPath;
		memset(productCode, 0, 16);
		
		accessibleSave = true;
		accessibleExtdata = false;
		
		loadTitle = true;
		if (!directoryExist(getArchiveSDMC(), backupPath))
		{
			Result res = createDirectory(getArchiveSDMC(), backupPath);
			if (R_FAILED(res))
			{
				createError(res, "Failed to create backup directory.");
			}
		}
		
		textureId = TEXTURE_TWLCARD;
	}
	
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
		case 0x00179600:
		case 0x00179800: return 0x00001794; // Fire Emblem Conquest SE NA
		case 0x00179700:
		case 0x0017A800: return 0x00001795; // Fire Emblem Conquest SE EU
		case 0x0012DD00:
		case 0x0012DE00: return 0x000012DC; // Fire Emblem If JP
		case 0x001B5100: return 0x001B5000; // Pokémon Ultramoon
	}
	
	return low >> 8;
}

FS_MediaType Title::getMediaType(void)
{
	return media;
}

FS_CardType Title::getCardType(void)
{
	return card;
}

CardType Title::getSPICardType(void)
{
	return cardType;
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

void loadTitles(bool forceRefresh)
{
	std::u16string savecachePath = u8tou16("/3ds/Checkpoint/savecache");
	std::u16string extdatacachePath = u8tou16("/3ds/Checkpoint/extdatacache");
	
	// on refreshing
	titleSaves.clear();
	titleExtdatas.clear();

	bool optimizedLoad = false;
	
	u8 hash[SHA256_BLOCK_SIZE];
	calculateTitleDBHash(hash);

	std::u16string titlesHashPath = u8tou16("/3ds/Checkpoint/titles.sha");
	if (!fileExist(getArchiveSDMC(), titlesHashPath) || !fileExist(getArchiveSDMC(), savecachePath) || !fileExist(getArchiveSDMC(), extdatacachePath))
	{
		// create title list sha256 hash file if it doesn't exist in the working directory
		FSStream output(getArchiveSDMC(), titlesHashPath, FS_OPEN_WRITE, SHA256_BLOCK_SIZE);
		output.write(hash, SHA256_BLOCK_SIZE);
		output.close();
	}
	else
	{
		// compare current hash with the previous hash
		FSStream input(getArchiveSDMC(), titlesHashPath, FS_OPEN_READ);
		if (input.getLoaded() && input.getSize() == SHA256_BLOCK_SIZE)
		{
			u8* buf = new u8[input.getSize()];
			input.read(buf, input.getSize());
			input.close();
			
			if (memcmp(hash, buf, SHA256_BLOCK_SIZE) == 0)
			{
				// hash matches
				optimizedLoad = true;
			}
			else
			{
				FSUSER_DeleteFile(getArchiveSDMC(), fsMakePath(PATH_UTF16, titlesHashPath.data()));
				FSStream output(getArchiveSDMC(), titlesHashPath, FS_OPEN_WRITE, SHA256_BLOCK_SIZE);
				output.write(hash, SHA256_BLOCK_SIZE);
				output.close();
			}
			
			delete[] buf;
		}
	}
	
	if (optimizedLoad && !forceRefresh)
	{
		// deserialize data
		importTitleListCache();
	}
	else
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
				if (title.load(ids[i], MEDIATYPE_SD, CARD_CTR))
				{
					if (title.getAccessibleSave())
					{
						titleSaves.push_back(title);
					}
					
					if (title.getAccessibleExtdata())
					{
						titleExtdatas.push_back(title);
					}
				}
			}
		}
		
		std::sort(titleSaves.begin(), titleSaves.end(), [](Title l, Title r) {
			return l.getShortDescription() < r.getShortDescription();
		});
		
		std::sort(titleExtdatas.begin(), titleExtdatas.end(), [](Title l, Title r) {
			return l.getShortDescription() < r.getShortDescription();
		});
		
		FS_CardType cardType;
		Result res = FSUSER_GetCardType(&cardType);
		if (R_SUCCEEDED(res))
		{
			if (cardType == CARD_CTR)
			{
				AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
				if (count > 0)
				{
					AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, count, ids);	
					if (checkHigh(ids[0]))
					{
						Title title;
						if (title.load(ids[0], MEDIATYPE_GAME_CARD, cardType))
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
			else
			{
				Title title;
				if (title.load(0, MEDIATYPE_GAME_CARD, cardType))
				{
					titleSaves.insert(titleSaves.begin(), title);
				}
			}
		}
	}
	
	// serialize data
	exportTitleListCache(titleSaves, savecachePath);
	exportTitleListCache(titleExtdatas, extdatacachePath);
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

void refreshDirectories(u64 id)
{
	const Mode_t mode = getMode();
	if (mode == MODE_SAVE)
	{
		for (size_t i = 0; i < titleSaves.size(); i++)
		{
			if (titleSaves.at(i).getId() == id)
			{
				titleSaves.at(i).refreshDirectories();
			}
		}
	}
	else
	{
		for (size_t i = 0; i < titleExtdatas.size(); i++)
		{
			if (titleExtdatas.at(i).getId() == id)
			{
				titleExtdatas.at(i).refreshDirectories();
			}
		}
	}
}

static void exportTitleListCache(std::vector<Title> list, const std::u16string path)
{
	u8* cache = new u8[list.size() * 10];
	for (size_t i = 0; i < list.size(); i++)
	{
		u64 id = list.at(i).getId();
		FS_MediaType media = list.at(i).getMediaType();
		FS_CardType card = list.at(i).getCardType();
		memcpy(cache + i*10 + 0, &id, sizeof(u64));
		memcpy(cache + i*10 + 8, &media, sizeof(u8));
		memcpy(cache + i*10 + 9, &card, sizeof(u8));
	}
	FSUSER_DeleteFile(getArchiveSDMC(), fsMakePath(PATH_UTF16, path.data()));
	FSStream output(getArchiveSDMC(), path, FS_OPEN_WRITE, list.size() * 10);
	output.write(cache, list.size() * 10);
	output.close();
	delete[] cache;
}

static void importTitleListCache(void)
{
	FSStream inputsaves(getArchiveSDMC(), u8tou16("/3ds/Checkpoint/savecache"), FS_OPEN_READ);
	u32 sizesaves = inputsaves.getSize() / 10;
	u8* cachesaves = new u8[inputsaves.getSize()];
	inputsaves.read(cachesaves, inputsaves.getSize());
	inputsaves.close();
	
	FSStream inputextdatas(getArchiveSDMC(), u8tou16("/3ds/Checkpoint/extdatacache"), FS_OPEN_READ);
	u32 sizeextdatas = inputextdatas.getSize() / 10;
	u8* cacheextdatas = new u8[inputextdatas.getSize()];
	inputextdatas.read(cacheextdatas, inputextdatas.getSize());
	inputextdatas.close();
	
	// fill the lists with blank titles firsts
	for (size_t i = 0, sz = std::max(sizesaves, sizeextdatas); i < sz; i++)
	{
		Title title;
		title.load();
		if (i < sizesaves)
		{
			titleSaves.push_back(title);
		}
		if (i < sizeextdatas)
		{
			titleExtdatas.push_back(title);
		}
	}
	
	// store already loaded ids
	std::vector<u64> alreadystored;
	
	for (size_t i = 0; i < sizesaves; i++)
	{
		u64 id;
		FS_MediaType media;
		FS_CardType card;
		memcpy(&id, cachesaves + i*10, sizeof(u64));
		memcpy(&media, cachesaves + i*10 + 8, sizeof(u8));
		memcpy(&card, cachesaves + i*10 + 9, sizeof(u8));
		Title title;
		title.load(id, media, card);
		titleSaves.at(i) = title;
		alreadystored.push_back(id);
	}
	
	for (size_t i = 0; i < sizeextdatas; i++)
	{
		u64 id;
		memcpy(&id, cacheextdatas + i*10, sizeof(u64));
		std::vector<u64>::iterator it = find(alreadystored.begin(), alreadystored.end(), id);
		if (it == alreadystored.end())
		{
			FS_MediaType media;
			FS_CardType card;
			memcpy(&media, cacheextdatas + i*10 + 8, sizeof(u8));
			memcpy(&card, cacheextdatas + i*10 + 9, sizeof(u8));
			Title title;
			title.load(id, media, card);
			titleExtdatas.at(i) = title;			
		}
		else
		{
			auto pos = it - alreadystored.begin();
			
			// avoid to copy a cartridge title into the extdata list twice
			if (i != 0 && pos == 0)
			{
				auto newpos = find(alreadystored.rbegin(), alreadystored.rend(), id);
				titleExtdatas.at(i) = titleSaves.at(alreadystored.rend() - newpos);
			}
			else
			{
				titleExtdatas.at(i) = titleSaves.at(pos);
			}
		}
	}
	
	delete[] cachesaves;
	delete[] cacheextdatas;
}
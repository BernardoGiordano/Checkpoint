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

#ifndef TITLE_H
#define TITLE_H

#include "spi.h"
#include "common.h"

class Title
{
public:
	void load(void);
	bool load(u64 id, FS_MediaType mediaType, FS_CardType cardType);
	bool getAccessibleSave(void);
	bool getAccessibleExtdata(void);
	
	std::string getMediatypeString(void);
	std::string getShortDescription(void);
	std::string getLongDescription(void);
	
	std::u16string getBackupPath(void);
	std::u16string getExtdataPath(void);
	std::vector<std::u16string> getDirectories(void);
	std::vector<std::u16string> getExtdatas(void);
	
	void refreshDirectories(void);
	u32 getHighId(void);
	u32 getLowId(void);
	u32 getUniqueId(void);
	u64 getId(void);
	u32 getExtdataId(void);
	FS_MediaType getMediaType(void);
	FS_CardType getCardType(void);
	CardType getSPICardType(void);
	size_t getTextureId(void);
	
	char productCode[16];
	
private:
	bool accessibleSave;
	bool accessibleExtdata;
	std::u16string shortDescription;
	std::u16string longDescription;
	std::u16string backupPath;
	std::u16string extdataPath;
	
	std::vector<std::u16string> directories;
	std::vector<std::u16string> extdatas;
	u64 id;
	FS_MediaType media;
	FS_CardType card;
	CardType cardType;
	size_t textureId;
};

void getTitle(Title &dst, int i);
int getTitlesCount(void);
size_t getTextureId(int i);

void loadFilter(void);
void loadTitles(bool forceRefresh);
void refreshDirectories(u64 id);

#endif
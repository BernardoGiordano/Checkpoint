/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#ifndef TITLE_HPP
#define TITLE_HPP

#include "archive.hpp"
#include "configuration.hpp"
#include "directory.hpp"
#include "fsstream.hpp"
#include "gui.hpp"
#include "io.hpp"
#include "smdh.hpp"
#include "spi.hpp"
#include "util.hpp"
#include <algorithm>
#include <citro2d.h>
#include <string>
#include <vector>

extern "C" {
#include "sha256.h"
}

#define TID_PKSM 0x000400000EC10000

class Title {
public:
    ~Title(void);

    bool accessibleSave(void);
    bool accessibleExtdata(void);
    FS_CardType cardType(void);
    std::vector<std::u16string> extdata(void);
    u32 extdataId(void);
    std::u16string extdataPath(void);
    std::u16string fullExtdataPath(size_t index);
    u32 highId(void);
    C2D_Image icon(void);
    u64 id(void);
    bool isActivityLog(void);
    void load(void);
    bool load(u64 id, FS_MediaType mediaType, FS_CardType cardType);
    void load(u64 id, u8* productCode, bool accessibleSave, bool accessibleExtdata, std::u16string shortDescription, std::u16string longDescription,
        std::u16string savePath, std::u16string extdataPath, FS_MediaType media, FS_CardType cardType, CardType card);
    std::string longDescription(void);
    std::u16string getLongDescription(void);
    u32 lowId(void);
    FS_MediaType mediaType(void);
    std::string mediaTypeString(void);
    void refreshDirectories(void);
    std::u16string savePath(void);
    std::u16string fullSavePath(size_t index);
    std::vector<std::u16string> saves(void);
    void setIcon(C2D_Image icon);
    std::string shortDescription(void);
    std::u16string getShortDescription(void);
    CardType SPICardType(void);
    u32 uniqueId(void);

    char productCode[16];

private:
    bool mAccessibleSave;
    bool mAccessibleExtdata;
    std::u16string mShortDescription;
    std::u16string mLongDescription;
    std::u16string mSavePath;
    std::u16string mExtdataPath;

    std::vector<std::u16string> mSaves;
    std::vector<std::u16string> mFullSavePaths;
    std::vector<std::u16string> mExtdata;
    std::vector<std::u16string> mFullExtdataPaths;
    u64 mId;
    FS_MediaType mMedia;
    FS_CardType mCard;
    CardType mCardType;
    C2D_Image mIcon;
};

void getTitle(Title& dst, int i);
int getTitleCount(void);
C2D_Image icon(int i);
bool favorite(int i);

void loadFilter(void);
void loadTitles(bool forceRefresh);
void refreshDirectories(u64 id);
void updateCard(void);

#endif
/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#include <3ds.h>
#include <citro2d.h>

#include <vector>
#include <string>

#include "spi.hpp"
#include "backupable.hpp"
#include "inputdata.hpp"

struct TitleInfo {
    TitleInfo(u64 _id, FS_MediaType _media, FS_CardType _card);

    u64 mId;
    char mUniqueIdText[8]; // contains gamecode for DS carts
    char mProductCode[16];
    char mShortDesc[0x80];
    char mLongDesc[0x100];
    u8 mIconData[48 * 48 * 2];
    // tell 3ds and ds apart
    FS_CardType mCard;
    FS_MediaType mMedia;
    // ds specific
    SPI::CardType mCardType;

    bool mHasSave;
    bool mIsGBAVC;
    bool mHasExtdata;

    C2D_Image mIcon;
    Tex3DS_SubTexture mSubTex;

    u32 uniqueId() const;
    u32 highId() const;
    u32 lowId() const;
    u32 extdataId() const;
    const char* mediatypeString() const;
};

struct Title {
    static void loaderThreadFunc(void* arg);
    static void freeIcons();
    static bool isActivityLog(const TitleInfo& info);
    static std::string saveBackupsDir(const TitleInfo& info);
    static std::string extdataBackupsDir(const TitleInfo& info);

    Title(u64 _id, FS_MediaType _media, FS_CardType _card);
    Title(TitleInfo&& info);
    void refreshDirectories();

    TitleInfo mInfo;
    bool mValid;

    // int is index into configuration extra path, with -1 being in the default path
    std::vector<std::pair<int, std::string>> mSaveBackups, mExtdataBackups;
};

struct TitleHolder : public BackupInfo {
    TitleHolder(std::vector<Title>& vec) : ts(&vec), titleIdx(vec.size())
    { }

    std::vector<Title>* ts;
    size_t titleIdx;

    BackupInfo& getInfo(); 

    virtual void drawInfo(DrawDataHolder& d) override final;
    virtual C2D_Image getIcon() override final;
    virtual bool favorite() override final;
    virtual BackupInfo::SpecialInfoResult getSpecialInfo(BackupInfo::SpecialInfo special) override final;
    virtual std::string getCheatKey() override final;
    
};

#define TitleHolderImpl(name) struct name : public TitleHolder { \
    name(std::vector<Title>& vec) : TitleHolder(vec) { }; \
    Backupable::ActionResult backup(InputDataHolder& i); \
    Backupable::ActionResult restore(InputDataHolder& i); \
    Backupable::ActionResult deleteBackup(size_t idx); \
    virtual const std::vector<std::pair<int, std::string>>& getBackupsList() override final; \
};

TitleHolderImpl(DSSaveTitleHolder)
TitleHolderImpl(GBASaveTitleHolder)
TitleHolderImpl(SaveTitleHolder)
TitleHolderImpl(ExtdataTitleHolder)

#endif

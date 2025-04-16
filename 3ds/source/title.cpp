/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2021 Bernardo Giordano, FlagBrew
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

#include "title.hpp"
#include "loader.hpp"
#include "main.hpp"
#include <chrono>

static constexpr Tex3DS_SubTexture dsIconSubt3x = {32, 32, 0.0f, 1.0f, 1.0f, 0.0f};
static C2D_Image dsIcon                         = {nullptr, &dsIconSubt3x};

C2D_Image loadTextureFromBytes(u16* bigIconData)
{
    C3D_Tex* tex                          = (C3D_Tex*)malloc(sizeof(C3D_Tex));
    static const Tex3DS_SubTexture subt3x = {48, 48, 0.0f, 48 / 64.0f, 48 / 64.0f, 0.0f};
    C2D_Image image                       = (C2D_Image){tex, &subt3x};
    C3D_TexInit(image.tex, 64, 64, GPU_RGB565);

    u16* dest = (u16*)image.tex->data + (64 - 48) * 64;
    u16* src  = bigIconData;
    for (int j = 0; j < 48; j += 8) {
        memcpy(dest, src, 48 * 8 * sizeof(u16));
        src += 48 * 8;
        dest += 64 * 8;
    }

    return image;
}

static C2D_Image loadTextureIcon(smdh_s* smdh)
{
    return loadTextureFromBytes(smdh->bigIconData);
}

static void loadDSIcon(u8* banner)
{
    static constexpr int WIDTH_POW2  = 32;
    static constexpr int HEIGHT_POW2 = 32;
    if (!dsIcon.tex) {
        dsIcon.tex = new C3D_Tex;
        C3D_TexInit(dsIcon.tex, WIDTH_POW2, HEIGHT_POW2, GPU_RGB565);
    }

    struct bannerData {
        u16 version;
        u16 crc;
        u8 reserved[28];
        u8 data[512];
        u16 palette[16];
    };
    bannerData* iconData = (bannerData*)banner;

    u16* output = (u16*)dsIcon.tex->data;
    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            u32 srcOff   = (((y >> 3) * 4 + (x >> 3)) * 8 + (y & 7)) * 4 + ((x & 7) >> 1);
            u32 srcShift = (x & 1) * 4;

            u16 pIndex = (iconData->data[srcOff] >> srcShift) & 0xF;
            u16 color  = 0xFFFF;
            if (pIndex != 0) {
                u16 r = iconData->palette[pIndex] & 0x1F;
                u16 g = (iconData->palette[pIndex] >> 5) & 0x1F;
                u16 b = (iconData->palette[pIndex] >> 10) & 0x1F;
                color = (r << 11) | (g << 6) | (g >> 4) | (b);
            }

            u32 dst     = ((((y >> 3) * (32 >> 3) + (x >> 3)) << 6) +
                       ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
            output[dst] = color;
        }
    }
}

void Title::load(void)
{
    mId    = 0xFFFFFFFFFFFFFFFF;
    mMedia = MEDIATYPE_SD;
    mCard  = CARD_CTR;
    memset(productCode, 0, 16);
    mShortDescription  = StringUtils::UTF8toUTF16("");
    mLongDescription   = StringUtils::UTF8toUTF16("");
    mSavePath          = StringUtils::UTF8toUTF16("");
    mExtdataPath       = StringUtils::UTF8toUTF16("");
    mAccessibleSave    = false;
    mAccessibleExtdata = false;
    mSaves.clear();
    mExtdata.clear();
}

void Title::load(u64 id, u8* _productCode, bool accessibleSave, bool accessibleExtdata, std::u16string shortDescription,
    std::u16string longDescription, std::u16string savePath, std::u16string extdataPath, FS_MediaType media, FS_CardType cardType, CardType card)
{
    mId                = id;
    mAccessibleSave    = accessibleSave;
    mAccessibleExtdata = accessibleExtdata;
    mShortDescription  = shortDescription;
    mLongDescription   = longDescription;
    mSavePath          = savePath;
    mExtdataPath       = extdataPath;
    mMedia             = media;
    mCard              = cardType;
    mCardType          = card;

    memcpy(productCode, _productCode, 16);
}

bool Title::load(u64 _id, FS_MediaType _media, FS_CardType _card)
{
    bool loadTitle = false;
    mId            = _id;
    mMedia         = _media;
    mCard          = _card;

    if (mCard == CARD_CTR) {
        smdh_s* smdh;
        if (mId == TID_PKSM) {
            smdh = loadSMDH("romfs:/PKSM.smdh");
        }
        else {
            smdh = loadSMDH(lowId(), highId(), mMedia);
        }
        if (smdh == NULL) {
            Logging::error("Failed to load title {:X} due to smdh == NULL", mId);
            return false;
        }

        char unique[12] = {0};
        sprintf(unique, "0x%05X ", (unsigned int)uniqueId());

        mShortDescription = StringUtils::removeForbiddenCharacters((char16_t*)smdh->applicationTitles[1].shortDescription);
        mLongDescription  = (char16_t*)smdh->applicationTitles[1].longDescription;
        mSavePath         = StringUtils::UTF8toUTF16("/3ds/Checkpoint/saves/") + StringUtils::UTF8toUTF16(unique) + mShortDescription;
        mExtdataPath      = StringUtils::UTF8toUTF16("/3ds/Checkpoint/extdata/") + StringUtils::UTF8toUTF16(unique) + mShortDescription;
        AM_GetTitleProductCode(mMedia, mId, productCode);

        mAccessibleSave    = Archive::accessible(mediaType(), lowId(), highId());
        mAccessibleExtdata = mMedia == MEDIATYPE_NAND ? false : Archive::accessible(extdataId());

        if (mAccessibleSave) {
            loadTitle = true;
            if (!io::directoryExists(Archive::sdmc(), mSavePath)) {
                Result res = io::createDirectory(Archive::sdmc(), mSavePath);
                if (R_FAILED(res)) {
                    loadTitle = false;
                    Logging::error("Failed to create backup directory with result 0x{:08X}.", res);
                }
            }
        }

        if (mAccessibleExtdata) {
            loadTitle = true;
            if (!io::directoryExists(Archive::sdmc(), mExtdataPath)) {
                Result res = io::createDirectory(Archive::sdmc(), mExtdataPath);
                if (R_FAILED(res)) {
                    loadTitle = false;
                    Logging::error("Failed to create backup directory with result 0x{:08X}.", res);
                }
            }
        }

        if (loadTitle) {
            mIcon = loadTextureIcon(smdh);
        }

        delete smdh;
    }
    else {
        u8* headerData = new u8[0x3B4];
        Result res     = FSUSER_GetLegacyRomHeader(mMedia, 0LL, headerData);
        if (R_FAILED(res)) {
            delete[] headerData;
            Logging::error("Failed get legacy rom header with result 0x{:08X}.", res);
            return false;
        }

        char _cardTitle[14] = {0};
        char _gameCode[6]   = {0};

        std::copy(headerData, headerData + 12, _cardTitle);
        std::copy(headerData + 12, headerData + 16, _gameCode);
        _cardTitle[13] = '\0';
        _gameCode[5]   = '\0';

        delete[] headerData;
        headerData = new u8[0x23C0];
        FSUSER_GetLegacyBannerData(mMedia, 0LL, headerData);
        loadDSIcon(headerData);
        mIcon = dsIcon;
        delete[] headerData;

        res = SPIGetCardType(&mCardType, (_gameCode[0] == 'I') ? 1 : 0);
        if (R_FAILED(res)) {
            Logging::error("Failed get SPI Card Type with result 0x{:08X}.", res);
            return false;
        }

        mShortDescription = StringUtils::removeForbiddenCharacters(StringUtils::UTF8toUTF16(_cardTitle));
        mLongDescription  = mShortDescription;
        mSavePath         = StringUtils::UTF8toUTF16("/3ds/Checkpoint/saves/") + StringUtils::UTF8toUTF16(_gameCode) + StringUtils::UTF8toUTF16(" ") +
                    mShortDescription;
        mExtdataPath = mSavePath;
        memset(productCode, 0, 16);

        mAccessibleSave    = true;
        mAccessibleExtdata = false;

        loadTitle = true;
        if (!io::directoryExists(Archive::sdmc(), mSavePath)) {
            res = io::createDirectory(Archive::sdmc(), mSavePath);
            if (R_FAILED(res)) {
                loadTitle = false;
                Logging::error("Failed to create backup directory with result 0x{:08X}.", res);
            }
        }
    }

    refreshDirectories();
    return loadTitle;
}

Title::~Title(void) {}

bool Title::accessibleSave(void)
{
    return mAccessibleSave;
}

bool Title::accessibleExtdata(void)
{
    return mAccessibleExtdata;
}

std::string Title::mediaTypeString(void)
{
    switch (mMedia) {
        case MEDIATYPE_SD:
            return "SD Card";
        case MEDIATYPE_GAME_CARD:
            return "Cartridge";
        case MEDIATYPE_NAND:
            return "NAND";
        default:
            return " ";
    }

    return " ";
}

std::string Title::shortDescription(void)
{
    return StringUtils::UTF16toUTF8(mShortDescription);
}

std::u16string Title::getShortDescription(void)
{
    return mShortDescription;
}

std::string Title::longDescription(void)
{
    return StringUtils::UTF16toUTF8(mLongDescription);
}

std::u16string Title::getLongDescription(void)
{
    return mLongDescription;
}

std::u16string Title::savePath(void)
{
    return mSavePath;
}

std::u16string Title::extdataPath(void)
{
    return mExtdataPath;
}

std::u16string Title::fullSavePath(size_t index)
{
    return mFullSavePaths.at(index);
}

std::u16string Title::fullExtdataPath(size_t index)
{
    return mFullExtdataPaths.at(index);
}

std::vector<std::u16string> Title::saves(void)
{
    return mSaves;
}

std::vector<std::u16string> Title::extdata(void)
{
    return mExtdata;
}

void Title::refreshDirectories(void)
{
    mSaves.clear();
    mExtdata.clear();
    mFullSavePaths.clear();
    mFullExtdataPaths.clear();

    if (accessibleSave()) {
        // standard save backups
        Directory savelist(Archive::sdmc(), mSavePath);
        if (savelist.good()) {
            for (size_t i = 0, sz = savelist.size(); i < sz; i++) {
                if (savelist.folder(i)) {
                    mSaves.push_back(savelist.entry(i));
                    mFullSavePaths.push_back(mSavePath + StringUtils::UTF8toUTF16("/") + savelist.entry(i));
                }
            }

            std::sort(mSaves.rbegin(), mSaves.rend());
            std::sort(mFullSavePaths.rbegin(), mFullSavePaths.rend());
            mSaves.insert(mSaves.begin(), StringUtils::UTF8toUTF16("New..."));
            mFullSavePaths.insert(mFullSavePaths.begin(), StringUtils::UTF8toUTF16("New..."));
        }
        else {
            Logging::error("Couldn't retrieve the save directory list for the title {}", shortDescription());
        }

        // save backups from configuration
        try {
            std::vector<std::u16string> additionalFolders = Configuration::getInstance().additionalSaveFolders(mId);
            if (!additionalFolders.empty()) {
                Logging::debug("Found {} additional save folders for title {:X}", additionalFolders.size(), mId);
                for (std::vector<std::u16string>::const_iterator it = additionalFolders.begin(); it != additionalFolders.end(); ++it) {
                    Logging::debug("Processing additional save folder: {}", StringUtils::UTF16toUTF8(*it));
                    if (io::directoryExists(Archive::sdmc(), *it)) {
                        Logging::debug("Additional save folder exists: {}", StringUtils::UTF16toUTF8(*it));
                        // we have other folders to parse
                        Directory list(Archive::sdmc(), *it);
                        if (list.good()) {
                            Logging::debug("Additional save folder is good: {}", StringUtils::UTF16toUTF8(*it));
                            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                                if (list.folder(i)) {
                                    Logging::debug("Found save folder: {}", StringUtils::UTF16toUTF8(list.entry(i)));
                                    mSaves.push_back(list.entry(i));
                                    mFullSavePaths.push_back(*it + StringUtils::UTF8toUTF16("/") + list.entry(i));
                                }
                            }
                        }
                        else {
                            Logging::error("Additional save folder is not good: {}", StringUtils::UTF16toUTF8(*it));
                        }
                    }
                    else {
                        Logging::error("Additional save folder does not exist: {}", StringUtils::UTF16toUTF8(*it));
                    }
                }
                Logging::debug("Finished processing additional save folders for title {:X}", mId);
            }
        }
        catch (const std::exception& e) {
            Logging::error("Exception when processing additional save folders: {}", e.what());
        }
        catch (...) {
            Logging::error("Unknown exception when processing additional save folders for title {:X}", mId);
        }
    }

    if (accessibleExtdata()) {
        // extdata backups
        Directory extlist(Archive::sdmc(), mExtdataPath);
        if (extlist.good()) {
            for (size_t i = 0, sz = extlist.size(); i < sz; i++) {
                if (extlist.folder(i)) {
                    mExtdata.push_back(extlist.entry(i));
                    mFullExtdataPaths.push_back(mExtdataPath + StringUtils::UTF8toUTF16("/") + extlist.entry(i));
                }
            }

            std::sort(mExtdata.begin(), mExtdata.end());
            std::sort(mFullExtdataPaths.begin(), mFullExtdataPaths.end());
            mExtdata.insert(mExtdata.begin(), StringUtils::UTF8toUTF16("New..."));
            mFullExtdataPaths.insert(mFullExtdataPaths.begin(), StringUtils::UTF8toUTF16("New..."));
        }
        else {
            Logging::error("Couldn't retrieve the extdata directory list for the title {}", shortDescription());
        }

        // extdata backups from configuration
        try {
            std::vector<std::u16string> additionalFolders = Configuration::getInstance().additionalExtdataFolders(mId);
            if (!additionalFolders.empty()) {
                Logging::debug("Found {} additional extdata folders for title {:X}", additionalFolders.size(), mId);
                for (std::vector<std::u16string>::const_iterator it = additionalFolders.begin(); it != additionalFolders.end(); ++it) {
                    Logging::debug("Processing additional extdata folder: {}", StringUtils::UTF16toUTF8(*it));
                    if (io::directoryExists(Archive::sdmc(), *it)) {
                        Logging::debug("Additional extdata folder exists: {}", StringUtils::UTF16toUTF8(*it));
                        // we have other folders to parse
                        Directory list(Archive::sdmc(), *it);
                        if (list.good()) {
                            Logging::debug("Additional extdata folder is good: {}", StringUtils::UTF16toUTF8(*it));
                            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                                if (list.folder(i)) {
                                    Logging::debug("Found extdata folder: {}", StringUtils::UTF16toUTF8(list.entry(i)));
                                    mExtdata.push_back(list.entry(i));
                                    mFullExtdataPaths.push_back(*it + StringUtils::UTF8toUTF16("/") + list.entry(i));
                                }
                            }
                        }
                        else {
                            Logging::error("Additional extdata folder is not good: {}", StringUtils::UTF16toUTF8(*it));
                        }
                    }
                    else {
                        Logging::error("Additional extdata folder does not exist: {}", StringUtils::UTF16toUTF8(*it));
                    }
                }
                Logging::debug("Finished processing additional extdata folders for title {:X}", mId);
            }
        }
        catch (const std::exception& e) {
            Logging::error("Exception when processing additional extdata folders: {}", e.what());
        }
        catch (...) {
            Logging::error("Unknown exception when processing additional extdata folders for title {:X}", mId);
        }
    }
}

u32 Title::highId(void)
{
    return (u32)(mId >> 32);
}

u32 Title::lowId(void)
{
    return (u32)mId;
}

u32 Title::uniqueId(void)
{
    return (lowId() >> 8);
}

u64 Title::id(void)
{
    return mId;
}

u32 Title::extdataId(void)
{
    u32 low = lowId();
    switch (low) {
        case 0x00055E00:
            return 0x055D; // Pokémon Y
        case 0x0011C400:
            return 0x11C5; // Pokémon Omega Ruby
        case 0x00175E00:
            return 0x1648; // Pokémon Moon
        case 0x00179600:
        case 0x00179800:
            return 0x1794; // Fire Emblem Conquest SE NA
        case 0x00179700:
        case 0x0017A800:
            return 0x1795; // Fire Emblem Conquest SE EU
        case 0x0012DD00:
        case 0x0012DE00:
            return 0x12DC; // Fire Emblem If JP
        case 0x001B5100:
            return 0x1B50; // Pokémon Ultramoon
                           // TODO: need confirmation for this
                           // case 0x001C5100:
                           // case 0x001C5300:
                           //     return 0x0BD3; // Etrian Odyssey V: Beyond the Myth
    }

    return low >> 8;
}

FS_MediaType Title::mediaType(void)
{
    return mMedia;
}

FS_CardType Title::cardType(void)
{
    return mCard;
}

CardType Title::SPICardType(void)
{
    return mCardType;
}

C2D_Image Title::icon(void)
{
    return mIcon;
}

void Title::setIcon(C2D_Image icon)
{
    mIcon = icon;
}

bool Title::isActivityLog(void)
{
    bool activityId = false;
    switch (lowId()) {
        case 0x00020200:
        case 0x00021200:
        case 0x00022200:
        case 0x00026200:
        case 0x00027200:
        case 0x00028200:
            activityId = true;
    }
    return mMedia == MEDIATYPE_NAND && activityId;
}
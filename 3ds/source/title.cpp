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

static bool validId(u64 id);
static C2D_Image loadTextureIcon(smdh_s* smdh);

static std::vector<Title> titleSaves;
static std::vector<Title> titleExtdatas;

static void exportTitleListCache(std::vector<Title>& list, const std::u16string& path);
static void importTitleListCache(void);

static constexpr Tex3DS_SubTexture dsIconSubt3x = {32, 32, 0.0f, 1.0f, 1.0f, 0.0f};
static C2D_Image dsIcon                         = {nullptr, &dsIconSubt3x};

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
            Logger::getInstance().log(Logger::ERROR, "Failed to load title 0x%lX due to smdh == NULL", mId);
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
                    Logger::getInstance().log(Logger::ERROR, "Failed to create backup directory with result 0x%08lX.", res);
                }
            }
        }

        if (mAccessibleExtdata) {
            loadTitle = true;
            if (!io::directoryExists(Archive::sdmc(), mExtdataPath)) {
                Result res = io::createDirectory(Archive::sdmc(), mExtdataPath);
                if (R_FAILED(res)) {
                    loadTitle = false;
                    Logger::getInstance().log(Logger::ERROR, "Failed to create backup directory with result 0x%08lX.", res);
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
            Logger::getInstance().log(Logger::ERROR, "Failed get legacy rom header with result 0x%08lX.", res);
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
            Logger::getInstance().log(Logger::ERROR, "Failed get SPI Card Type with result 0x%08lX.", res);
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
                Logger::getInstance().log(Logger::ERROR, "Failed to create backup directory with result 0x%08lX.", res);
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
            Logger::getInstance().log(Logger::ERROR, "Couldn't retrieve the save directory list for the title " + shortDescription());
        }

        // save backups from configuration
        std::vector<std::u16string> additionalFolders = Configuration::getInstance().additionalSaveFolders(mId);
        for (std::vector<std::u16string>::const_iterator it = additionalFolders.begin(); it != additionalFolders.end(); ++it) {
            // we have other folders to parse
            Directory list(Archive::sdmc(), *it);
            if (list.good()) {
                for (size_t i = 0, sz = list.size(); i < sz; i++) {
                    if (list.folder(i)) {
                        mSaves.push_back(list.entry(i));
                        mFullSavePaths.push_back(*it + StringUtils::UTF8toUTF16("/") + list.entry(i));
                    }
                }
            }
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
            Logger::getInstance().log(Logger::ERROR, "Couldn't retrieve the extdata directory list for the title " + shortDescription());
        }

        // extdata backups from configuration
        std::vector<std::u16string> additionalFolders = Configuration::getInstance().additionalExtdataFolders(mId);
        for (std::vector<std::u16string>::const_iterator it = additionalFolders.begin(); it != additionalFolders.end(); ++it) {
            // we have other folders to parse
            Directory list(Archive::sdmc(), *it);
            if (list.good()) {
                for (size_t i = 0, sz = list.size(); i < sz; i++) {
                    if (list.folder(i)) {
                        mExtdata.push_back(list.entry(i));
                        mFullExtdataPaths.push_back(*it + StringUtils::UTF8toUTF16("/") + list.entry(i));
                    }
                }
            }
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

static bool validId(u64 id)
{
    // check for invalid titles
    switch ((u32)id) {
        // Instruction Manual
        case 0x00008602:
        case 0x00009202:
        case 0x00009B02:
        case 0x0000A402:
        case 0x0000AC02:
        case 0x0000B402:
        // Internet Browser
        case 0x00008802:
        case 0x00009402:
        case 0x00009D02:
        case 0x0000A602:
        case 0x0000AE02:
        case 0x0000B602:
        case 0x20008802:
        case 0x20009402:
        case 0x20009D02:
        case 0x2000AE02:
        // Garbage
        case 0x00021A00:
            return false;
    }

    // check for updates
    u32 high = id >> 32;
    if (high == 0x0004000E) {
        return false;
    }

    return !Configuration::getInstance().filter(id);
}

void loadTitles(bool forceRefresh)
{
    try {
        static const std::u16string savecachePath    = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullsavecache");
        static const std::u16string extdatacachePath = StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullextdatacache");

        // on refreshing
        titleSaves.clear();
        titleExtdatas.clear();

        bool optimizedLoad = false;

        u8 hash[SHA256_BLOCK_SIZE];
        calculateTitleDBHash(hash);

        std::u16string titlesHashPath = StringUtils::UTF8toUTF16("/3ds/Checkpoint/titles.sha");
        if (!io::fileExists(Archive::sdmc(), titlesHashPath) || !io::fileExists(Archive::sdmc(), savecachePath) ||
            !io::fileExists(Archive::sdmc(), extdatacachePath)) {
            // create title list sha256 hash file if it doesn't exist in the working directory
            FSStream output(Archive::sdmc(), titlesHashPath, FS_OPEN_WRITE, SHA256_BLOCK_SIZE);
            output.write(hash, SHA256_BLOCK_SIZE);
            output.close();
        }
        else {
            // compare current hash with the previous hash
            FSStream input(Archive::sdmc(), titlesHashPath, FS_OPEN_READ);
            if (input.good() && input.size() == SHA256_BLOCK_SIZE) {
                u8* buf = new u8[input.size()];
                input.read(buf, input.size());
                input.close();

                if (memcmp(hash, buf, SHA256_BLOCK_SIZE) == 0) {
                    // hash matches
                    optimizedLoad = true;
                }
                else {
                    FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, titlesHashPath.data()));
                    FSStream output(Archive::sdmc(), titlesHashPath, FS_OPEN_WRITE, SHA256_BLOCK_SIZE);
                    output.write(hash, SHA256_BLOCK_SIZE);
                    output.close();
                }

                delete[] buf;
            }
        }

        if (optimizedLoad && !forceRefresh) {
            // deserialize data
            importTitleListCache();
            for (auto& title : titleSaves) {
                title.refreshDirectories();
            }
            for (auto& title : titleExtdatas) {
                title.refreshDirectories();
            }
        }
        else {
            u32 count = 0;

            if (Configuration::getInstance().nandSaves()) {
                AM_GetTitleCount(MEDIATYPE_NAND, &count);
                std::unique_ptr<u64[]> ids_nand = std::unique_ptr<u64[]>(new u64[count]);
                AM_GetTitleList(NULL, MEDIATYPE_NAND, count, ids_nand.get());

                for (u32 i = 0; i < count; i++) {
                    if (validId(ids_nand[i])) {
                        Title title;
                        if (title.load(ids_nand[i], MEDIATYPE_NAND, CARD_CTR)) {
                            if (title.accessibleSave()) {
                                titleSaves.push_back(title);
                            }
                            // TODO: extdata?
                        }
                    }
                }
            }

            count = 0;
            AM_GetTitleCount(MEDIATYPE_SD, &count);
            std::unique_ptr<u64[]> ids = std::unique_ptr<u64[]>(new u64[count]);
            AM_GetTitleList(NULL, MEDIATYPE_SD, count, ids.get());

            for (u32 i = 0; i < count; i++) {
                if (validId(ids[i])) {
                    Title title;
                    if (title.load(ids[i], MEDIATYPE_SD, CARD_CTR)) {
                        if (title.accessibleSave()) {
                            titleSaves.push_back(title);
                        }

                        if (title.accessibleExtdata()) {
                            titleExtdatas.push_back(title);
                        }
                    }
                }
            }

            // always check for PKSM's extdata archive
            bool isPKSMIdAlreadyHere = false;
            for (u32 i = 0; i < count; i++) {
                if (ids[i] == TID_PKSM) {
                    isPKSMIdAlreadyHere = true;
                    break;
                }
            }
            if (!isPKSMIdAlreadyHere) {
                Title title;
                if (title.load(TID_PKSM, MEDIATYPE_SD, CARD_CTR)) {
                    if (title.accessibleExtdata()) {
                        titleExtdatas.push_back(title);
                    }
                }
            }
        }

        std::sort(titleSaves.begin(), titleSaves.end(), [](Title& l, Title& r) {
            if (Configuration::getInstance().favorite(l.id()) != Configuration::getInstance().favorite(r.id())) {
                return Configuration::getInstance().favorite(l.id());
            }
            else {
                return l.shortDescription() < r.shortDescription();
            }
        });

        std::sort(titleExtdatas.begin(), titleExtdatas.end(), [](Title& l, Title& r) {
            if (Configuration::getInstance().favorite(l.id()) != Configuration::getInstance().favorite(r.id())) {
                return Configuration::getInstance().favorite(l.id());
            }
            else {
                return l.shortDescription() < r.shortDescription();
            }
        });

        // serialize data
        exportTitleListCache(titleSaves, savecachePath);
        exportTitleListCache(titleExtdatas, extdatacachePath);

        FS_CardType cardType;
        Result res = FSUSER_GetCardType(&cardType);
        if (R_SUCCEEDED(res)) {
            if (cardType == CARD_CTR) {
                u32 count = 0;
                AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
                if (count > 0) {
                    std::unique_ptr<u64[]> ids = std::unique_ptr<u64[]>(new u64[count]);
                    AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, count, ids.get());
                    if (validId(ids[0])) {
                        Title title;
                        if (title.load(ids[0], MEDIATYPE_GAME_CARD, cardType)) {
                            if (title.accessibleSave()) {
                                titleSaves.insert(titleSaves.begin(), title);
                            }

                            if (title.accessibleExtdata()) {
                                titleExtdatas.insert(titleExtdatas.begin(), title);
                            }
                        }
                    }
                }
            }
            else {
                Title title;
                if (title.load(0, MEDIATYPE_GAME_CARD, cardType)) {
                    titleSaves.insert(titleSaves.begin(), title);
                }
            }
        }
    }
    catch (const std::exception& e) {
    }
    catch(...) {}
}

void getTitle(Title& dst, int i)
{
    const Mode_t mode = Archive::mode();
    if (i < getTitleCount()) {
        dst = mode == MODE_SAVE ? titleSaves.at(i) : titleExtdatas.at(i);
    }
}

int getTitleCount(void)
{
    const Mode_t mode = Archive::mode();
    return mode == MODE_SAVE ? titleSaves.size() : titleExtdatas.size();
}

void Title::setIcon(C2D_Image icon)
{
    mIcon = icon;
}

C2D_Image icon(int i)
{
    const Mode_t mode = Archive::mode();
    return mode == MODE_SAVE ? titleSaves.at(i).icon() : titleExtdatas.at(i).icon();
}

bool favorite(int i)
{
    const Mode_t mode = Archive::mode();
    u64 id            = mode == MODE_SAVE ? titleSaves.at(i).id() : titleExtdatas.at(i).id();
    return Configuration::getInstance().favorite(id);
}

static C2D_Image loadTextureFromBytes(u16* bigIconData)
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

void refreshDirectories(u64 id)
{
    const Mode_t mode = Archive::mode();
    if (mode == MODE_SAVE) {
        for (size_t i = 0; i < titleSaves.size(); i++) {
            if (titleSaves.at(i).id() == id) {
                titleSaves.at(i).refreshDirectories();
            }
        }
    }
    else {
        for (size_t i = 0; i < titleExtdatas.size(); i++) {
            if (titleExtdatas.at(i).id() == id) {
                titleExtdatas.at(i).refreshDirectories();
            }
        }
    }
}

static const size_t ENTRYSIZE = 5341;

/**
 * CACHE STRUCTURE
 * start, len
 * id (0, 8)
 * productCode (8, 16)
 * accessibleSave (24, 1)
 * accessibleExtdata (25, 1)
 * shortDescription (26, 64)
 * longDescription (90, 128)
 * savePath (218, 256)
 * extdataPath (474, 256)
 * mediaType (730, 1)
 * fs cardtype (731, 1)
 * cardtype (732, 1)
 * bigIconData (733, 4608)
 */

static void exportTitleListCache(std::vector<Title>& list, const std::u16string& path)
{
    std::unique_ptr<u8[]> cache = std::unique_ptr<u8[]>(new u8[list.size() * ENTRYSIZE]());
    for (size_t i = 0; i < list.size(); i++) {
        u64 id                       = list.at(i).id();
        bool accessibleSave          = list.at(i).accessibleSave();
        bool accessibleExtdata       = list.at(i).accessibleExtdata();
        std::string shortDescription = StringUtils::UTF16toUTF8(list.at(i).getShortDescription());
        std::string longDescription  = StringUtils::UTF16toUTF8(list.at(i).getLongDescription());
        std::string savePath         = StringUtils::UTF16toUTF8(list.at(i).savePath());
        std::string extdataPath      = StringUtils::UTF16toUTF8(list.at(i).extdataPath());
        FS_MediaType media           = list.at(i).mediaType();
        FS_CardType cardType         = list.at(i).cardType();
        CardType card                = list.at(i).SPICardType();

        if (cardType == CARD_CTR) {
            smdh_s* smdh = loadSMDH(list.at(i).lowId(), list.at(i).highId(), media);
            if (smdh != NULL) {
                memcpy(cache.get() + i * ENTRYSIZE + 733, smdh->bigIconData, 0x900 * 2);
            }
            delete smdh;
        }

        memcpy(cache.get() + i * ENTRYSIZE + 0, &id, sizeof(u64));
        memcpy(cache.get() + i * ENTRYSIZE + 8, list.at(i).productCode, 16);
        memcpy(cache.get() + i * ENTRYSIZE + 24, &accessibleSave, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 25, &accessibleExtdata, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 26, shortDescription.c_str(), shortDescription.length());
        memcpy(cache.get() + i * ENTRYSIZE + 90, longDescription.c_str(), longDescription.length());
        memcpy(cache.get() + i * ENTRYSIZE + 218, savePath.c_str(), savePath.length());
        memcpy(cache.get() + i * ENTRYSIZE + 474, extdataPath.c_str(), extdataPath.length());
        memcpy(cache.get() + i * ENTRYSIZE + 730, &media, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 731, &cardType, sizeof(u8));
        memcpy(cache.get() + i * ENTRYSIZE + 732, &card, sizeof(u8));
    }

    FSUSER_DeleteFile(Archive::sdmc(), fsMakePath(PATH_UTF16, path.data()));
    FSStream output(Archive::sdmc(), path, FS_OPEN_WRITE, list.size() * ENTRYSIZE);
    output.write(cache.get(), list.size() * ENTRYSIZE);
    output.close();
}

static void importTitleListCache(void)
{
    FSStream inputsaves(Archive::sdmc(), StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullsavecache"), FS_OPEN_READ);
    u32 sizesaves  = inputsaves.size() / ENTRYSIZE;
    u8* cachesaves = new u8[inputsaves.size()];
    inputsaves.read(cachesaves, inputsaves.size());
    inputsaves.close();

    FSStream inputextdatas(Archive::sdmc(), StringUtils::UTF8toUTF16("/3ds/Checkpoint/fullextdatacache"), FS_OPEN_READ);
    u32 sizeextdatas  = inputextdatas.size() / ENTRYSIZE;
    u8* cacheextdatas = new u8[inputextdatas.size()];
    inputextdatas.read(cacheextdatas, inputextdatas.size());
    inputextdatas.close();

    // fill the lists with blank titles firsts
    for (size_t i = 0, sz = std::max(sizesaves, sizeextdatas); i < sz; i++) {
        Title title;
        title.load();
        if (i < sizesaves) {
            titleSaves.push_back(title);
        }
        if (i < sizeextdatas) {
            titleExtdatas.push_back(title);
        }
    }

    // store already loaded ids
    std::vector<u64> alreadystored;

    for (size_t i = 0; i < sizesaves; i++) {
        u64 id;
        u8 productCode[16];
        bool accessibleSave;
        bool accessibleExtdata;
        char shortDescription[0x40];
        char longDescription[0x80];
        char savePath[256];
        char extdataPath[256];
        FS_MediaType media;
        FS_CardType cardType;
        CardType card;

        memcpy(&id, cachesaves + i * ENTRYSIZE, sizeof(u64));
        memcpy(productCode, cachesaves + i * ENTRYSIZE + 8, 16);
        memcpy(&accessibleSave, cachesaves + i * ENTRYSIZE + 24, sizeof(u8));
        memcpy(&accessibleExtdata, cachesaves + i * ENTRYSIZE + 25, sizeof(u8));
        memcpy(shortDescription, cachesaves + i * ENTRYSIZE + 26, 0x40);
        memcpy(longDescription, cachesaves + i * ENTRYSIZE + 90, 0x80);
        memcpy(savePath, cachesaves + i * ENTRYSIZE + 218, 256);
        memcpy(extdataPath, cachesaves + i * ENTRYSIZE + 474, 256);
        memcpy(&media, cachesaves + i * ENTRYSIZE + 730, sizeof(u8));
        memcpy(&cardType, cachesaves + i * ENTRYSIZE + 731, sizeof(u8));
        memcpy(&card, cachesaves + i * ENTRYSIZE + 732, sizeof(u8));

        Title title;
        title.load(id, productCode, accessibleSave, accessibleExtdata, StringUtils::UTF8toUTF16(shortDescription),
            StringUtils::UTF8toUTF16(longDescription), StringUtils::UTF8toUTF16(savePath), StringUtils::UTF8toUTF16(extdataPath), media, cardType,
            card);

        if (cardType == CARD_CTR) {
            u16 bigIconData[0x900];
            memcpy(bigIconData, cachesaves + i * ENTRYSIZE + 733, 0x900 * 2);
            C2D_Image smallIcon = loadTextureFromBytes(bigIconData);
            title.setIcon(smallIcon);
        }
        else {
            // this cannot happen
            title.setIcon(Gui::noIcon());
        }

        titleSaves.at(i) = title;
        alreadystored.push_back(id);
    }

    for (size_t i = 0; i < sizeextdatas; i++) {
        u64 id;
        memcpy(&id, cacheextdatas + i * ENTRYSIZE, sizeof(u64));
        std::vector<u64>::iterator it = find(alreadystored.begin(), alreadystored.end(), id);
        if (it == alreadystored.end()) {
            u8 productCode[16];
            bool accessibleSave;
            bool accessibleExtdata;
            char shortDescription[0x40];
            char longDescription[0x80];
            char savePath[256];
            char extdataPath[256];
            FS_MediaType media;
            FS_CardType cardType;
            CardType card;

            memcpy(productCode, cacheextdatas + i * ENTRYSIZE + 8, 16);
            memcpy(&accessibleSave, cacheextdatas + i * ENTRYSIZE + 24, sizeof(u8));
            memcpy(&accessibleExtdata, cacheextdatas + i * ENTRYSIZE + 25, sizeof(u8));
            memcpy(shortDescription, cacheextdatas + i * ENTRYSIZE + 26, 0x40);
            memcpy(longDescription, cacheextdatas + i * ENTRYSIZE + 90, 0x80);
            memcpy(savePath, cacheextdatas + i * ENTRYSIZE + 218, 256);
            memcpy(extdataPath, cacheextdatas + i * ENTRYSIZE + 474, 256);
            memcpy(&media, cacheextdatas + i * ENTRYSIZE + 730, sizeof(u8));
            memcpy(&cardType, cacheextdatas + i * ENTRYSIZE + 731, sizeof(u8));
            memcpy(&card, cacheextdatas + i * ENTRYSIZE + 732, sizeof(u8));

            Title title;
            title.load(id, productCode, accessibleSave, accessibleExtdata, StringUtils::UTF8toUTF16(shortDescription),
                StringUtils::UTF8toUTF16(longDescription), StringUtils::UTF8toUTF16(savePath), StringUtils::UTF8toUTF16(extdataPath), media, cardType,
                card);

            if (cardType == CARD_CTR) {
                u16 bigIconData[0x900];
                memcpy(bigIconData, cacheextdatas + i * ENTRYSIZE + 733, 0x900 * 2);
                C2D_Image smallIcon = loadTextureFromBytes(bigIconData);
                title.setIcon(smallIcon);
            }
            else {
                // this cannot happen
                title.setIcon(Gui::noIcon());
            }

            titleExtdatas.at(i) = title;
        }
        else {
            auto pos = it - alreadystored.begin();

            // avoid to copy a cartridge title into the extdata list twice
            if (i != 0 && pos == 0) {
                auto newpos         = find(alreadystored.rbegin(), alreadystored.rend(), id);
                titleExtdatas.at(i) = titleSaves.at(alreadystored.rend() - newpos - 1);
            }
            else {
                titleExtdatas.at(i) = titleSaves.at(pos);
            }
        }
    }

    delete[] cachesaves;
    delete[] cacheextdatas;
}

static bool scanCard(void)
{
    static bool isScanning = false;
    if (isScanning) {
        return false;
    }
    else {
        isScanning = true;
    }

    bool ret   = false;
    Result res = 0;
    u32 count  = 0;
    FS_CardType cardType;
    res = FSUSER_GetCardType(&cardType);
    if (R_SUCCEEDED(res)) {
        if (cardType == CARD_CTR) {
            res = AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
            if (R_SUCCEEDED(res) && count > 0) {
                u64 id;
                res = AM_GetTitleList(NULL, MEDIATYPE_GAME_CARD, count, &id);
                if (validId(id)) {
                    Title title;
                    if (title.load(id, MEDIATYPE_GAME_CARD, cardType)) {
                        ret = true;
                        if (title.accessibleSave()) {
                            if (titleSaves.empty() || titleSaves.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                                titleSaves.insert(titleSaves.begin(), title);
                            }
                        }
                        if (title.accessibleExtdata()) {
                            if (titleExtdatas.empty() || titleExtdatas.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                                titleExtdatas.insert(titleExtdatas.begin(), title);
                            }
                        }
                    }
                }
            }
        }
        else {
            Title title;
            if (title.load(0, MEDIATYPE_GAME_CARD, cardType)) {
                ret = true;
                if (titleSaves.empty() || titleSaves.at(0).mediaType() != MEDIATYPE_GAME_CARD) {
                    titleSaves.insert(titleSaves.begin(), title);
                }
            }
        }
    }
    isScanning = false;
    return ret;
}

void updateCard(void)
{
    static bool first     = true;
    static bool oldCardIn = false;
    if (first) {
        FSUSER_CardSlotIsInserted(&oldCardIn);
        first = false;
        return;
    }
    bool cardIn = false;

    FSUSER_CardSlotIsInserted(&cardIn);
    if (cardIn != oldCardIn) {
        if (cardIn) {
            bool power;
            FSUSER_CardSlotPowerOn(&power);
            while (!power) {
                FSUSER_CardSlotGetCardIFPowerStatus(&power);
            }
            oldCardIn = scanCard();
        }
        else {
            if (!titleSaves.empty() && titleSaves.at(0).mediaType() == MEDIATYPE_GAME_CARD) {
                titleSaves.erase(titleSaves.begin());
            }
            if (!titleExtdatas.empty() && titleExtdatas.at(0).mediaType() == MEDIATYPE_GAME_CARD) {
                titleExtdatas.erase(titleExtdatas.begin());
            }
            oldCardIn = false;
        }
    }
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

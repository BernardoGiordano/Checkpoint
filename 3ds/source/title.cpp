/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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
#include "titlequirks.hpp"
#include <chrono>

C2D_Image loadTextureFromBytes(u16* bigIconData)
{
    C3D_Tex* tex                          = (C3D_Tex*)malloc(sizeof(C3D_Tex));
    static const Tex3DS_SubTexture subt3x = {48, 48, 0.0f, 48 / 64.0f, 48 / 64.0f, 0.0f};
    C2D_Image image                       = (C2D_Image){tex, &subt3x};
    C3D_TexInit(image.tex, 64, 64, GPU_RGB565);
    // Bilinear filtering: the grid tile scales this 48px icon down to 44px, and
    // the default nearest filter makes that shrink look like a bad downsample.
    C3D_TexSetFilter(image.tex, GPU_LINEAR, GPU_LINEAR);

    u16* dest = (u16*)image.tex->data + (64 - 48) * 64;
    u16* src  = bigIconData;
    for (int j = 0; j < 48; j += 8) {
        memcpy(dest, src, 48 * 8 * sizeof(u16));
        src += 48 * 8;
        dest += 64 * 8;
    }

    return image;
}

void Title::load(void)
{
    mId    = 0xFFFFFFFFFFFFFFFF;
    mMedia = MEDIATYPE_SD;
    mCard  = CARD_CTR;
    memset(productCode, 0, 16);
    mShortDescription  = "";
    mLongDescription   = "";
    mSavePath          = StringUtils::UTF8toUTF16("");
    mExtdataPath       = StringUtils::UTF8toUTF16("");
    mAccessibleSave    = false;
    mAccessibleExtdata = false;
    mGBA               = false;
    mSaves.clear();
    mExtdata.clear();
}

void Title::load(u64 id, u8* _productCode, bool accessibleSave, bool saveIsGBA, bool accessibleExtdata, std::string shortDescription,
    std::string longDescription, std::u16string savePath, std::u16string extdataPath, FS_MediaType media, FS_CardType cardType, CardType card)
{
    mId                = id;
    mAccessibleSave    = accessibleSave;
    mGBA               = saveIsGBA;
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

Title::~Title(void) {}

bool Title::accessibleSave(void)
{
    return mAccessibleSave;
}

bool Title::accessibleExtdata(void)
{
    return mAccessibleExtdata;
}

bool Title::isGBAVC(void)
{
    return mGBA;
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

std::string Title::shortDescription(void) const
{
    return mShortDescription;
}

std::string Title::longDescription(void) const
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

void Title::loadBackupList(const std::u16string& basePath, const std::vector<std::u16string>& additionalFolders, bool descending,
    std::vector<std::u16string>& names, std::vector<std::u16string>& fullPaths)
{
    Directory baselist(Archive::sdmc(), basePath);
    if (baselist.good()) {
        for (size_t i = 0, sz = baselist.size(); i < sz; i++) {
            if (baselist.folder(i)) {
                names.push_back(baselist.entry(i));
                fullPaths.push_back(basePath + StringUtils::UTF8toUTF16("/") + baselist.entry(i));
            }
        }

        if (descending) {
            std::sort(names.rbegin(), names.rend());
            std::sort(fullPaths.rbegin(), fullPaths.rend());
        }
        else {
            std::sort(names.begin(), names.end());
            std::sort(fullPaths.begin(), fullPaths.end());
        }
        names.insert(names.begin(), StringUtils::UTF8toUTF16("New..."));
        fullPaths.insert(fullPaths.begin(), StringUtils::UTF8toUTF16("New..."));
    }
    else {
        Logging::error("Couldn't retrieve the directory list for the title {}", shortDescription());
    }

    // backups from configured additional folders, appended unsorted after "New..."
    try {
        for (const auto& folder : additionalFolders) {
            if (!io::directoryExists(Archive::sdmc(), folder)) {
                Logging::error("Additional folder does not exist: {}", StringUtils::UTF16toUTF8(folder));
                continue;
            }
            Directory list(Archive::sdmc(), folder);
            if (!list.good()) {
                Logging::error("Additional folder is not good: {}", StringUtils::UTF16toUTF8(folder));
                continue;
            }
            for (size_t i = 0, sz = list.size(); i < sz; i++) {
                if (list.folder(i)) {
                    names.push_back(list.entry(i));
                    fullPaths.push_back(folder + StringUtils::UTF8toUTF16("/") + list.entry(i));
                }
            }
        }
    }
    catch (const std::exception& e) {
        Logging::error("Exception when processing additional folders: {}", e.what());
    }
    catch (...) {
        Logging::error("Unknown exception when processing additional folders for title {:X}", mId);
    }
}

void Title::refreshDirectories(void)
{
    mSaves.clear();
    mExtdata.clear();
    mFullSavePaths.clear();
    mFullExtdataPaths.clear();

    if (accessibleSave() || isGBAVC()) {
        loadBackupList(mSavePath, Configuration::getInstance().additionalSaveFolders(mId), true, mSaves, mFullSavePaths);
    }

    if (accessibleExtdata()) {
        loadBackupList(mExtdataPath, Configuration::getInstance().additionalExtdataFolders(mId), false, mExtdata, mFullExtdataPaths);
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

u64 Title::id(void) const
{
    return mId;
}

u32 Title::extdataId(void)
{
    return TitleQuirks::extdataIdFor(mId);
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
    return TitleQuirks::isActivityLog(lowId(), mMedia);
}

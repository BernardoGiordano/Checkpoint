/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
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

#include "titlecache.hpp"
#include "gui.hpp"
#include "smdh.hpp"
#include <cstring>

namespace {
    // Field layout of one cache entry. The single source of truth for the format;
    // encode() and decode() are mirror images over these offsets.
    constexpr size_t OFF_ID         = 0;   // u64
    constexpr size_t OFF_PRODUCT    = 8;   // 16 bytes
    constexpr size_t OFF_ACCESS     = 24;  // u8 bitfield: bit0 save, bit1 GBA raw
    constexpr size_t OFF_ACCESS_EXT = 25;  // u8 bool
    constexpr size_t OFF_SHORT      = 26;  // 0x40
    constexpr size_t OFF_LONG       = 90;  // 0x80
    constexpr size_t OFF_SAVE_PATH  = 218; // 256
    constexpr size_t OFF_EXT_PATH   = 474; // 256
    constexpr size_t OFF_MEDIA      = 730; // u8
    constexpr size_t OFF_FS_CARD    = 731; // u8
    constexpr size_t OFF_CARD       = 732; // u8
    constexpr size_t OFF_ICON       = 733; // ICON_BYTES

    constexpr size_t SHORT_LEN  = 0x40;
    constexpr size_t LONG_LEN   = 0x80;
    constexpr size_t PATH_LEN   = 256;
    constexpr size_t ICON_BYTES = 0x900 * 2; // bigIconData: 0x900 u16 pixels
}

void TitleCache::encode(u8* dst, Title& title)
{
    std::memset(dst, 0, ENTRY_SIZE);

    u64 id = title.id();
    // bit 0: regular save accessible, bit 1: GBA VC raw save accessible
    u8 accessibleSaveRaw         = title.accessibleSave() ? 1 : (title.isGBAVC() ? 2 : 0);
    bool accessibleExtdata       = title.accessibleExtdata();
    std::string shortDescription = title.shortDescription();
    std::string longDescription  = title.longDescription();
    std::string savePath         = StringUtils::UTF16toUTF8(title.savePath());
    std::string extdataPath      = StringUtils::UTF16toUTF8(title.extdataPath());
    FS_MediaType media           = title.mediaType();
    FS_CardType cardType         = title.cardType();
    CardType card                = title.SPICardType();

    if (cardType == CARD_CTR) {
        smdh_s* smdh = loadSMDH(title.lowId(), title.highId(), media);
        if (smdh != NULL) {
            std::memcpy(dst + OFF_ICON, smdh->bigIconData, ICON_BYTES);
        }
        delete smdh;
    }

    std::memcpy(dst + OFF_ID, &id, sizeof(u64));
    std::memcpy(dst + OFF_PRODUCT, title.productCode, 16);
    std::memcpy(dst + OFF_ACCESS, &accessibleSaveRaw, sizeof(u8));
    std::memcpy(dst + OFF_ACCESS_EXT, &accessibleExtdata, sizeof(u8));
    std::memcpy(dst + OFF_SHORT, shortDescription.c_str(), shortDescription.length());
    std::memcpy(dst + OFF_LONG, longDescription.c_str(), longDescription.length());
    std::memcpy(dst + OFF_SAVE_PATH, savePath.c_str(), savePath.length());
    std::memcpy(dst + OFF_EXT_PATH, extdataPath.c_str(), extdataPath.length());
    std::memcpy(dst + OFF_MEDIA, &media, sizeof(u8));
    std::memcpy(dst + OFF_FS_CARD, &cardType, sizeof(u8));
    std::memcpy(dst + OFF_CARD, &card, sizeof(u8));
}

Title TitleCache::decode(const u8* src)
{
    u64 id;
    u8 productCode[16];
    u8 accessibleSaveRaw;
    bool accessibleExtdata;
    char shortDescription[SHORT_LEN];
    char longDescription[LONG_LEN];
    char savePath[PATH_LEN];
    char extdataPath[PATH_LEN];
    FS_MediaType media;
    FS_CardType cardType;
    CardType card;

    std::memcpy(&id, src + OFF_ID, sizeof(u64));
    std::memcpy(productCode, src + OFF_PRODUCT, 16);
    std::memcpy(&accessibleSaveRaw, src + OFF_ACCESS, sizeof(u8));
    std::memcpy(&accessibleExtdata, src + OFF_ACCESS_EXT, sizeof(u8));
    std::memcpy(shortDescription, src + OFF_SHORT, SHORT_LEN);
    std::memcpy(longDescription, src + OFF_LONG, LONG_LEN);
    std::memcpy(savePath, src + OFF_SAVE_PATH, PATH_LEN);
    std::memcpy(extdataPath, src + OFF_EXT_PATH, PATH_LEN);
    std::memcpy(&media, src + OFF_MEDIA, sizeof(u8));
    std::memcpy(&cardType, src + OFF_FS_CARD, sizeof(u8));
    std::memcpy(&card, src + OFF_CARD, sizeof(u8));

    bool accessibleSave = accessibleSaveRaw & 1;
    bool saveIsGBA      = accessibleSaveRaw & 2;

    Title title;
    title.load(id, productCode, accessibleSave, saveIsGBA, accessibleExtdata, shortDescription, longDescription,
        StringUtils::UTF8toUTF16(savePath), StringUtils::UTF8toUTF16(extdataPath), media, cardType, card);

    if (cardType == CARD_CTR) {
        u16 bigIconData[0x900];
        std::memcpy(bigIconData, src + OFF_ICON, ICON_BYTES);
        title.setIcon(loadTextureFromBytes(bigIconData));
    }
    else {
        // this cannot happen
        title.setIcon(Gui::noIcon());
    }

    return title;
}

u64 TitleCache::readId(const u8* src)
{
    u64 id;
    std::memcpy(&id, src + OFF_ID, sizeof(u64));
    return id;
}

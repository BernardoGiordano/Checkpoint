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

#include "fspxi.hpp"
#include "csvc.h"

#include <cstring>

namespace FSPXI {
    Handle fspxiHandle = 0;
    constexpr u32 pxiSaveFilePath[5] = {
        1,
        1,
        3,
        0,
        0
    };
    constexpr u8 ZEROS[0x20]            = {0};
    constexpr u32 POSSIBLE_SAVE_SIZES[] = {
        0x400,   // 8kbit
        0x2000,  // 64kbit
        0x8000,  // 256kbit
        0x10000, // 512kbit
        0x20000, // 1024kbit/1Mbit
    };
    struct GbaHeader
    {
        u8 magic[4];       // .SAV
        u8 padding1[12];   // Always 0xFF
        u8 cmac[0x10];     // CMAC. MUST BE RECALCULATED ON SAVE
        u8 padding2[0x10]; // Always 0xFF
        u32 contentId;     // Always 1
        u32 savesMade;     // Check this to find which save to load
        u64 titleId;
        u8 sdCid[0x10];
        u32 saveOffset; // Always 0x200
        u32 saveSize;
        u8 padding3[8];      // Always 0xFF
        u8 arm7Registers[8]; // Not really sure what to do with this. Seems to be 0x7F 00 00 00 00 00 00 00?
        u8 padding4[0x198];  // Get it to the proper size
    };

    Result openSaveArchive(FSPXI_Archive* archive, u32 lowId, u32 highId, FS_MediaType media)
    {
        const u32 path[4] = {lowId, highId, media, 1};
        return FSPXI_OpenArchive(fspxiHandle, archive, ARCHIVE_SAVEDATA_AND_CONTENT, {PATH_BINARY, 16, path});
    }
    Result openSaveFile(FSPXI_File* hnd, FSPXI_Archive archive, u32 flags)
    {
        return FSPXI_OpenFile(fspxiHandle, hnd, archive, {PATH_BINARY, 20, pxiSaveFilePath}, flags, 0);
    }
}

Result FSPXI::getHandle()
{
    return svcControlService(SERVICEOP_STEAL_CLIENT_SESSION, &fspxiHandle, "PxiFS0");
}
void FSPXI::closeHandle()
{
    if (fspxiHandle != 0) {
        svcCloseHandle(fspxiHandle);
    }
}

bool FSPXI::checkHasSave(u32 lowId, u32 highId, FS_MediaType media)
{
    bool out = false;
    FSPXI_Archive archive;
    Result res = openSaveArchive(&archive, lowId, highId, media);
    if (R_SUCCEEDED(res)) {
        FSPXI_File file;
        res = openSaveFile(&file, archive, FS_OPEN_READ);
        if (R_SUCCEEDED(res)) {
            out = true;
            FSPXI_CloseFile(fspxiHandle, file);
        }
        FSPXI_CloseArchive(fspxiHandle, archive);
    }
    return out;
}
std::vector<u8> FSPXI::getMostRecentSlot(u32 lowId, u32 highId, FS_MediaType media)
{
    std::vector<u8> data;
    FSPXI_Archive archive;
    Result res = openSaveArchive(&archive, lowId, highId, media);
    if (R_SUCCEEDED(res)) {
        FSPXI_File file;
        res = openSaveFile(&file, archive, FS_OPEN_READ);
        if (R_SUCCEEDED(res)) {
            GbaHeader header;
            u64 off = 0;
            FSPXI_ReadFile(fspxiHandle, file, nullptr, off, &header, sizeof(GbaHeader));

            if (memcpy(&header, ZEROS, sizeof(ZEROS) == 0)) { // top save is not there, grab bottom one
                for(const u32 size : POSSIBLE_SAVE_SIZES) {
                    res = FSPXI_ReadFile(fspxiHandle, file, nullptr, size + sizeof(GbaHeader), &header, sizeof(GbaHeader));
                    if (R_SUCCEEDED(res) && memcmp(header.magic, ".SAV", 4) == 0) {
                        off = header.saveSize + sizeof(GbaHeader);
                        break;
                    }
                }
            }
            else {
                GbaHeader headerBottom;
                FSPXI_ReadFile(fspxiHandle, file, nullptr, header.saveSize + sizeof(header), &header, sizeof(GbaHeader));
                if (headerBottom.savesMade > header.savesMade) {
                    off = header.saveSize + sizeof(GbaHeader);
                    memcpy(&header, &headerBottom, sizeof(GbaHeader));
                }
            }

            data.resize(header.saveSize);
            res = FSPXI_ReadFile(fspxiHandle, file, nullptr, off + sizeof(header), data.data(), data.size());
            FSPXI_CloseFile(fspxiHandle, file);
        }
        FSPXI_CloseArchive(fspxiHandle, archive);
    }
    return data;
}
bool FSPXI::writeBackup(u32 lowId, u32 highId, FS_MediaType media, const std::vector<u8>& data)
{
    FSPXI_Archive archive;
    Result res = openSaveArchive(&archive, lowId, highId, media);
    if (R_SUCCEEDED(res)) {
        FSPXI_File file;
        res = openSaveFile(&file, archive, FS_OPEN_READ);
        if (R_SUCCEEDED(res)) {
            // TODO: copy from PKSM
            FSPXI_CloseFile(fspxiHandle, file);
            return true;
        }
        FSPXI_CloseArchive(fspxiHandle, archive);
    }
    return false;
}

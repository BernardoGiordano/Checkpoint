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

#include "smdh.hpp"
#include "fileptr.hpp"

std::unique_ptr<smdh_s> loadSMDH(u32 low, u32 high, u8 media)
{
    Handle fileHandle;

    u32 archPath[]               = {low, high, media, 0x0};
    static const u32 filePath[]  = {0x0, 0x0, 0x2, 0x6E6F6369, 0x0};
    std::unique_ptr<smdh_s> smdh = std::make_unique<smdh_s>();

    FS_Path binArchPath = {PATH_BINARY, 0x10, archPath};
    FS_Path binFilePath = {PATH_BINARY, 0x14, filePath};

    Result res = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SAVEDATA_AND_CONTENT, binArchPath, binFilePath, FS_OPEN_READ, 0);
    if (R_SUCCEEDED(res)) {
        u32 read;
        FSFILE_Read(fileHandle, &read, 0, smdh.get(), sizeof(smdh_s));
    }
    else {
        smdh = nullptr;
    }

    FSFILE_Close(fileHandle);
    return smdh;
}

std::unique_ptr<smdh_s> loadSMDH(const std::string& path)
{
    FilePtr f = openFile(path.c_str(), "rb");
    if (f) {
        std::unique_ptr<smdh_s> smdh = std::make_unique<smdh_s>();
        fread(smdh.get(), 1, sizeof(smdh_s), f.get());
        return smdh;
    }
    return nullptr;
}
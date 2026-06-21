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

#include "savedatasource.hpp"
#include "filesystem.hpp"
#include "title.hpp"

Result SaveDataSource::mount(Title& title) const
{
    Result res = 0;
    FsFileSystem fileSystem;
    switch (mType) {
        case FsSaveDataType_Bcat:
            res = FileSystem::mountBcatSave(&fileSystem, title.id());
            break;
        case FsSaveDataType_Device:
            res = FileSystem::mountDeviceSave(&fileSystem, title.id());
            break;
        case FsSaveDataType_System:
            res = FileSystem::mountSystemSave(&fileSystem, title.id(), title.saveDataSpaceId());
            break;
        default:
            res = FileSystem::mountSave(&fileSystem, title.id(), title.userId());
            break;
    }
    if (R_FAILED(res)) {
        return res;
    }
    if (FileSystem::mountDevice(fileSystem) == -1) {
        FileSystem::unmountDevice();
        return -2;
    }
    return 0;
}

std::string SaveDataSource::baseDir() const
{
    switch (mType) {
        case FsSaveDataType_Bcat:
            return "sdmc:/switch/Checkpoint/bcat/";
        case FsSaveDataType_Device:
            return "sdmc:/switch/Checkpoint/device/";
        case FsSaveDataType_System:
            return "sdmc:/switch/Checkpoint/system/";
        default:
            return "sdmc:/switch/Checkpoint/saves/";
    }
}

bool SaveDataSource::isUserAccount() const
{
    return mType == FsSaveDataType_Account;
}

std::string SaveDataSource::fixedUserName() const
{
    switch (mType) {
        case FsSaveDataType_Bcat:
            return "BCAT";
        case FsSaveDataType_Device:
            return "Device";
        case FsSaveDataType_System:
            return "System";
        default:
            return "";
    }
}

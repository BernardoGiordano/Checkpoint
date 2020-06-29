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

#include "title.hpp"
#include "appdata.hpp"
#include "logger.hpp"
#include "platform.hpp"
#include "stringutils.hpp"
#include "io.hpp"
#include "archive.hpp"
#include "config.hpp"
#include "smdh.hpp"
#include "colors.hpp"

#include <cstdio>
#include <cstring>

#include <filesystem>
namespace fs = std::filesystem;

BackupInfo& TitleHolder::getInfo()
{
    return *this;
}

void TitleHolder::drawInfo(DrawDataHolder& d)
{
    auto& title = ts->at(titleIdx).mInfo;
    C2D_Text shortDesc, longDesc, id, prodCode, media, c2dId, c2dMediatype;

    char lowid[18];
    snprintf(lowid, 9, "%08X", (int)title.lowId());

    d.citro.dynamicText(&shortDesc, title.mShortDesc);
    d.citro.dynamicText(&longDesc, title.mLongDesc);
    d.citro.dynamicText(&id, lowid);
    d.citro.dynamicText(&media, title.mediatypeString());
    
    d.citro.dynamicText(&c2dId, "ID:");
    d.citro.dynamicText(&c2dMediatype, "Mediatype:");

    float longDescHeight, lowidWidth;
    C2D_TextGetDimensions(&longDesc, 0.55f, 0.55f, NULL, &longDescHeight);
    C2D_TextGetDimensions(&id, 0.5f, 0.5f, &lowidWidth, NULL);

    C2D_DrawText(&shortDesc, C2D_WithColor, 4, 1, 0.5f, 0.6f, 0.6f, COLOR_WHITE);
    C2D_DrawText(&longDesc, C2D_WithColor, 4, 27, 0.5f, 0.55f, 0.55f, COLOR_GREY_LIGHT);

    C2D_DrawText(&c2dId, C2D_WithColor, 4, 31 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
    C2D_DrawText(&id, C2D_WithColor, 25, 31 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_WHITE);

    snprintf(lowid, 18, "(%s)", title.mProductCode);
    d.citro.dynamicText(&prodCode, lowid);
    C2D_TextOptimize(&prodCode);
    C2D_DrawText(&prodCode, C2D_WithColor, 30 + lowidWidth, 32 + longDescHeight, 0.5f, 0.42f, 0.42f, COLOR_GREY_LIGHT);
    C2D_DrawText(&c2dMediatype, C2D_WithColor, 4, 47 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
    C2D_DrawText(&media, C2D_WithColor, 75, 47 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_WHITE);

    C2D_DrawRectSolid(260, 27, 0.5f, 52, 52, COLOR_BLACK);
    if (title.mIcon.subtex->width == 48) {
        C2D_DrawImageAt(title.mIcon, 262, 29, 0.5f, NULL, 1.0f, 1.0f);
    }
    else {
        C2D_DrawImageAt(title.mIcon, 262 + 8, 29 + 8, 0.5f, NULL, 1.0f, 1.0f);
    }
}
C2D_Image TitleHolder::getIcon()
{
    auto& info = ts->at(titleIdx).mInfo;
    info.mIcon.subtex = &info.mSubTex;
    return info.mIcon;
}
bool TitleHolder::favorite()
{
    return Configuration::get().favorite(ts->at(titleIdx).mInfo.mId);
}
BackupInfo::SpecialInfoResult TitleHolder::getSpecialInfo(BackupInfo::SpecialInfo special)
{
    if (special == BackupInfo::SpecialInfo::TitleIsActivityLog) {
        if (Title::isActivityLog(ts->at(titleIdx).mInfo)) {
            return BackupInfo::SpecialInfoResult::True;
        }
        else {
            return BackupInfo::SpecialInfoResult::False;
        }
    }
    else if (special == BackupInfo::SpecialInfo::CanCheat) {
        if (ts->at(titleIdx).mInfo.mCard == CARD_CTR) {
            return BackupInfo::SpecialInfoResult::True;
        }
        else {
            return BackupInfo::SpecialInfoResult::False;
        }
    }

    return BackupInfo::SpecialInfoResult::Invalid;
}
std::string TitleHolder::getCheatKey()
{
    return StringUtils::format("%016llX", ts->at(titleIdx).mInfo.mId);
}

Backupable::ActionResult DSSaveTitleHolder::backup(InputDataHolder& i)
{
    // const TitleInfo& info = ts->at(titleIdx).mInfo;
    return std::make_tuple(io::ActionResult::Failure, -1, "DS backup not implemented.");
}
Backupable::ActionResult DSSaveTitleHolder::restore(InputDataHolder& i)
{
    // const TitleInfo& info = ts->at(titleIdx).mInfo;
    return std::make_tuple(io::ActionResult::Failure, -1, "DS restore not implemented.");
}
Backupable::ActionResult DSSaveTitleHolder::deleteBackup(size_t idx)
{
    auto& title = ts->at(titleIdx);
    const auto bak = std::move(title.mSaveBackups[idx]);
    title.mSaveBackups.erase(title.mSaveBackups.begin() + idx);

    IODataHolder data;
    if (bak.first == -1) {
        data.srcPath = Title::saveBackupsDir(title.mInfo);
    }
    else {
        data.srcPath = Configuration::get().additionalSaveFolders(title.mInfo.mId)[bak.first];
    }
    data.srcPath /= bak.second;
    auto res = io::deleteFolderRecursively(data);
    return std::make_tuple(res, 0, res == io::ActionResult::Success ? "Backup deletion successful." : "Backup deletion failed.");
}
const std::vector<std::pair<int, std::string>>& DSSaveTitleHolder::getBackupsList()
{
    return ts->at(titleIdx).mSaveBackups;
}

Backupable::ActionResult GBASaveTitleHolder::backup(InputDataHolder& i)
{
    // const TitleInfo& info = ts->at(titleIdx).mInfo;
    return std::make_tuple(io::ActionResult::Failure, -1, "GBA backup not implemented.");
}
Backupable::ActionResult GBASaveTitleHolder::restore(InputDataHolder& i)
{
    // const TitleInfo& info = ts->at(titleIdx).mInfo;
    return std::make_tuple(io::ActionResult::Failure, -1, "GBA restore not implemented.");
}
Backupable::ActionResult GBASaveTitleHolder::deleteBackup(size_t idx)
{
    auto& title = ts->at(titleIdx);
    const auto bak = std::move(title.mSaveBackups[idx]);
    title.mSaveBackups.erase(title.mSaveBackups.begin() + idx);

    IODataHolder data;
    if (bak.first == -1) {
        data.srcPath = Title::saveBackupsDir(title.mInfo);
    }
    else {
        data.srcPath = Configuration::get().additionalSaveFolders(title.mInfo.mId)[bak.first];
    }
    data.srcPath /= bak.second;
    auto res = io::deleteFolderRecursively(data);
    return std::make_tuple(res, 0, res == io::ActionResult::Success ? "Backup deletion successful." : "Backup deletion failed.");
}
const std::vector<std::pair<int, std::string>>& GBASaveTitleHolder::getBackupsList()
{
    return ts->at(titleIdx).mSaveBackups;
}

Backupable::ActionResult SaveTitleHolder::backup(InputDataHolder& i)
{
    TitleInfo info = ts->at(titleIdx).mInfo;
    Logger::log(Logger::DEBUG, "Backing up save for %016llX", info.mId);

    IODataHolder data;
    data.dstPath = MOUNT_ARCHIVE_NAME ":/";
    if (i.backupName.first < 0) {
        data.srcPath = Title::saveBackupsDir(info);
    }
    else {
        data.srcPath = Configuration::get().additionalSaveFolders(info.mId)[i.backupName.first];
    }
    data.srcPath /= i.backupName.second;

    Result res = 0;
    if (info.mMedia == MEDIATYPE_NAND) {
        const u32 path[2] = {info.mMedia, (0x00020000 | (info.lowId() >> 8))};
        res = Archive::mount(ARCHIVE_SYSTEM_SAVEDATA, {PATH_BINARY, 8, path});
    }
    else {
        const u32 path[3] = {info.mMedia, info.lowId(), info.highId()};
        res = Archive::mount(ARCHIVE_USER_SAVEDATA, {PATH_BINARY, 12, path});
    }
    if (R_SUCCEEDED(res)) {
        if (io::directoryExists(data)) {
            io::deleteFolderRecursively(data);
            io::createDirectory(data);
        }
        std::swap(data.srcPath, data.dstPath);

        io::copyDirectory(data);
        Archive::unmount();

        if (i.backupName.first == -2) {
            LightLock_Lock(&i.parent.backupableVectorLock);
            auto& backups = ts->at(titleIdx).mSaveBackups;
            backups.push_back(i.backupName);
            backups.back().first = -1;
            std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });
            LightLock_Unlock(&i.parent.backupableVectorLock);
        }

        return std::make_tuple(io::ActionResult::Success, 0, "Save backup succesful.");
    }

    return std::make_tuple(io::ActionResult::Failure, res, "Failed to mount save.");
}
Backupable::ActionResult SaveTitleHolder::restore(InputDataHolder& i)
{
    TitleInfo info = ts->at(titleIdx).mInfo;
    Logger::log(Logger::DEBUG, "Restoring save for %016llX", info.mId);

    IODataHolder data;
    data.dstPath = MOUNT_ARCHIVE_NAME ":/";
    if (i.backupName.first < 0) {
        data.srcPath = Title::saveBackupsDir(info);
    }
    else {
        data.srcPath = Configuration::get().additionalSaveFolders(info.mId)[i.backupName.first];
    }
    data.srcPath /= i.backupName.second;

    std::swap(data.srcPath, data.dstPath);
    Result res = 0;
    if (info.mMedia == MEDIATYPE_NAND) {
        const u32 path[2] = {info.mMedia, (0x00020000 | (info.lowId() >> 8))};
        res = Archive::mount(ARCHIVE_SYSTEM_SAVEDATA, {PATH_BINARY, 8, path});
    }
    else {
        const u32 path[3] = {info.mMedia, info.lowId(), info.highId()};
        res = Archive::mount(ARCHIVE_USER_SAVEDATA, {PATH_BINARY, 12, path});
    }
    if (R_SUCCEEDED(res)) {
        io::copyDirectory(data);
        Archive::unmount();

        return std::make_tuple(io::ActionResult::Success, 0, "Save restore succesful.");
    }

    return std::make_tuple(io::ActionResult::Failure, res, "Failed to mount save.");
}
Backupable::ActionResult SaveTitleHolder::deleteBackup(size_t idx)
{
    auto& title = ts->at(titleIdx);
    const auto bak = std::move(title.mSaveBackups[idx]);
    title.mSaveBackups.erase(title.mSaveBackups.begin() + idx);

    IODataHolder data;
    if (bak.first == -1) {
        data.srcPath = Title::saveBackupsDir(title.mInfo);
    }
    else {
        data.srcPath = Configuration::get().additionalSaveFolders(title.mInfo.mId)[bak.first];
    }
    data.srcPath /= bak.second;
    auto res = io::deleteFolderRecursively(data);
    return std::make_tuple(res, 0, res == io::ActionResult::Success ? "Backup deletion successful." : "Backup deletion failed.");
}
const std::vector<std::pair<int, std::string>>& SaveTitleHolder::getBackupsList()
{
    return ts->at(titleIdx).mSaveBackups;
}

Backupable::ActionResult ExtdataTitleHolder::backup(InputDataHolder& i)
{
    TitleInfo info = ts->at(titleIdx).mInfo;
    Logger::log(Logger::DEBUG, "Backing up extdata for %016llX", info.mId);

    IODataHolder data;
    data.dstPath = MOUNT_ARCHIVE_NAME ":/";
    if (i.backupName.first < 0) {
        data.srcPath = Title::extdataBackupsDir(info);
    }
    else {
        data.srcPath = Configuration::get().additionalExtdataFolders(info.mId)[i.backupName.first];
    }
    data.srcPath /= i.backupName.second;

    const u32 path[3] = {MEDIATYPE_SD, info.extdataId(), 0};
    Result res = Archive::mount(ARCHIVE_EXTDATA, {PATH_BINARY, 12, path});
    if (R_SUCCEEDED(res)) {
        if (io::directoryExists(data)) {
            io::deleteFolderRecursively(data);
            io::createDirectory(data);
        }
        std::swap(data.srcPath, data.dstPath);

        io::copyDirectory(data);
        Archive::unmount();

        if (i.backupName.first == -2) {
            LightLock_Lock(&i.parent.backupableVectorLock);
            auto& backups = ts->at(titleIdx).mExtdataBackups;
            backups.push_back(i.backupName);
            backups.back().first = -1;
            std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });
            LightLock_Unlock(&i.parent.backupableVectorLock);
        }

        return std::make_tuple(io::ActionResult::Success, 0, "Extdata backup succesful.");
    }

    return std::make_tuple(io::ActionResult::Failure, res, "Failed to mount extdata.");
}
Backupable::ActionResult ExtdataTitleHolder::restore(InputDataHolder& i)
{
    TitleInfo info = ts->at(titleIdx).mInfo;
    Logger::log(Logger::DEBUG, "Restoring extdata for %016llX", info.mId);

    IODataHolder data;
    data.dstPath = MOUNT_ARCHIVE_NAME ":/";
    if (i.backupName.first < 0) {
        data.srcPath = Title::extdataBackupsDir(info);
    }
    else {
        data.srcPath = Configuration::get().additionalExtdataFolders(info.mId)[i.backupName.first];
    }
    data.srcPath /= i.backupName.second;

    const u32 path[3] = {MEDIATYPE_SD, info.extdataId(), 0};
    Result res = Archive::mount(ARCHIVE_EXTDATA, {PATH_BINARY, 12, path});
    if (R_SUCCEEDED(res)) {
        io::copyDirectory(data);
        Archive::unmount();

        return std::make_tuple(io::ActionResult::Success, 0, "Extdata restore succesful.");
    }

    return std::make_tuple(io::ActionResult::Failure, res, "Failed to mount extdata.");
}
Backupable::ActionResult ExtdataTitleHolder::deleteBackup(size_t idx)
{
    auto& title = ts->at(titleIdx);
    const auto bak = std::move(title.mExtdataBackups[idx]);
    title.mExtdataBackups.erase(title.mExtdataBackups.begin() + idx);

    IODataHolder data;
    if (bak.first == -1) {
        data.srcPath = Title::extdataBackupsDir(title.mInfo);;
    }
    else {
        data.srcPath = Configuration::get().additionalSaveFolders(title.mInfo.mId)[bak.first];
    }
    data.srcPath /= bak.second;
    auto res = io::deleteFolderRecursively(data);
    return std::make_tuple(res, 0, res == io::ActionResult::Success ? "Backup deletion successful." : "Backup deletion failed.");
}
const std::vector<std::pair<int, std::string>>& ExtdataTitleHolder::getBackupsList()
{
    return ts->at(titleIdx).mExtdataBackups;
}

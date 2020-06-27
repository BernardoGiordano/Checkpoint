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

#include "wifi.hpp"
#include "platform.hpp"
#include "io.hpp"
#include "stringutils.hpp"
#include "fileptr.hpp"
#include "config.hpp"

#include <filesystem>
namespace fs = std::filesystem;

namespace Wifi {
    constexpr u32 SlotsBegin = 0x00080000;
    constexpr u32 SlotCount  = 3;
    std::array<WifiSlotHolder*, SlotCount> actualPtrs;
    std::vector<std::pair<int, std::string>> knownBackups;
}

Wifi::WifiSlotHolder::WifiSlotHolder(u32 idx) : slotIndex(idx), anyWrite(false)
{
    CFG_GetConfigInfoBlk8(SlotSize, SlotsBegin + slotIndex, bytes.data());
}
void Wifi::WifiSlotHolder::writeToCFG()
{
    if(anyWrite) {
        CFG_SetConfigInfoBlk8(SlotSize, SlotsBegin + slotIndex, bytes.data());
    }
}

Backupable::ActionResult Wifi::WifiSlotHolder::backup(InputDataHolder& i)
{
    IODataHolder data;
    if(i.backupName.first < 0) {
        data.srcPath = Platform::Directories::WifiSlotBackupsDir;
    }
    else {
        data.srcPath = Configuration::get().additionalWifiSlotsFolders()[i.backupName.first];
    }
    data.srcPath /= i.backupName.second;
    if(!io::directoryExists(data)) {
        io::createDirectory(data);
    }

    data.srcPath /= "slot.bin";
    FilePtr file = openFile(data.srcPath.c_str(), "wb");

    if(file) {
        fwrite(bytes.data(), 1, SlotSize, file.get());
        if(i.backupName.first == -2) {
            knownBackups.push_back(i.backupName);
            knownBackups.back().first = -1;
            std::sort(knownBackups.begin(), knownBackups.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });
        }
        return std::make_tuple(io::ActionResult::Success, 0, StringUtils::format("Wifi slot %d saved to disk.", slotIndex + 1));
    }
    else {
        return std::make_tuple(io::ActionResult::Failure, -errno, "Failed to open backup file.");
    }
}
Backupable::ActionResult Wifi::WifiSlotHolder::restore(InputDataHolder& i)
{
    IODataHolder data;
    if(i.backupName.first < 0) {
        data.srcPath = Platform::Directories::WifiSlotBackupsDir;
    }
    else {
        data.srcPath = Configuration::get().additionalWifiSlotsFolders()[i.backupName.first];
    }
    data.srcPath /= i.backupName.second;

    data.srcPath /= "slot.bin";
    FilePtr file = openFile(data.srcPath.c_str(), "rb");

    if(file) {
        fread(bytes.data(), 1, SlotSize, file.get());
        anyWriteToWifiSlots = true;
        anyWrite = true;
        return std::make_tuple(io::ActionResult::Success, 0, StringUtils::format("Wifi slot %d wrote from backup.", slotIndex + 1));
    }
    else {
        return std::make_tuple(io::ActionResult::Failure, -errno, "Failed to open backup file.");
    }

}
Backupable::ActionResult Wifi::WifiSlotHolder::deleteBackup(size_t idx)
{
    const auto bak = std::move(knownBackups[idx]);
    knownBackups.erase(knownBackups.begin() + idx);

    IODataHolder data;
    if(bak.first == - 1) {
        data.srcPath = Platform::Directories::WifiSlotBackupsDir;
    }
    else {
        data.srcPath = Configuration::get().additionalWifiSlotsFolders()[bak.first];
    }
    data.srcPath /= bak.second;
    auto res = io::deleteFolderRecursively(data);
    return std::make_tuple(res, 0, res == io::ActionResult::Success ? "Backup deletion successful." : "Backup deletion failed.");
}
BackupInfo& Wifi::WifiSlotHolder::getInfo()
{
    return *this;
}

void Wifi::WifiSlotHolder::drawInfo(DrawDataHolder& d)
{
    C2D_Text txt;
    char slotNameStr[12];
    snprintf(slotNameStr, 12, "WiFi slot %lu", slotIndex + 1);
    d.citro.dynamicText(&txt, slotNameStr);

    C2D_DrawText(&txt, C2D_WithColor, 4, 1, 0.5f, 0.6f, 0.6f, COLOR_WHITE);

    if(!exists()) {
        d.citro.dynamicText(&txt, "Currently empty");
        C2D_DrawText(&txt, C2D_WithColor, 4, 27, 0.5f, 0.55f, 0.55f, COLOR_GREY_LIGHT);
    }

    C2D_DrawRectSolid(260, 27, 0.5f, 52, 52, COLOR_BLACK);
    C2D_DrawImageAt(getIcon(), 262, 29, 0.5f, NULL, 1.0f, 1.0f);
}
C2D_Image Wifi::WifiSlotHolder::getIcon()
{
    return CTRH::wifiSlotIcons[slotIndex];
}
bool Wifi::WifiSlotHolder::favorite()
{
    return false;
}
const std::vector<std::pair<int, std::string>>& Wifi::WifiSlotHolder::getBackupsList()
{
    return knownBackups;
}
BackupInfo::SpecialInfoResult Wifi::WifiSlotHolder::getSpecialInfo(BackupInfo::SpecialInfo special)
{
    if(special == BackupInfo::SpecialInfo::WifiSlotExists) {
        if(exists()) {
            return BackupInfo::SpecialInfoResult::True;
        }
        else {
            return BackupInfo::SpecialInfoResult::False;
        }
    }

    return BackupInfo::SpecialInfoResult::Invalid;
}

bool Wifi::WifiSlotHolder::exists()
{
    return bytes[0] || bytes[1];
}

void Wifi::load(std::vector<std::unique_ptr<Backupable>>& out)
{
    {
        // standard save backups
        for(auto& bak_entry : fs::directory_iterator(Platform::Directories::WifiSlotBackupsDir)) {
            if(bak_entry.is_directory()) {
                knownBackups.push_back(std::make_pair(-1, bak_entry.path().stem().string()));
            }
        }

        // save backups from configuration
        size_t idx = 0;
        for(const auto& additionalPath : Configuration::get().additionalWifiSlotsFolders()) {
            for(auto& bak_entry : fs::directory_iterator(additionalPath)) {
                knownBackups.push_back(std::make_pair(idx, bak_entry.path().stem().string()));
            }
            idx++;
        }

        std::sort(knownBackups.begin(), knownBackups.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
    }

    for(u32 i = 0; i < SlotCount; ++i) {
        auto ptr = std::make_unique<BackupableWithData<WifiSlotHolder>>(i);
        actualPtrs[i] = &ptr.get()->data;
        out.push_back(std::move(ptr));
    }
}
void Wifi::finalizeWrite()
{
    for(WifiSlotHolder* p : actualPtrs) {
        p->writeToCFG();
    }
}

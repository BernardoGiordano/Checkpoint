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

#ifndef WIFI_HPP
#define WIFI_HPP

#include <3ds.h>
#include <array>

#include "backupable.hpp"
#include "inputdata.hpp"

namespace Wifi {
    constexpr u32 SlotSize   = 0xC00;
    inline bool anyWriteToWifiSlots = false;

    struct WifiSlotHolder : public BackupInfo {
        WifiSlotHolder(u32 idx);

        void writeToCFG();

        Backupable::ActionResult backup(InputDataHolder& i);
        Backupable::ActionResult restore(InputDataHolder& i);
        Backupable::ActionResult deleteBackup(size_t idx);
        BackupInfo& getInfo();

        virtual void drawInfo(DrawDataHolder& d) override final;
        virtual C2D_Image getIcon() override final;
        virtual bool favorite() override final;
        virtual const std::vector<std::pair<int, std::string>>& getBackupsList() override final;
        virtual BackupInfo::SpecialInfoResult getSpecialInfo(BackupInfo::SpecialInfo special) override final;
        virtual std::string getCheatKey() override final;

    private:
        bool exists();

        const u32 slotIndex;
        bool anyWrite;
        std::array<u8, SlotSize> bytes;
    };

    void load(std::vector<std::unique_ptr<Backupable>>& out);
    void finalizeWrite();
}

#endif

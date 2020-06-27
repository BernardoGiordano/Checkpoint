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

#ifndef BACKUPINFO_HPP
#define BACKUPINFO_HPP

#include "drawdata.hpp"
#include <citro2d.h>
#include <vector>
#include <string>

namespace BackupTypes
{
    enum Types_ : int {
        First = 0,

        Save = First,
        Extdata,
        WifiSlots,

        End
    };

    inline int after(int current)
    {
        current++;
        if(current == End) {
            current = First;
        }
        return current;
    }

    inline const char* name(int current)
    {
        switch(current) {
            case Save:
                return "saves";
            case Extdata:
                return "extdata";
            case WifiSlots:
                return "wifi slots";
            default:
                return " ";
        }

        return " ";
    }
}

struct BackupInfo {
    enum class SpecialInfo {
        TitleIsActivityLog,
        WifiSlotExists,
    };
    enum class SpecialInfoResult {
        Invalid = -1,
        False = 0,
        True = 1,
    };

    virtual ~BackupInfo()
    { }

    virtual void drawInfo(DrawDataHolder&) = 0;
    virtual C2D_Image getIcon() = 0;
    virtual const std::vector<std::pair<int, std::string>>& getBackupsList() = 0;
    virtual bool favorite() = 0;
    virtual SpecialInfoResult getSpecialInfo(SpecialInfo) = 0;

    bool mMultiSelected = false;
};

#endif

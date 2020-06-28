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

#include "appdata.hpp"

CheatManager& DataHolder::getCheats()
{
    if (!cheats) cheats = std::unique_ptr<CheatManager>(new CheatManager);

    return *cheats;
}

void DataHolder::beginBackup(Backupable* on)
{
    actionType = Action::Type::Backup;
    actOn = on;
    LightEvent_Signal(&shouldPerformAction);
}
void DataHolder::beginMultiBackup()
{
    actionType = Action::Type::MultiBackup;
    LightEvent_Signal(&shouldPerformAction);
}
void DataHolder::beginRestore(Backupable* on)
{
    actionType = Action::Type::Restore;
    actOn = on;
    LightEvent_Signal(&shouldPerformAction);
}

void DataHolder::setAllMultiSelection()
{
    LightLock_Lock(&backupableVectorLock);
    auto& vec = thingsToActOn[selectedType];
    for(auto& b : vec) {
        auto& info = b->getInfo();
        if (!info.mMultiSelected) {
            info.mMultiSelected = true;
            multiSelectedCount[selectedType]++;
        }
    }
    LightLock_Unlock(&backupableVectorLock);
}
void DataHolder::clearMultiSelection()
{
    auto& vec = thingsToActOn[selectedType];
    for(auto& b : vec) {
        auto& info = b->getInfo();
        if (info.mMultiSelected) {
            info.mMultiSelected = false;
            multiSelectedCount[selectedType]--;
        }
    }
}

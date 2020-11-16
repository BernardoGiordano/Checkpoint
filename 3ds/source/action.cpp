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

#include <3ds.h>
#include "stringutils.hpp"
#include "action.hpp"
#include "appdata.hpp"

void Action::performActionThreadFunc(void* arg)
{
    DataHolder& data = *static_cast<DataHolder*>(arg);
    while(LightEvent_Wait(&data.shouldPerformAction), data.actionThreadKeepGoing.test_and_set())
    {
        data.actionOngoing = true;

        auto actionType = data.actionType;
        auto actOn = data.actOn;

        if (actionType == Action::Type::Backup) {
            data.resultInfo = actOn->backup(data.input);
        }
        else if (actionType == Action::Type::MultiBackup) {
            size_t errorCount = 0;
            const size_t selectedCount = data.multiSelectedCount[data.selectedType];
            
            LightLock_Lock(&data.backupableVectorLock);
            for(auto& bak : data.thingsToActOn[data.selectedType]) {
                bool& multi = bak->getInfo().mMultiSelected;
                if (multi) {
                    multi = false;
                    data.multiSelectedCount[data.selectedType]--;
                    auto res = bak->backup(data.input);
                    if (std::get<0>(res) == io::ActionResult::Failure) {
                        errorCount++;
                    }
                }
            }
            LightLock_Unlock(&data.backupableVectorLock);

            if (errorCount != 0) {
                if (errorCount == selectedCount) {
                    data.resultInfo = std::make_tuple(io::ActionResult::Failure, -1, StringUtils::format(
                        "All %zd backup attempt%s failed.",
                        errorCount,
                        errorCount == 1 ? "" : "s"
                    ));
                }
                else {
                    data.resultInfo = std::make_tuple(io::ActionResult::Success, 0, StringUtils::format(
                        "Backups completed,\n%zd error%s occured.",
                        errorCount,
                        errorCount == 1 ? "" : "s"
                    ));
                }
            }
            else {
                data.resultInfo = std::make_tuple(io::ActionResult::Success, 0, "All backups completed\nsuccessfully.");
            }
        }
        else if (actionType == Action::Type::Restore) {
            data.resultInfo = actOn->restore(data.input);
        }

        data.actOn = nullptr;
        data.actionType = Action::Type::Invalid;
        data.actionOngoing = false;
    }
}

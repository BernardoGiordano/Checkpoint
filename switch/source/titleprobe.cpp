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

#include "titleprobe.hpp"
#include "savedatasource.hpp"

bool TitleProbe::probe(Title& dst, const FsSaveDataInfo& info, IconStore& icons, NsApplicationControlData* nsacd)
{
    const u8 type = info.save_data_type;
    // System saves are keyed by their system_save_data_id; everything else by application_id.
    const u64 tid = (type == FsSaveDataType_System) ? info.system_save_data_id : info.application_id;
    const u64 sid = info.save_data_id;

    if (Configuration::getInstance().filter(tid)) {
        return false;
    }

    std::string name;
    std::string author;
    AccountUid uid = {0};

    if (type == FsSaveDataType_System) {
        // verify the save can be mounted before adding it
        FsFileSystem testFs;
        if (R_FAILED(FileSystem::mountSystemSave(&testFs, tid, info.save_data_space_id))) {
            return false;
        }
        fsFsClose(&testFs);
        name = StringUtils::format("0x%016llX", tid);
        icons.loadPlaceholderIcon(tid);
    }
    else {
        if (type == FsSaveDataType_Account) {
            uid = info.uid;
        }
        size_t outsize         = 0;
        NacpLanguageEntry* nle = NULL;
        Result res = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, nsacd, sizeof(NsApplicationControlData), &outsize);
        if (R_FAILED(res) || outsize < sizeof(nsacd->nacp)) {
            return false;
        }
        res = nacpGetLanguageEntry(&nsacd->nacp, &nle);
        if (R_FAILED(res) || nle == NULL) {
            return false;
        }
        name   = std::string(nle->name);
        author = std::string(nle->author);
        icons.loadIcon(tid, nsacd, outsize - sizeof(nsacd->nacp));
    }

    SaveDataSource source(type);
    const std::string userName = source.isUserAccount() ? Account::username(uid) : source.fixedUserName();
    const std::string safeName =
        StringUtils::containsInvalidChar(name) ? StringUtils::format("0x%016llX", tid) : StringUtils::removeForbiddenCharacters(name);
    const std::string path = source.baseDir() + StringUtils::format("0x%016llX", tid) + " " + safeName;

    if (!io::directoryExists(path)) {
        io::createDirectory(path);
    }

    const u8 spaceId = (type == FsSaveDataType_System) ? info.save_data_space_id : (u8)FsSaveDataSpaceId_User;
    dst.init(type, tid, uid, spaceId, name, author, userName, path);
    dst.saveId(sid);

    if (type == FsSaveDataType_Account) {
        PdmPlayStatistics stats;
        if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(tid, uid, false, &stats))) {
            dst.playTimeNanoseconds(stats.playtime);
            dst.lastPlayedTimestamp(stats.last_timestamp_user);
        }
    }

    return true;
}

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

#ifndef SAVEDATASOURCE_HPP
#define SAVEDATASOURCE_HPP

#include <string>
#include <switch.h>

class Title;

// One openable save-data archive of a given kind (account / BCAT / device /
// system). The single place that knows, per kind: how to mount it under the
// "save:" device, its on-SD backup root, whether it is a per-user account save,
// and the fixed label shown for the non-account kinds. Concentrates the
// save-data-type switch that was otherwise repeated in io::backup/io::restore,
// Title::init and the backup-name suggestion.
class SaveDataSource {
public:
    explicit SaveDataSource(u8 saveDataType) : mType(saveDataType) {}

    // Mounts this kind's archive for `title` under the "save:" device. Returns the
    // FS result; -2 indicates the fsdev device mount itself failed.
    Result mount(Title& title) const;

    // On-SD backup root for this kind, e.g. "sdmc:/switch/Checkpoint/saves/".
    std::string baseDir() const;

    // True for per-user account saves; false for the BCAT/Device/System singletons.
    bool isUserAccount() const;

    // Fixed user-column label for the non-account kinds ("BCAT"/"Device"/"System");
    // empty for account saves, which display the real user name.
    std::string fixedUserName() const;

private:
    u8 mType;
};

#endif // SAVEDATASOURCE_HPP

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

#ifndef BACKUPABLE_HPP
#define BACKUPABLE_HPP

#include "inputdata.hpp"
#include "backupinfo.hpp"
#include "io.hpp"

struct Backupable {
    using ActionResult = std::tuple<io::ActionResult, long, std::string>;

    virtual ActionResult backup(InputDataHolder& i) = 0;
    virtual ActionResult restore(InputDataHolder& i) = 0;
    virtual ActionResult deleteBackup(size_t idx) = 0;
    virtual BackupInfo& getInfo() = 0;
};

template<typename T>
struct BackupableWithData : public Backupable {
    BackupableWithData(T d) : data(d)
    { }

    virtual Backupable::ActionResult backup(InputDataHolder& i) final
    {
        return data.backup(i);
    }
    virtual Backupable::ActionResult restore(InputDataHolder& i) final
    {
        return data.restore(i);
    }
    virtual ActionResult deleteBackup(size_t idx) final
    {
        return data.deleteBackup(idx);
    }

    virtual BackupInfo& getInfo() final
    {
        return data.getInfo();
    }

    T data;
};

#endif

/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#ifndef IO_HPP
#define IO_HPP

#include "KeyboardManager.hpp"
#include "directory.hpp"
#include "fsstream.hpp"
#include "gui.hpp"
#include "util.hpp"
#include <3ds.h>

#define BUFFER_SIZE 0x50000

namespace io {
    void backup(size_t index);
    void restore(size_t index);

    Result copyDirectory(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath);
    void copyFile(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath);
    Result createDirectory(FS_Archive archive, const std::u16string& path);
    void deleteBackupFolder(const std::u16string& path);
    Result deleteFolderRecursively(FS_Archive arch, const std::u16string& path);
    bool directoryExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(const std::string& path);
}

#endif
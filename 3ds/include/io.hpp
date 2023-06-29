/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2021 Bernardo Giordano, FlagBrew
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
#include "multiselection.hpp"
#include "spi.hpp"
#include "title.hpp"
#include "util.hpp"
#include <3ds.h>
#include <tuple>

#define BUFFER_SIZE 0x50000

namespace io {
    std::tuple<bool, Result, std::string> backup(size_t index, size_t cellIndex);
    std::tuple<bool, Result, std::string> restore(size_t index, size_t cellIndex, const std::string& nameFromCell);

    Result copyDirectory(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath);
    void copyFile(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath);
    Result createDirectory(FS_Archive archive, const std::u16string& path);
    void deleteBackupFolder(const std::u16string& path);
    Result deleteFolderContentsRecursively(FS_Archive arch, const std::u16string& path);
    Result deleteFolderRecursively(FS_Archive arch, const std::u16string& path);
    bool directoryExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(const std::string& path);
}

#endif
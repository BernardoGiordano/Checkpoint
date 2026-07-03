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

#ifndef IO_HPP
#define IO_HPP

#include "directory.hpp"
#include "fsstream.hpp"
#include "progress.hpp"
#include "spi.hpp"
#include "title.hpp"
#include "util.hpp"
#include <3ds.h>

#define BUFFER_SIZE 0x50000

class BackupTarget;

namespace io {
    // The stage at which a backup/restore failed. The UI maps it (together with
    // the target's data-type name) to a human message; io itself carries no UI text.
    enum class BackupStage { OpenArchive, DeleteDst, CreateDst, Copy, ReadSpi, ReadFile, WriteFile, Commit, SecureValue };

    struct IoOutcome {
        bool ok;
        Result res;
        BackupStage stage; // meaningful only when !ok
    };

    // Backs up `target` into the already-resolved `dstPath` (the caller picks the
    // folder name and decides new-vs-overwrite). Reports progress through `sink`.
    IoOutcome backup(const BackupTarget& target, const std::u16string& dstPath, ProgressSink& sink);
    // Restores `target` from the already-resolved backup folder `srcPath`.
    IoOutcome restore(const BackupTarget& target, const std::u16string& srcPath, ProgressSink& sink);

    size_t countFiles(FS_Archive arch, const std::u16string& path);
    Result copyDirectory(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath, ProgressSink& sink);
    Result copyFile(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath, ProgressSink& sink);
    // Copies a GBA VC raw save between the FSPXI archive and an SD-card file (fromPxi selects the direction).
    Result copyPxiSaveFile(FSPXI_Archive pxiArch, FS_Archive regularArch, const std::u16string& path, bool fromPxi, ProgressSink& sink);
    Result createDirectory(FS_Archive archive, const std::u16string& path);
    void deleteBackupFolder(const std::u16string& path);
    Result deleteFolderRecursively(FS_Archive arch, const std::u16string& path);
    bool directoryExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(const std::string& path);
}

#endif
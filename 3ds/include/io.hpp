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
#include <vector>

#define BUFFER_SIZE 0x50000

class BackupTarget;

namespace io {
    // The stage at which a backup/restore failed. The UI maps it (together with
    // the target's data-type name) to a human message; io itself carries no UI text.
    enum class BackupStage { OpenArchive, DeleteDst, CreateDst, Copy, ReadSpi, ReadFile, WriteFile, Commit, SecureValue, PathTooLong, CardNandSave };

    struct IoOutcome {
        bool ok;
        Result res;
        BackupStage stage;      // meaningful only when !ok
        bool cancelled = false; // set only for a backup aborted via ProgressSink::cancelled()
    };

    // Backs up `target` into the already-resolved `dstPath` (the caller picks the
    // folder name and decides new-vs-overwrite). Reports progress through `sink`.
    IoOutcome backup(const BackupTarget& target, const std::u16string& dstPath, ProgressSink& sink);
    // Restores `target` from the already-resolved backup folder `srcPath`.
    IoOutcome restore(const BackupTarget& target, const std::u16string& srcPath, ProgressSink& sink);

    // One entry of a copy tree: `rel` is the path relative to the copy root, `folder`
    // distinguishes a directory to create from a file to copy.
    struct TreeEntry {
        std::u16string rel;
        bool folder;
    };
    // Enumerate the whole tree under `path` in a single walk, pre-order (a folder
    // precedes its contents), so callers get both the progress total and the copy
    // plan without walking the tree twice.
    Result collectTree(FS_Archive arch, const std::u16string& path, std::vector<TreeEntry>& out);
    // FS rejects any path longer than this many UTF-16 units (the null terminator
    // is not counted) — see checkPathLengths.
    inline constexpr size_t MAX_PATH_UNITS = 0x100;
    // Answers "can every path in this copy plan be handed to FS at all?" before a
    // single byte moves, so a restore never wipes the console-side save only to die
    // on a backup folder FS was never going to accept. Checks both sides: the copy
    // roots themselves and every entry under them. Names the offending path in the
    // log and returns the same Result FS would have.
    Result checkPathLengths(const std::vector<TreeEntry>& entries, const std::u16string& srcRoot, const std::u16string& dstRoot);
    // Copy a tree previously enumerated by collectTree, reusing one heap buffer for
    // every file. Stops on the first failure.
    Result copyTree(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcRoot, const std::u16string& dstRoot,
        const std::vector<TreeEntry>& entries, ProgressSink& sink);
    // Copies a single file using the caller-provided BUFFER_SIZE scratch buffer.
    Result copyFile(
        FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath, ProgressSink& sink, u8* buffer);
    Result createDirectory(FS_Archive archive, const std::u16string& path);
    void deleteBackupFolder(const std::u16string& path);
    // Recursively counts the files under `path`, directories excluded. Gives the
    // destination wipe a real total to report against before it starts deleting.
    size_t countFilesRecursively(FS_Archive arch, const std::u16string& path);
    // Both delete helpers report every removed file through `sink` when one is
    // given. A wipe has no byte progress, only a file count, so each file is
    // reported with size 0.
    Result deleteFolderRecursively(FS_Archive arch, const std::u16string& path, ProgressSink* sink = nullptr);
    // Empties `path` without deleting the directory itself — used for a DSiWare
    // restore, where the TWL FAT `data` directory must survive.
    Result deleteFolderContentsRecursively(FS_Archive arch, const std::u16string& path, ProgressSink* sink = nullptr);
    bool directoryExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(FS_Archive archive, const std::u16string& path);
    bool fileExists(const std::string& path);
}

#endif
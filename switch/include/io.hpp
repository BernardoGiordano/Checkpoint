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

#include "KeyboardManager.hpp"
#include "account.hpp"
#include "directory.hpp"
#include "multiselection.hpp"
#include "progress.hpp"
#include "title.hpp"
#include "util.hpp"
#include <dirent.h>
#include <switch.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

#define BUFFER_SIZE 0x80000

class Title;

namespace io {
    // The stage at which a backup/restore failed. The UI maps it to a human
    // message; io itself carries no UI text.
    enum class BackupStage { OpenArchive, DeleteDst, CreateDst, Copy, Commit, Verify };

    struct IoOutcome {
        bool ok;
        Result res;
        BackupStage stage;      // meaningful only when !ok
        bool cancelled = false; // set only for a backup aborted via ProgressSink::cancelled(); ok is false, res is 0
    };

    // What one recursive walk of a tree found. The same struct is filled by the
    // pre-flight scan of a backup and by the copy that follows, so the two can be
    // compared: a copy that reports success but visited fewer files than the scan
    // counted is a silently incomplete restore, which is what #541 looked like
    // from the outside.
    struct TreeStats {
        size_t files = 0;
        size_t dirs  = 0;
        u64 bytes    = 0;
        // `bytes` with every file rounded up to a save-filesystem cluster: what
        // the tree will actually occupy once restored. A backup of tens of
        // thousands of tiny files needs several times its byte size (#541).
        u64 allocated = 0;
        // Directories that failed to list and files that failed to stat. Non-zero
        // means the walk itself is incomplete, so `files`/`bytes` are lower bounds
        // and nothing derived from them can be trusted.
        size_t unreadable = 0;
    };

    // One file the copy actually wrote, with the CRC32 of the bytes that flowed
    // through it. Recorded while copying so the post-restore verification never
    // has to read the backup a second time: it re-reads only the committed save
    // and compares against these figures, halving the IO of the verify pass (the
    // CRC itself is a handful of hardware instructions over data already in the
    // copy buffer).
    struct CopiedFile {
        std::string path; // destination path, exactly as it was written
        u64 size = 0;
        u32 crc  = 0;
    };

    // Backs up `title` into the already-resolved `dstPath` (the caller picks the
    // folder name and decides new-vs-overwrite). Reports progress through `sink`.
    IoOutcome backup(Title& title, const std::string& dstPath, ProgressSink& sink);
    // Restores `title` from the already-resolved backup folder `srcPath`.
    IoOutcome restore(Title& title, const std::string& srcPath, ProgressSink& sink);

    size_t countFiles(const std::string& path);
    // One walk of `path` collecting the file count, the directory count and the
    // total byte size, plus how much of the tree could not be read. Replaces
    // walking the same tree once per figure. Pass a `sink` when the walk is long
    // enough that the modal would otherwise look frozen; the caller owns
    // begin()/end() since only it knows the expected total.
    TreeStats scanTree(const std::string& path, ProgressSink* sink = nullptr);
    // `commitWriteLimit` > 0 caps the bytes written to the save device between
    // commits, so large writes never overflow the save's journal; 0 disables
    // mid-file commits (writes to sdmc: are unaffected either way). When `copied`
    // is given it accumulates what the copy actually moved, for the caller to
    // check against its scan. `digests`, when given, collects one CopiedFile per
    // file written, which is what the restore verification checks the save
    // against.
    Result copyDirectory(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink, u64 commitWriteLimit = 0,
        TreeStats* copied = nullptr, size_t expectedFiles = 0, std::vector<CopiedFile>* digests = nullptr);
    // `crcOut`, when given, receives the CRC32 of every byte read from the source.
    Result copyFile(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink, u64 commitWriteLimit = 0, u64* bytesCopied = nullptr,
        u32* crcOut = nullptr);
    Result createDirectory(const std::string& path);
    // Deletes everything under `path`; with `removeRoot` also removes `path`
    // itself. Pass false when `path` is a mount root (e.g. "save:/"), which can
    // never be rmdir'd. Every removed file is reported through `sink` when one is
    // given; a wipe has no byte progress, only a file count, so each is reported
    // with size 0.
    Result deleteFolderRecursively(const std::string& path, bool removeRoot = true, ProgressSink* sink = nullptr);
    bool directoryExists(const std::string& path);
    bool fileExists(const std::string& path);
}

#endif
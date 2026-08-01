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

#include "io.hpp"
#include "configuration.hpp"
#include "logging.hpp"
#include "savedatasource.hpp"
#include "titlecatalog.hpp"
#include <arm_acle.h>
#include <chrono>
#include <cstring>

// Errno-domain copy failures (fopen/fread/fwrite/mkdir) folded into the Result
// channel that IoOutcome carries; the exact cause is in the log.
static const Result RES_COPY_FAILED = MAKERESULT(Module_Libnx, LibnxError_IoError);

// Safety margin kept free in the save journal when committing partway through a
// file: commits themselves consume journal space for filesystem metadata.
static constexpr u64 JOURNAL_COMMIT_MARGIN = 0x100000;

// Extra headroom added on top of the backup size when extending the save data
// partition, so the restored save has room to breathe.
static constexpr u64 SAVE_EXTEND_MARGIN = 0x500000;

// Save data filesystems allocate in 16 KiB clusters: every restored file wastes
// up to one cluster of slack, which adds up for backups with thousands of small
// files (see #541).
static constexpr u64 SAVE_CLUSTER_SIZE = 0x4000;

namespace {
    // Hardware CRC32 (zlib polynomial), same routine as transfer.cpp: only used
    // to compare a backup file against its restored copy, so the exact variant
    // doesn't matter as long as both sides use this function.
    u32 updateCrc(u32 crc, const u8* data, size_t len)
    {
        u32 c = crc;
        while (len >= 8) {
            u64 v;
            std::memcpy(&v, data, 8);
            c = __crc32d(c, v);
            data += 8;
            len -= 8;
        }
        while (len > 0) {
            c = __crc32b(c, *data++);
            len--;
        }
        return c;
    }

    bool fileSizeAndCrc(const std::string& path, u64& outSize, u32& outCrc)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (f == NULL) {
            Logging::error("Verification: failed to open {} with errno {}.", path, errno);
            return false;
        }
        u8* buf  = new u8[BUFFER_SIZE];
        u64 size = 0;
        u32 crc  = 0;
        size_t count;
        while ((count = fread(buf, 1, BUFFER_SIZE, f)) > 0) {
            crc = updateCrc(crc, buf, count);
            size += count;
        }
        const bool readError = ferror(f) != 0;
        delete[] buf;
        fclose(f);
        if (readError) {
            Logging::error("Verification: read error on {} with errno {}.", path, errno);
            return false;
        }
        outSize = size;
        outCrc  = crc;
        return true;
    }

    struct VerifyStats {
        size_t checked      = 0;
        size_t missing      = 0;
        size_t sizeMismatch = 0;
        size_t crcMismatch  = 0;
        size_t unreadable   = 0;
        size_t extraneous   = 0;

        size_t problems() const { return missing + sizeMismatch + crcMismatch + unreadable + extraneous; }
    };

    // Walks the backup tree and checks every file has a byte-identical (size +
    // CRC32) counterpart in the restored save.
    void verifyTree(const std::string& srcPath, const std::string& dstPath, VerifyStats& stats, ProgressSink& sink)
    {
        Directory items(srcPath);
        if (!items.good()) {
            Logging::error("Verification: failed to list backup directory {} with result 0x{:08X}.", srcPath, (u32)items.error());
            stats.unreadable++;
            return;
        }
        for (size_t i = 0, sz = items.size(); i < sz; i++) {
            const std::string src = srcPath + items.entry(i);
            const std::string dst = dstPath + items.entry(i);
            if (items.folder(i)) {
                if (!io::directoryExists(dst)) {
                    Logging::error("Verification: directory {} missing from restored save.", dst);
                    stats.missing++;
                }
                else {
                    verifyTree(src + "/", dst + "/", stats, sink);
                }
            }
            else {
                sink.startFile(items.entry(i), 0);
                stats.checked++;
                u64 srcSize = 0, dstSize = 0;
                u32 srcCrc = 0, dstCrc = 0;
                if (!fileSizeAndCrc(src, srcSize, srcCrc)) {
                    stats.unreadable++;
                }
                else {
                    struct stat st;
                    if (stat(dst.c_str(), &st) != 0) {
                        Logging::error("Verification: file {} missing from restored save (errno {}).", dst, errno);
                        stats.missing++;
                    }
                    else if (!fileSizeAndCrc(dst, dstSize, dstCrc)) {
                        stats.unreadable++;
                    }
                    else if (srcSize != dstSize) {
                        Logging::error("Verification: size mismatch on {}: backup {} bytes, save {} bytes.", dst, srcSize, dstSize);
                        stats.sizeMismatch++;
                    }
                    else if (srcCrc != dstCrc) {
                        Logging::error("Verification: CRC mismatch on {} ({} bytes): backup {:08X}, save {:08X}.", dst, srcSize, srcCrc, dstCrc);
                        stats.crcMismatch++;
                    }
                }
                sink.finishFile();
            }
        }
    }

    // Walks the restored save and flags anything the wipe should have removed
    // but that isn't part of the backup.
    void scanExtraneous(const std::string& dstPath, const std::string& srcPath, VerifyStats& stats)
    {
        Directory items(dstPath);
        if (!items.good()) {
            Logging::error("Verification: failed to list save directory {} with result 0x{:08X}.", dstPath, (u32)items.error());
            stats.unreadable++;
            return;
        }
        for (size_t i = 0, sz = items.size(); i < sz; i++) {
            const std::string dst = dstPath + items.entry(i);
            const std::string src = srcPath + items.entry(i);
            if (items.folder(i)) {
                if (!io::directoryExists(src)) {
                    Logging::error("Verification: extraneous directory {} present in restored save.", dst);
                    stats.extraneous++;
                }
                else {
                    scanExtraneous(dst + "/", src + "/", stats);
                }
            }
            else if (!io::fileExists(src)) {
                Logging::error("Verification: extraneous file {} present in restored save.", dst);
                stats.extraneous++;
            }
        }
    }

}

bool io::fileExists(const std::string& path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

size_t io::countFiles(const std::string& path)
{
    size_t count = 0;
    Directory items(path);
    if (!items.good()) {
        return 0;
    }
    for (size_t i = 0, sz = items.size(); i < sz; i++) {
        if (items.folder(i)) {
            count += io::countFiles(path + items.entry(i) + "/");
        }
        else {
            count++;
        }
    }
    return count;
}

u64 io::directorySize(const std::string& path)
{
    u64 total = 0;
    Directory items(path);
    if (!items.good()) {
        return 0;
    }
    std::string base = path;
    if (!base.empty() && base.back() != '/') {
        base += "/";
    }
    for (size_t i = 0, sz = items.size(); i < sz; i++) {
        const std::string child = base + items.entry(i);
        if (items.folder(i)) {
            total += io::directorySize(child + "/");
        }
        else {
            struct stat st;
            if (stat(child.c_str(), &st) == 0) {
                total += (u64)st.st_size;
            }
        }
    }
    return total;
}

Result io::copyFile(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink, u64 commitWriteLimit)
{
    FILE* src = fopen(srcPath.c_str(), "rb");
    if (src == NULL) {
        Logging::error("Failed to open source file {} during copy with errno {}.", srcPath, errno);
        return RES_COPY_FAILED;
    }
    FILE* dst = fopen(dstPath.c_str(), "wb");
    if (dst == NULL) {
        Logging::error("Failed to open destination file {} during copy with errno {}.", dstPath, errno);
        fclose(src);
        return RES_COPY_FAILED;
    }

    fseek(src, 0, SEEK_END);
    u64 sz = ftell(src);
    rewind(src);

    u8* buf    = new u8[BUFFER_SIZE];
    u64 offset = 0;
    Result res = 0;

    size_t slashpos = srcPath.rfind("/");
    sink.startFile(srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1), sz);

    // The save journal only holds `commitWriteLimit` bytes of uncommitted writes:
    // a single file bigger than that must be committed partway through, or the
    // commit at the end would overflow the journal and fail (#443, #297).
    const bool toSaveDevice = dstPath.rfind("save:/", 0) == 0;
    u64 journalPending      = 0;

    while (offset < sz) {
        if (sink.cancelled()) {
            break;
        }

        size_t count = fread((char*)buf, 1, BUFFER_SIZE, src);
        if (count == 0) {
            Logging::error("fread returned 0 for file {} at offset {}/{} with errno {}. Aborting copy.", srcPath, offset, sz, errno);
            res = RES_COPY_FAILED;
            break;
        }

        // commit *before* the write that would cross the limit, while the
        // journal still has room for it
        if (toSaveDevice && commitWriteLimit > 0 && journalPending + count > commitWriteLimit && journalPending > 0) {
            if (fclose(dst) != 0) {
                Logging::error("fclose before mid-file commit failed for {} with errno {}. Aborting copy.", dstPath, errno);
                dst = NULL;
                res = RES_COPY_FAILED;
                break;
            }
            res = fsdevCommitDevice("save");
            if (R_FAILED(res)) {
                Logging::error("Mid-file commit of {} at offset {}/{} failed with result 0x{:08X}. Aborting copy.", dstPath, offset, sz, (u32)res);
                dst = NULL;
                break;
            }
            dst = fopen(dstPath.c_str(), "ab");
            if (dst == NULL) {
                Logging::error("Failed to reopen {} after mid-file commit with errno {}. Aborting copy.", dstPath, errno);
                res = RES_COPY_FAILED;
                break;
            }
            Logging::debug("Mid-file commit of {} at offset {}/{} OK.", dstPath, offset, sz);
            journalPending = 0;
        }

        if (fwrite((char*)buf, 1, count, dst) != count) {
            Logging::error("fwrite failed for file {} at offset {}/{} with errno {}. Aborting copy.", dstPath, offset, sz, errno);
            res = RES_COPY_FAILED;
            break;
        }
        offset += count;
        journalPending += count;
        sink.advanceBytes(offset);
    }

    delete[] buf;
    fclose(src);
    if (dst != NULL && fclose(dst) != 0 && R_SUCCEEDED(res)) {
        Logging::error("fclose failed for file {} with errno {}.", dstPath, errno);
        res = RES_COPY_FAILED;
    }
    sink.finishFile();

    // commit each file to the save, so a huge restore doesn't accumulate one
    // giant uncommitted journal
    if (R_SUCCEEDED(res) && toSaveDevice) {
        res = fsdevCommitDevice("save");
        if (R_FAILED(res)) {
            Logging::error("Failed to commit file {} to the save archive with result 0x{:08X}.", dstPath, (u32)res);
        }
    }
    return res;
}

Result io::copyDirectory(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink, u64 commitWriteLimit)
{
    Result res = 0;
    Directory items(srcPath);

    if (!items.good()) {
        return items.error();
    }

    for (size_t i = 0, sz = items.size(); i < sz && R_SUCCEEDED(res); i++) {
        if (sink.cancelled()) {
            break;
        }

        std::string newsrc = srcPath + items.entry(i);
        std::string newdst = dstPath + items.entry(i);

        if (items.folder(i)) {
            res = io::createDirectory(newdst);
            if (R_SUCCEEDED(res)) {
                newsrc += "/";
                newdst += "/";
                res = io::copyDirectory(newsrc, newdst, sink, commitWriteLimit);
            }
        }
        else {
            res = io::copyFile(newsrc, newdst, sink, commitWriteLimit);
        }
    }

    return res;
}

Result io::createDirectory(const std::string& path)
{
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) {
        Logging::error("Failed to create directory {} with errno {}.", path, errno);
        return RES_COPY_FAILED;
    }
    return 0;
}

bool io::directoryExists(const std::string& path)
{
    struct stat sb;
    return (stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
}

Result io::deleteFolderRecursively(const std::string& path, bool removeRoot, ProgressSink* sink)
{
    Directory dir(path);
    if (!dir.good()) {
        Logging::error("Delete: failed to list directory {} with errno {}.", path, (u32)dir.error());
        return dir.error();
    }

    Result firstError = 0;
    // A path already gone is not a failed deletion: readdir snapshots can go
    // stale, and the goal (the entry not existing) is met either way (#541).
    auto note = [&](int rc, const std::string& target) {
        if (rc == 0) {
            return;
        }
        if (errno == ENOENT) {
            Logging::debug("Delete: {} reported ENOENT, ignoring.", target);
            return;
        }
        Logging::error("Delete: failed to delete {} with errno {}.", target, errno);
        if (firstError == 0) {
            firstError = errno ? errno : -1;
        }
    };

    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            std::string newpath = path + dir.entry(i) + "/";
            Result sub          = deleteFolderRecursively(newpath, true, sink);
            if (sub != 0 && firstError == 0) {
                firstError = sub;
            }
            newpath = path + dir.entry(i);
            note(rmdir(newpath.c_str()), newpath);
        }
        else {
            std::string newpath = path + dir.entry(i);
            note(std::remove(newpath.c_str()), newpath);
            if (sink != nullptr) {
                sink->startFile(dir.entry(i), 0);
                sink->finishFile();
            }
        }
    }

    if (removeRoot) {
        note(rmdir(path.c_str()), path);
    }
    return firstError;
}

io::IoOutcome io::backup(Title& title, const std::string& dstPath, ProgressSink& sink)
{
    Logging::info("Started backup of {}. Title id: 0x{:016X}; User id: 0x{:X}{:X}.", title.name().c_str(), title.id(), title.userId().uid[1],
        title.userId().uid[0]);

    Result res = SaveDataSource(title.saveDataType()).mount(title);
    if (R_FAILED(res)) {
        Logging::error("Failed to mount filesystem during backup with result 0x{:08X}. Title id: 0x{:016X}.", res, title.id());
        return {false, res, io::BackupStage::OpenArchive};
    }

    if (io::directoryExists(dstPath)) {
        int rc = io::deleteFolderRecursively((dstPath + "/").c_str());
        if (rc != 0) {
            FileSystem::unmountDevice();
            Logging::error("Failed to recursively delete directory {} with result {}.", dstPath, rc);
            return {false, (Result)rc, io::BackupStage::DeleteDst};
        }
    }

    res = io::createDirectory(dstPath);
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to create directory {} with result 0x{:08X}.", dstPath, (u32)res);
        return {false, res, io::BackupStage::CreateDst};
    }
    sink.begin("Backup", io::countFiles("save:/"));
    res = io::copyDirectory("save:/", dstPath + "/", sink);
    sink.end();
    if (sink.cancelled()) {
        FileSystem::unmountDevice();
        io::deleteFolderRecursively((dstPath + "/").c_str());
        Logging::info("Backup of {} cancelled by user.", title.name().c_str());
        return {false, 0, io::BackupStage::Copy, true};
    }
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        io::deleteFolderRecursively((dstPath + "/").c_str());
        Logging::error("Failed to copy directory {} with result 0x{:08X}. Skipping...", dstPath, res);
        return {false, res, io::BackupStage::Copy};
    }

    // The backup-folder list is refreshed by the caller on the main thread:
    // io::backup runs on the TransferJob worker and the Switch TitleCatalog has no
    // mutex, so the worker must not mutate it while the UI thread reads it.
    FileSystem::unmountDevice();
    Logging::info("Backup succeeded.");
    return {true, 0, io::BackupStage::Copy};
}

io::IoOutcome io::restore(Title& title, const std::string& srcPath, ProgressSink& sink)
{
    Logging::info(
        "Started restore of {}. Title id: 0x{:016X}; User id: 0x{:X}{:X}; Source: {}; Save data type: {}; Space id: {}; Save id: 0x{:016X}.",
        title.name().c_str(), title.id(), title.userId().uid[1], title.userId().uid[0], srcPath, (int)title.saveDataType(),
        (int)title.saveDataSpaceId(), title.saveId());

    // The extra data holds the *actual* current data/journal sizes of this save
    // container (the NACP only has the initial ones, stale once a save has been
    // extended). Read before mounting: extending requires the save unmounted.
    u64 journalSize               = 0;
    FsSaveDataExtraData extraData = {};
    Result res =
        fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&extraData, sizeof(extraData), (FsSaveDataSpaceId)title.saveDataSpaceId(), title.saveId());
    if (R_SUCCEEDED(res)) {
        journalSize = (u64)extraData.journal_size;
        Logging::info("Save extra data: data_size={} journal_size={}.", (u64)extraData.data_size, journalSize);
    }
    else {
        Logging::error("Failed to read save extra data with result 0x{:08X}. Title id: 0x{:016X}. "
                       "Restoring without journal awareness.",
            (u32)res, title.id());
    }

    Logging::info("Scanning backup {} (this can take minutes for backups with many files)...", srcPath);
    const size_t fileCount = io::countFiles(srcPath);
    const u64 backupSize   = io::directorySize(srcPath);
    Logging::info("Backup to restore: {} files, {} bytes total.", fileCount, backupSize);

    // If the backup doesn't fit the currently allocated save data, grow the
    // partition before restoring: a save can outgrow its original allocation as
    // the game adds content (#443, #297, #541).
    if (journalSize > 0 && title.saveDataType() != FsSaveDataType_System) {
        // each file wastes up to one allocation cluster on the save filesystem
        const u64 neededSize = backupSize + (u64)fileCount * SAVE_CLUSTER_SIZE + SAVE_EXTEND_MARGIN;
        if (neededSize > (u64)extraData.data_size) {
            res = fsExtendSaveDataFileSystem((FsSaveDataSpaceId)title.saveDataSpaceId(), title.saveId(), (s64)neededSize, (s64)journalSize);
            if (R_FAILED(res)) {
                Logging::error("Failed to extend save data from {} to {} bytes with result 0x{:08X}. Title id: 0x{:016X}.", (u64)extraData.data_size,
                    neededSize, (u32)res, title.id());
                return {false, res, io::BackupStage::OpenArchive};
            }
            Logging::info("Extended save data of title 0x{:016X} from {} to {} bytes to fit backup of {} bytes.", title.id(),
                (u64)extraData.data_size, neededSize, backupSize);
        }
        else {
            Logging::info("No extend needed: backup needs {} bytes, save data already has {}.", neededSize, (u64)extraData.data_size);
        }
    }

    res = SaveDataSource(title.saveDataType()).mount(title);
    if (R_FAILED(res)) {
        Logging::error("Failed to mount filesystem during restore with result 0x{:08X}. Title id: 0x{:016X}.", res, title.id());
        return {false, res, io::BackupStage::OpenArchive};
    }

    std::string dstPath = "save:/";

    sink.begin("Clearing", io::countFiles(dstPath));
    res = io::deleteFolderRecursively(dstPath.c_str(), false, &sink);
    sink.end();
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to recursively delete directory {} with result 0x{:08X}.", dstPath, res);
        return {false, res, io::BackupStage::DeleteDst};
    }

    // commit the wipe on its own, so the deletions don't eat into the journal
    // budget of the copies that follow
    res = fsdevCommitDevice("save");
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to commit save wipe with result 0x{:08X}.", (u32)res);
        return {false, res, io::BackupStage::Commit};
    }

    // leave a margin under the journal size so in-flight writes never overflow
    // it; 0 (extra data unavailable) disables mid-file commits
    const u64 commitWriteLimit = journalSize > JOURNAL_COMMIT_MARGIN ? journalSize - JOURNAL_COMMIT_MARGIN : journalSize;
    Logging::info("Copy starting with commitWriteLimit={} (journal size {}).", commitWriteLimit, journalSize);

    const auto copyStart = std::chrono::steady_clock::now();
    sink.begin("Restore", fileCount);
    res = io::copyDirectory(srcPath, dstPath, sink, commitWriteLimit);
    sink.end();
    const auto copySeconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - copyStart).count();
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to copy directory {} to {} with result 0x{:08X}. Skipping...", srcPath, dstPath, res);
        return {false, res, io::BackupStage::Copy};
    }
    Logging::info("Copy phase finished in {} s.", copySeconds);

    res = fsdevCommitDevice("save");
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to commit save with result 0x{:08X}.", res);
        return {false, res, io::BackupStage::Commit};
    }
    FileSystem::unmountDevice();

    if (!Configuration::getInstance().isVerifyRestoreEnabled()) {
        Logging::info("Restore succeeded (verification disabled in settings).");
        return {true, 0, io::BackupStage::Copy};
    }

    // Verify against a *fresh* mount, so what is compared is what actually got
    // committed to disk — not a cached view of the writes above (#541).
    res = SaveDataSource(title.saveDataType()).mount(title);
    if (R_FAILED(res)) {
        Logging::error("Failed to remount save for post-restore verification with result 0x{:08X}. Skipping verification.", (u32)res);
        Logging::info("Restore succeeded (unverified).");
        return {true, 0, io::BackupStage::Copy};
    }

    VerifyStats stats;
    const auto verifyStart = std::chrono::steady_clock::now();
    sink.begin("Verify", fileCount);
    verifyTree(srcPath, dstPath, stats, sink);
    scanExtraneous(dstPath, srcPath, stats);
    sink.end();
    const auto verifySeconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - verifyStart).count();
    FileSystem::unmountDevice();

    Logging::info("Verification: {} files checked in {} s — {} missing, {} size mismatches, {} CRC mismatches, {} unreadable, {} extraneous.",
        stats.checked, verifySeconds, stats.missing, stats.sizeMismatch, stats.crcMismatch, stats.unreadable, stats.extraneous);
    if (stats.problems() > 0) {
        Logging::error("Restore verification FAILED: the data on the save does not match the backup. See mismatches above.");
        return {false, RES_COPY_FAILED, io::BackupStage::Verify};
    }

    Logging::info("Restore succeeded (verified).");
    return {true, 0, io::BackupStage::Copy};
}
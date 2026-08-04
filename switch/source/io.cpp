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
#include <algorithm>
#include <arm_acle.h>
#include <chrono>
#include <cstring>

// Errno-domain copy failures (fopen/fread/fwrite/mkdir) folded into the Result
// channel that IoOutcome carries; the exact cause is in the log.
static const Result RES_COPY_FAILED = MAKERESULT(Module_Libnx, LibnxError_IoError);

// Safety margin kept free in the save journal when committing partway through a
// file: commits themselves consume journal space for filesystem metadata.
static constexpr u64 JOURNAL_COMMIT_MARGIN = 0x100000;

// Headroom left free in the save data partition on top of what the backup
// occupies. The game has to be able to write to its own save after a restore:
// a container sized exactly to the backup leaves it nothing to work with, and
// the save filesystem also slows down sharply as it approaches full. Whichever
// of the two is larger is used.
static constexpr u64 SAVE_EXTEND_MIN_HEADROOM = 0x4000000; // 64 MiB
static constexpr u64 SAVE_EXTEND_HEADROOM_DIV = 10;        // or 10% of the payload

// Save data filesystems allocate in 16 KiB clusters: every restored file wastes
// up to one cluster of slack, which adds up for backups with thousands of small
// files.
static constexpr u64 SAVE_CLUSTER_SIZE = 0x4000;

namespace {
    void scanTreeInto(const std::string& path, io::TreeStats& stats, ProgressSink* sink)
    {
        Directory items(path);
        if (!items.good()) {
            Logging::error("Scan: failed to list {} with error 0x{:08X}.", path, (u32)items.error());
            stats.unreadable++;
            return;
        }
        for (size_t i = 0, sz = items.size(); i < sz; i++) {
            const std::string child = path + items.entry(i);
            if (items.folder(i)) {
                stats.dirs++;
                scanTreeInto(child + "/", stats, sink);
            }
            else {
                stats.files++;
                if (sink != nullptr) {
                    sink->startFile(items.entry(i), 0);
                }
                struct stat st;
                if (stat(child.c_str(), &st) == 0) {
                    stats.bytes += (u64)st.st_size;
                }
                else {
                    Logging::error("Scan: stat failed on {} with errno {}.", child, errno);
                    stats.unreadable++;
                }
                if (sink != nullptr) {
                    sink->finishFile();
                }
            }
        }
    }

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

    enum class ReadOutcome { Ok, Missing, Unreadable };

    // Reads `path` whole and hands back its size and CRC32. `buf` is the caller's
    // scratch buffer on purpose: verification opens tens of thousands of files
    // back to back, and a fresh 512 KiB allocation per file was pure overhead.
    ReadOutcome fileSizeAndCrc(const std::string& path, u8* buf, u64& outSize, u32& outCrc, ProgressSink* sink = nullptr)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (f == NULL) {
            if (errno == ENOENT) {
                return ReadOutcome::Missing;
            }
            Logging::error("Verification: failed to open {} with errno {}.", path, errno);
            return ReadOutcome::Unreadable;
        }
        u64 size = 0;
        u32 crc  = 0;
        size_t count;
        while ((count = fread(buf, 1, BUFFER_SIZE, f)) > 0) {
            crc = updateCrc(crc, buf, count);
            size += count;
            if (sink != nullptr) {
                sink->advanceBytes(size);
            }
        }
        const bool readError = ferror(f) != 0;
        fclose(f);
        if (readError) {
            Logging::error("Verification: read error on {} with errno {}.", path, errno);
            return ReadOutcome::Unreadable;
        }
        outSize = size;
        outCrc  = crc;
        return ReadOutcome::Ok;
    }

    struct VerifyStats {
        size_t checked      = 0;
        u64 bytes           = 0;
        size_t missing      = 0;
        size_t sizeMismatch = 0;
        size_t crcMismatch  = 0;
        size_t unreadable   = 0;

        size_t problems() const { return missing + sizeMismatch + crcMismatch + unreadable; }
    };

    // Re-reads every file the copy wrote, off a freshly mounted save, and compares
    // it against the CRC32 the copy took of the bytes it moved. Only the save is
    // read: the backup was already read once, by the copy.
    void verifyCopiedFiles(const std::vector<io::CopiedFile>& copied, VerifyStats& stats, ProgressSink& sink)
    {
        std::vector<u8> buf(BUFFER_SIZE);
        for (const io::CopiedFile& file : copied) {
            const size_t slashpos = file.path.rfind('/');
            sink.startFile(file.path.substr(slashpos + 1), file.size);
            stats.checked++;
            stats.bytes += file.size;

            u64 size = 0;
            u32 crc  = 0;
            switch (fileSizeAndCrc(file.path, buf.data(), size, crc, &sink)) {
                case ReadOutcome::Missing:
                    Logging::error("Verification: file {} missing from restored save.", file.path);
                    stats.missing++;
                    break;
                case ReadOutcome::Unreadable:
                    stats.unreadable++;
                    break;
                case ReadOutcome::Ok:
                    if (size != file.size) {
                        Logging::error("Verification: size mismatch on {}: copied {} bytes, save holds {} bytes.", file.path, file.size, size);
                        stats.sizeMismatch++;
                    }
                    else if (crc != file.crc) {
                        Logging::error(
                            "Verification: CRC mismatch on {} ({} bytes): copied {:08X}, save holds {:08X}.", file.path, file.size, file.crc, crc);
                        stats.crcMismatch++;
                    }
                    break;
            }
            sink.finishFile();
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

io::TreeStats io::scanTree(const std::string& path, ProgressSink* sink)
{
    TreeStats total;
    scanTreeInto(path, total, sink);
    return total;
}

Result io::copyFile(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink, u64 commitWriteLimit, u64* bytesCopied, u32* crcOut)
{
    FILE* src = fopen(srcPath.c_str(), "rb");
    if (src == NULL) {
        Logging::error("Failed to open source file {} during copy with errno {}.", srcPath, errno);
        return RES_COPY_FAILED;
    }
    fseek(src, 0, SEEK_END);
    u64 sz = ftell(src);
    rewind(src);

    const bool toSaveDevice = dstPath.rfind("save:/", 0) == 0;

    // Create the destination at its final size instead of letting it grow one
    // write at a time. Every extending write on the save filesystem has to find
    // and chain a free block, so an append-grown tree of tens of thousands of
    // files both fragments and gets slower as the save fills.
    bool preallocated = false;
    if (toSaveDevice && sz > 0) {
        const Result cr = fsdevCreateFile(dstPath.c_str(), (size_t)sz, 0);
        if (R_SUCCEEDED(cr)) {
            preallocated = true;
        }
        else {
            Logging::debug("Preallocating {} at {} bytes failed with result 0x{:08X}; growing it by writes instead.", dstPath, sz, (u32)cr);
        }
    }

    // "wb" truncates, which would throw the preallocation away: an already-sized
    // file has to be opened for update instead.
    FILE* dst = fopen(dstPath.c_str(), preallocated ? "r+b" : "wb");
    if (dst == NULL) {
        Logging::error("Failed to open destination file {} during copy with errno {}.", dstPath, errno);
        fclose(src);
        return RES_COPY_FAILED;
    }

    u8* buf    = new u8[BUFFER_SIZE];
    u64 offset = 0;
    u32 crc    = 0;
    Result res = 0;

    size_t slashpos = srcPath.rfind("/");
    sink.startFile(srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1), sz);

    // The save journal only holds `commitWriteLimit` bytes of uncommitted writes:
    // a single file bigger than that must be committed partway through, or the
    // commit at the end would overflow the journal and fail (#443, #297).
    u64 journalPending = 0;

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

        // Checksum on the way through: the data is already in the buffer and the
        // CRC32 is a hardware instruction, so this costs the copy nothing and
        // saves the verification pass a full second read of the backup.
        if (crcOut != nullptr) {
            crc = updateCrc(crc, buf, count);
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
            // A preallocated file is already `sz` long, so appending would write
            // past the data instead of into it: reopen for update and seek back.
            dst = fopen(dstPath.c_str(), preallocated ? "r+b" : "ab");
            if (dst == NULL) {
                Logging::error("Failed to reopen {} after mid-file commit with errno {}. Aborting copy.", dstPath, errno);
                res = RES_COPY_FAILED;
                break;
            }
            if (preallocated && fseek(dst, (long)offset, SEEK_SET) != 0) {
                Logging::error("Failed to seek {} back to offset {} after mid-file commit with errno {}. Aborting copy.", dstPath, offset, errno);
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
    // stdio buffers, so a write that the save filesystem rejects usually only
    // surfaces here: never treat a copy as complete without checking the close
    if (dst != NULL && fclose(dst) != 0 && R_SUCCEEDED(res)) {
        Logging::error("fclose failed for file {} with errno {}.", dstPath, errno);
        res = RES_COPY_FAILED;
    }
    sink.finishFile();

    // A loop that ends early without setting `res` would otherwise hand back a
    // short file as a successful copy.
    if (R_SUCCEEDED(res) && offset != sz && !sink.cancelled()) {
        Logging::error("Copy of {} ended at {} of {} bytes without an error. Treating as failed.", dstPath, offset, sz);
        res = RES_COPY_FAILED;
    }

    if (bytesCopied != nullptr) {
        *bytesCopied = offset;
    }
    if (crcOut != nullptr) {
        *crcOut = crc;
    }

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

Result io::copyDirectory(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink, u64 commitWriteLimit, TreeStats* copied,
    std::vector<CopiedFile>* digests)
{
    Result res = 0;
    Directory items(srcPath);

    if (!items.good()) {
        Logging::error("Copy: failed to list source directory {} with error 0x{:08X}. Aborting copy.", srcPath, (u32)items.error());
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
                if (copied != nullptr) {
                    copied->dirs++;
                }
                newsrc += "/";
                newdst += "/";
                res = io::copyDirectory(newsrc, newdst, sink, commitWriteLimit, copied, digests);
            }
        }
        else {
            u64 bytes = 0;
            u32 crc   = 0;
            res       = io::copyFile(newsrc, newdst, sink, commitWriteLimit, &bytes, digests != nullptr ? &crc : nullptr);
            if (digests != nullptr && R_SUCCEEDED(res)) {
                digests->push_back(CopiedFile{newdst, bytes, crc});
            }
            if (copied != nullptr && R_SUCCEEDED(res)) {
                copied->files++;
                copied->bytes += bytes;
            }
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
    // stale, and the goal (the entry not existing) is met either way.
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
    const io::TreeStats saveTree = io::scanTree("save:/");
    if (saveTree.unreadable > 0) {
        FileSystem::unmountDevice();
        Logging::error(
            "Refusing to back up: {} entries under save:/ could not be read, so the backup would be silently incomplete.", saveTree.unreadable);
        return {false, RES_COPY_FAILED, io::BackupStage::Copy};
    }

    io::TreeStats copiedTree;
    sink.begin("Backup", saveTree.files);
    res = io::copyDirectory("save:/", dstPath + "/", sink, 0, &copiedTree);
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

    // A copy that reported success but moved less than the scan found means the
    // walk lost part of the tree without any single operation failing; the
    // backup on the SD card would look fine and restore to a broken save.
    if (copiedTree.files != saveTree.files || copiedTree.bytes != saveTree.bytes) {
        FileSystem::unmountDevice();
        io::deleteFolderRecursively((dstPath + "/").c_str());
        Logging::error("Backup incomplete: copied {} files / {} bytes but the save holds {} files / {} bytes. Discarding the backup.",
            copiedTree.files, copiedTree.bytes, saveTree.files, saveTree.bytes);
        return {false, RES_COPY_FAILED, io::BackupStage::Copy};
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
    const io::TreeStats backupTree = io::scanTree(srcPath);
    const size_t fileCount         = backupTree.files;
    const u64 backupSize           = backupTree.bytes;
    Logging::info("Backup to restore: {} files, {} dirs, {} bytes total.", fileCount, backupTree.dirs, backupSize);

    // Restoring a tree we could not fully read would wipe the save and put back
    // whatever part of the backup happened to be listable — a restore that
    // "succeeds" and leaves the game unable to load.
    if (backupTree.unreadable > 0) {
        Logging::error("Refusing to restore: {} entries under {} could not be read. The save has not been touched.", backupTree.unreadable, srcPath);
        return {false, RES_COPY_FAILED, io::BackupStage::Copy};
    }
    if (fileCount == 0) {
        Logging::error("Refusing to restore: {} contains no files. The save has not been touched.", srcPath);
        return {false, RES_COPY_FAILED, io::BackupStage::Copy};
    }

    // If the backup doesn't fit the currently allocated save data, grow the
    // partition before restoring: a save can outgrow its original allocation as
    // the game adds content (#443, #297).
    if (journalSize > 0 && title.saveDataType() != FsSaveDataType_System) {
        // each file wastes up to one allocation cluster on the save filesystem
        const u64 payload    = backupSize + (u64)fileCount * SAVE_CLUSTER_SIZE;
        const u64 headroom   = std::max(SAVE_EXTEND_MIN_HEADROOM, payload / SAVE_EXTEND_HEADROOM_DIV);
        const u64 neededSize = payload + headroom;
        if (neededSize > (u64)extraData.data_size) {
            res = fsExtendSaveDataFileSystem((FsSaveDataSpaceId)title.saveDataSpaceId(), title.saveId(), (s64)neededSize, (s64)journalSize);
            if (R_FAILED(res)) {
                Logging::error("Failed to extend save data from {} to {} bytes with result 0x{:08X}. Title id: 0x{:016X}.", (u64)extraData.data_size,
                    neededSize, (u32)res, title.id());
                return {false, res, io::BackupStage::OpenArchive};
            }
            Logging::info("Extended save data of title 0x{:016X} from {} to {} bytes to fit backup of {} bytes.", title.id(),
                (u64)extraData.data_size, neededSize, backupSize);

            // The system is free to round or clamp what it grants, so the only
            // trustworthy figure is the one read back afterwards.
            FsSaveDataExtraData grown = {};
            Result readBack =
                fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(&grown, sizeof(grown), (FsSaveDataSpaceId)title.saveDataSpaceId(), title.saveId());
            if (R_SUCCEEDED(readBack)) {
                if ((u64)grown.data_size < neededSize) {
                    Logging::warning(
                        "Extend granted {} bytes, {} were asked for. The restore may run out of space.", (u64)grown.data_size, neededSize);
                }
                journalSize = (u64)grown.journal_size;
            }
            else {
                Logging::error("Failed to re-read save extra data after extend with result 0x{:08X}.", (u32)readBack);
            }
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

    // A wipe that left entries behind means the restore starts on top of the old
    // save: files the backup does not contain survive, and the free space the
    // copy is about to need is already spent.
    const io::TreeStats leftovers = io::scanTree(dstPath);
    if (leftovers.files > 0 || leftovers.dirs > 0 || leftovers.unreadable > 0) {
        FileSystem::unmountDevice();
        Logging::error("Wipe left {} files, {} dirs and {} unreadable entries under save:/. Aborting before the copy.", leftovers.files,
            leftovers.dirs, leftovers.unreadable);
        return {false, RES_COPY_FAILED, io::BackupStage::DeleteDst};
    }

    // leave a margin under the journal size so in-flight writes never overflow
    // it; 0 (extra data unavailable) disables mid-file commits
    const u64 commitWriteLimit = journalSize > JOURNAL_COMMIT_MARGIN ? journalSize - JOURNAL_COMMIT_MARGIN : journalSize;
    Logging::info("Copy starting with commitWriteLimit={} (journal size {}).", commitWriteLimit, journalSize);

    // With byte-for-byte verification on, the copy hands back a CRC32 per file so
    // the check that follows only has to read the save.
    const bool verifyBytes = Configuration::getInstance().isVerifyRestoreEnabled();
    std::vector<io::CopiedFile> copiedFiles;
    if (verifyBytes) {
        copiedFiles.reserve(fileCount);
    }

    io::TreeStats copiedTree;
    const auto copyStart = std::chrono::steady_clock::now();
    sink.begin("Restore", fileCount);
    res = io::copyDirectory(srcPath, dstPath, sink, commitWriteLimit, &copiedTree, verifyBytes ? &copiedFiles : nullptr);
    sink.end();
    const auto copySeconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - copyStart).count();
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to copy directory {} to {} with result 0x{:08X}. Skipping...", srcPath, dstPath, res);
        return {false, res, io::BackupStage::Copy};
    }
    Logging::info("Copy phase finished in {} s.", copySeconds);

    // The cheap structural guarantee, always on: whatever the CRC verification
    // setting says, a restore never reports success after moving fewer files or
    // fewer bytes than the pre-flight scan of the backup found.
    if (copiedTree.files != backupTree.files || copiedTree.dirs != backupTree.dirs || copiedTree.bytes != backupTree.bytes) {
        FileSystem::unmountDevice();
        Logging::error("Restore incomplete: copied {} files / {} dirs / {} bytes but the backup holds {} files / {} dirs / {} bytes.",
            copiedTree.files, copiedTree.dirs, copiedTree.bytes, backupTree.files, backupTree.dirs, backupTree.bytes);
        return {false, RES_COPY_FAILED, io::BackupStage::Copy};
    }

    res = fsdevCommitDevice("save");
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to commit save with result 0x{:08X}.", res);
        return {false, res, io::BackupStage::Commit};
    }
    FileSystem::unmountDevice();

    // Check against a *fresh* mount, so what is inspected is what actually got
    // committed to disk — not a cached view of the writes above.
    res = SaveDataSource(title.saveDataType()).mount(title);
    if (R_FAILED(res)) {
        Logging::error("Failed to remount save for the post-restore check with result 0x{:08X}. Skipping it.", (u32)res);
        Logging::info("Restore succeeded (unverified).");
        return {true, 0, io::BackupStage::Copy};
    }

    // Structural check: re-walk the committed save and compare its shape against
    // the backup. One directory walk, no file reads — it catches a save that lost
    // entries between the writes and the commit. Redundant when the byte-for-byte
    // pass below runs, since that one opens every file the copy wrote.
    if (!verifyBytes) {
        sink.begin("Verify", fileCount);
        const io::TreeStats restoredTree = io::scanTree(dstPath, &sink);
        sink.end();
        FileSystem::unmountDevice();
        if (restoredTree.files != backupTree.files || restoredTree.dirs != backupTree.dirs || restoredTree.bytes != backupTree.bytes ||
            restoredTree.unreadable > 0) {
            Logging::error(
                "Restore check FAILED: save holds {} files / {} dirs / {} bytes ({} unreadable), backup holds {} files / {} dirs / {} bytes.",
                restoredTree.files, restoredTree.dirs, restoredTree.bytes, restoredTree.unreadable, backupTree.files, backupTree.dirs,
                backupTree.bytes);
            return {false, RES_COPY_FAILED, io::BackupStage::Verify};
        }
        Logging::info("Restore succeeded (structure matches; byte-for-byte verification disabled in settings).");
        return {true, 0, io::BackupStage::Copy};
    }

    VerifyStats stats;
    const auto verifyStart = std::chrono::steady_clock::now();
    sink.begin("Verify", copiedFiles.size());
    verifyCopiedFiles(copiedFiles, stats, sink);
    sink.end();
    const auto verifySeconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - verifyStart).count();
    FileSystem::unmountDevice();

    Logging::info("Verification: {} files / {} bytes checked in {} s — {} missing, {} size mismatches, {} CRC mismatches, {} unreadable.",
        stats.checked, stats.bytes, verifySeconds, stats.missing, stats.sizeMismatch, stats.crcMismatch, stats.unreadable);
    if (stats.problems() > 0) {
        Logging::error("Restore verification FAILED: the data on the save does not match the backup. See mismatches above.");
        return {false, RES_COPY_FAILED, io::BackupStage::Verify};
    }
    if (stats.checked != backupTree.files) {
        Logging::error("Restore verification FAILED: checked {} files but the backup holds {}.", stats.checked, backupTree.files);
        return {false, RES_COPY_FAILED, io::BackupStage::Verify};
    }

    Logging::info("Restore succeeded (verified).");
    return {true, 0, io::BackupStage::Copy};
}
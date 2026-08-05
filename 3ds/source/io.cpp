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
#include "backuptarget.hpp"
#include "csvc.hpp"
#include "dscard.hpp"
#include "gbasave.hpp"
#include "loader.hpp"

// Synthetic failure Result for a short write (FSFILE_Write reported success but
// committed fewer bytes than requested — typically a full archive). Negative so
// R_FAILED() is true; the exact value is opaque, the BackupStage carries meaning.
static const Result RES_SHORT_WRITE = MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_FS, RD_TOO_LARGE);

// FS returns this (FS module, WrongArgument) when a read runs past the real,
// allocated data of a sparse / partly-populated extdata file — FSFILE_Read
// rejects the whole request instead of short-reading. It is not a hard failure:
// it marks the true end of readable data, which can fall short of the size
// FSFILE_GetSize reports. See copyFile's shrink-and-stop handling.
static const u32 RES_FS_PAST_DATA = 0xD900458B;

static const Result RES_PATH_TOO_LONG = 0xE0E046C8;

static std::u16string rawBackupFile(const std::u16string& folder)
{
    std::u16string canonical = folder + StringUtils::UTF8toUTF16("00000001.sav");
    if (io::fileExists(Archive::sdmc(), canonical)) {
        return canonical;
    }

    Directory dir(Archive::sdmc(), folder);
    if (!dir.good()) {
        return canonical;
    }
    std::u16string only;
    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            continue;
        }
        if (!only.empty()) {
            // More than one candidate: stay with the canonical name so the
            // failure names the file Checkpoint expects.
            return canonical;
        }
        only = folder + dir.entry(i);
    }
    return only.empty() ? canonical : only;
}

bool io::fileExists(const std::string& path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool io::fileExists(FS_Archive archive, const std::u16string& path)
{
    FSStream stream(archive, path, FS_OPEN_READ);
    bool exist = stream.good();
    stream.close();
    return exist;
}

static Result collectTreeImpl(FS_Archive arch, const std::u16string& root, const std::u16string& sub, std::vector<io::TreeEntry>& out)
{
    // A directory deep enough to overflow FS's path limit would fail the open with
    // an opaque Result; name it here instead, and stop the walk on the same code
    // checkPathLengths would report for the entries under it.
    if (root.length() + sub.length() > io::MAX_PATH_UNITS) {
        Logging::error("Path too long to enumerate ({} units, limit {}): {}", root.length() + sub.length(), io::MAX_PATH_UNITS,
            StringUtils::UTF16toUTF8(root + sub));
        return RES_PATH_TOO_LONG;
    }

    Directory items(arch, root + sub);
    if (!items.good()) {
        return items.error();
    }
    for (size_t i = 0, sz = items.size(); i < sz; i++) {
        std::u16string rel = sub + items.entry(i);
        if (items.folder(i)) {
            out.push_back({rel, true});
            Result res = collectTreeImpl(arch, root, rel + StringUtils::UTF8toUTF16("/"), out);
            if (R_FAILED(res)) {
                return res;
            }
        }
        else {
            out.push_back({rel, false});
        }
    }
    return 0;
}

Result io::collectTree(FS_Archive arch, const std::u16string& path, std::vector<TreeEntry>& out)
{
    return collectTreeImpl(arch, path, StringUtils::UTF8toUTF16(""), out);
}

Result io::checkPathLengths(const std::vector<TreeEntry>& entries, const std::u16string& srcRoot, const std::u16string& dstRoot)
{
    auto tooLong = [](const std::u16string& path) {
        if (path.length() <= MAX_PATH_UNITS) {
            return false;
        }
        Logging::error("Path too long for FS ({} units, limit {}): {}", path.length(), MAX_PATH_UNITS, StringUtils::UTF16toUTF8(path));
        return true;
    };

    if (tooLong(srcRoot) || tooLong(dstRoot)) {
        return RES_PATH_TOO_LONG;
    }

    for (const auto& entry : entries) {
        if (tooLong(srcRoot + entry.rel) || tooLong(dstRoot + entry.rel)) {
            return RES_PATH_TOO_LONG;
        }
    }

    return 0;
}

Result io::copyFile(
    FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath, ProgressSink& sink, u8* buffer)
{
    u32 size = 0;
    FSStream input(srcArch, srcPath, FS_OPEN_READ);
    if (input.good()) {
        size = input.size() > BUFFER_SIZE ? BUFFER_SIZE : input.size();
    }
    else {
        Logging::error("Failed to open source file {} during copy with result {}.", StringUtils::UTF16toUTF8(srcPath), input.result());
        return input.result();
    }

    FSStream output(dstArch, dstPath, FS_OPEN_WRITE, input.size());
    if (!output.good()) {
        Logging::error("Failed to open destination file {} during copy with result {}.", StringUtils::UTF16toUTF8(dstPath), output.result());
        input.close();
        return output.result();
    }

    size_t slashpos = srcPath.rfind(StringUtils::UTF8toUTF16("/"));
    sink.startFile(srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1), input.size());

    Result res = 0;
    u32 offset = 0;
    u32 chunk  = size; // shrinks toward the readable-data boundary on RES_FS_PAST_DATA
    do {
        if (sink.cancelled()) {
            break;
        }
        u32 rd = input.read(buffer, chunk);
        if (R_FAILED(input.result())) {
            // Sparse extdata: this offset+chunk runs past the file's real data. Halve
            // the request to recover any readable prefix at this offset; once even a
            // single byte can't be read here, we've hit the true end of data — stop
            // this file cleanly and keep the backup going.
            if ((u32)input.result() == RES_FS_PAST_DATA) {
                if (chunk > 1) {
                    chunk /= 2;
                    continue;
                }
                Logging::info("Reached end of readable data for {} at offset {} (sparse extdata); backing up {} bytes.",
                    StringUtils::UTF16toUTF8(srcPath), offset, offset);
                break;
            }
            res = input.result();
            Logging::error("Read failure during copy of {} with result 0x{:08X}.", StringUtils::UTF16toUTF8(srcPath), (u32)res);
            break;
        }
        if (rd == 0) {
            break;
        }
        u32 wt = output.write(buffer, rd);
        if (R_FAILED(output.result())) {
            res = output.result();
            Logging::error("Write failure during copy of {} with result 0x{:08X}.", StringUtils::UTF16toUTF8(dstPath), (u32)res);
            break;
        }
        if (wt != rd) {
            res = RES_SHORT_WRITE;
            Logging::error("Short write during copy of {}: wrote {} of {} bytes.", StringUtils::UTF16toUTF8(dstPath), wt, rd);
            break;
        }
        offset += rd;
        sink.advanceBytes(offset);
    } while (!input.eof());
    if (res == 0) {
        sink.finishFile();
    }

    input.close();
    output.close();
    return res;
}

Result io::copyTree(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcRoot, const std::u16string& dstRoot,
    const std::vector<TreeEntry>& entries, ProgressSink& sink)
{
    // One scratch buffer for every file in the tree instead of a per-file new/delete.
    auto buf   = std::make_unique<u8[]>(BUFFER_SIZE);
    Result res = 0;

    for (const auto& entry : entries) {
        if (sink.cancelled()) {
            break;
        }
        std::u16string dst = dstRoot + entry.rel;
        if (entry.folder) {
            res = io::createDirectory(dstArch, dst);
            // 0xC82044B9 == directory already exists; treat as success.
            if (R_FAILED(res) && (u32)res != 0xC82044B9) {
                return res;
            }
            res = 0;
        }
        else {
            res = io::copyFile(srcArch, dstArch, srcRoot + entry.rel, dst, sink, buf.get());
            if (R_FAILED(res)) {
                return res;
            }
        }
    }

    return res;
}

Result io::createDirectory(FS_Archive archive, const std::u16string& path)
{
    return FSUSER_CreateDirectory(archive, fsMakePath(PATH_UTF16, path.data()), 0);
}

bool io::directoryExists(FS_Archive archive, const std::u16string& path)
{
    Handle handle;

    if (R_FAILED(FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_UTF16, path.data())))) {
        return false;
    }

    if (R_FAILED(FSDIR_Close(handle))) {
        return false;
    }

    return true;
}

size_t io::countFilesRecursively(FS_Archive arch, const std::u16string& path)
{
    Directory dir(arch, path);
    if (!dir.good()) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            count += countFilesRecursively(arch, path + dir.entry(i) + StringUtils::UTF8toUTF16("/"));
        }
        else {
            count++;
        }
    }

    return count;
}

Result io::deleteFolderContentsRecursively(FS_Archive arch, const std::u16string& path, ProgressSink* sink)
{
    Directory dir(arch, path);
    if (!dir.good()) {
        return dir.error();
    }

    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            std::u16string newpath = path + dir.entry(i) + StringUtils::UTF8toUTF16("/");
            deleteFolderContentsRecursively(arch, newpath, sink);
            newpath = path + dir.entry(i);
            FSUSER_DeleteDirectory(arch, fsMakePath(PATH_UTF16, newpath.data()));
        }
        else {
            std::u16string newpath = path + dir.entry(i);
            FSUSER_DeleteFile(arch, fsMakePath(PATH_UTF16, newpath.data()));
            if (sink != nullptr) {
                sink->startFile(dir.entry(i), 0);
                sink->finishFile();
            }
        }
    }

    return 0;
}

Result io::deleteFolderRecursively(FS_Archive arch, const std::u16string& path, ProgressSink* sink)
{
    Result res = deleteFolderContentsRecursively(arch, path, sink);
    if (R_FAILED(res)) {
        return res;
    }
    FSUSER_DeleteDirectory(arch, fsMakePath(PATH_UTF16, path.data()));
    return 0;
}

io::IoOutcome io::backup(const BackupTarget& target, const std::u16string& dstPath, ProgressSink& sink)
{
    Title& title = target.title();
    Result res   = 0;

    Logging::info("Started backup of {}. Title id: 0x{:08X}.", title.shortDescription().c_str(), title.lowId());

    if (title.cardType() == CARD_CTR || title.isDSiWare()) {
        ArchiveHandle handle = target.open(res);
        if (R_FAILED(res)) {
            Logging::error("Failed to open save archive with result 0x{:08X}.", (u32)res);
            return {false, res, BackupStage::OpenArchive};
        }

        // Start from a clean destination folder.
        if (io::directoryExists(Archive::sdmc(), dstPath)) {
            res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            if (R_FAILED(res)) {
                Logging::error("Failed to delete the existing backup directory recursively with result 0x{:08X}.", (u32)res);
                return {false, res, BackupStage::DeleteDst};
            }
        }

        res = io::createDirectory(Archive::sdmc(), dstPath);
        if (R_FAILED(res)) {
            Logging::error("Failed to create destination directory.");
            return {false, res, BackupStage::CreateDst};
        }

        if (handle.isRaw()) {
            std::u16string savePath = dstPath + StringUtils::UTF8toUTF16("/00000001.sav");

            sink.begin("Backup", 1);
            res = GbaSave::backup(handle.pxi(), Archive::sdmc(), savePath, sink);
            sink.end();
            if (sink.cancelled()) {
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::info("Backup of {} cancelled by user.", title.shortDescription().c_str());
                return {false, 0, BackupStage::Copy, true};
            }
            if (R_FAILED(res)) {
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("Failed to backup GBA save. Result {}.", res);
                return {false, res, BackupStage::Copy};
            }
        }
        else {
            std::u16string copyPath = dstPath + StringUtils::UTF8toUTF16("/");

            // A DSiWare save is not its own archive: it's the title's data
            // directory inside the TWL NAND FAT, so the copy is rooted there.
            const std::u16string archiveRoot =
                title.isDSiWare() ? Archive::twlSaveDataPath(title.lowId(), title.highId()) : StringUtils::UTF8toUTF16("/");

            std::vector<io::TreeEntry> entries;
            res = io::collectTree(handle.fs(), archiveRoot, entries);
            if (R_FAILED(res)) {
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("Failed to enumerate {} for backup. Result {}.", target.dataTypeName(), res);
                return {false, res, (u32)res == RES_PATH_TOO_LONG ? BackupStage::PathTooLong : BackupStage::Copy};
            }

            // The console-side tree is short; it is the SD destination — root plus
            // title folder plus the backup name the user just typed — that can push
            // a deep save over the limit. Say so now, not half a copy in.
            res = io::checkPathLengths(entries, archiveRoot, copyPath);
            if (R_FAILED(res)) {
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("Refusing to back up {}: the destination folder name makes a path FS cannot create.", target.dataTypeName());
                return {false, res, BackupStage::PathTooLong};
            }

            size_t fileCount = 0;
            for (const auto& e : entries) {
                if (!e.folder) {
                    fileCount++;
                }
            }

            sink.begin("Backup", fileCount);
            res = io::copyTree(handle.fs(), Archive::sdmc(), archiveRoot, copyPath, entries, sink);
            sink.end();
            if (sink.cancelled()) {
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::info("Backup of {} cancelled by user.", title.shortDescription().c_str());
                return {false, 0, BackupStage::Copy, true};
            }
            if (R_FAILED(res)) {
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("Failed to backup {}. Result {}.", target.dataTypeName(), res);
                return {false, res, BackupStage::Copy};
            }
        }

        TitleCatalog::get().refreshDirectories(title.id());
    }
    else {
        CardType cardType = title.SPICardType();
        u32 saveSize      = SPIGetCapacity(cardType);
        // NO_CHIP / an unreadable cart reports 0 capacity; guard the division below.
        if (saveSize == 0) {
            // A cart whose save is in on-cart NAND has no SPI chip to report a
            // capacity, so it lands here. Name that case: the generic message
            // blames the save archive for hardware 3DS mode simply can't address.
            const DSCard::NandSave nand = DSCard::probeNandSave(title.mediaType());
            if (nand.present) {
                DSCard::logNandSave(nand);
                Logging::error("Refusing to back up {}: its save is in on-cart NAND, unreachable from 3DS mode.", title.shortDescription().c_str());
                return {false, 0, BackupStage::CardNandSave};
            }
            Logging::error("SPI backup: card reports zero capacity ({}).", (int)cardType);
            return {false, res, BackupStage::OpenArchive};
        }
        u32 sectorSize = (saveSize < 0x10000) ? saveSize : 0x10000;

        // Start from a clean destination folder.
        if (io::directoryExists(Archive::sdmc(), dstPath)) {
            res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            if (R_FAILED(res)) {
                Logging::error("Failed to delete the existing backup directory recursively with result 0x{:08X}.", (u32)res);
                return {false, res, BackupStage::DeleteDst};
            }
        }

        res = io::createDirectory(Archive::sdmc(), dstPath);
        if (R_FAILED(res)) {
            Logging::error("Failed to create destination directory with result 0x{:08X}.", (u32)res);
            return {false, res, BackupStage::CreateDst};
        }

        std::u16string fileName = StringUtils::UTF8toUTF16(title.shortDescription().c_str()) + StringUtils::UTF8toUTF16(".sav");
        std::u16string copyPath = dstPath + StringUtils::UTF8toUTF16("/") + fileName;

        sink.begin("Backup", 1);
        sink.startFile(fileName, saveSize);

        u8* saveFile      = new u8[saveSize];
        const u32 sectors = saveSize / sectorSize;
        for (u32 i = 0; i < sectors; ++i) {
            if (sink.cancelled()) {
                delete[] saveFile;
                sink.end();
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::info("Backup of {} cancelled by user.", title.shortDescription().c_str());
                return {false, 0, BackupStage::Copy, true};
            }
            res = SPIReadSaveData(cardType, sectorSize * i, saveFile + sectorSize * i, sectorSize);
            if (R_FAILED(res)) {
                delete[] saveFile;
                sink.end();
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("Failed to read save data from SPI with result 0x{:08X}.", (u32)res);
                return {false, res, BackupStage::ReadSpi};
            }
            sink.advanceBytes(sectorSize * (i + 1));
        }

        FSStream stream(Archive::sdmc(), copyPath, FS_OPEN_WRITE, saveSize);
        if (stream.good()) {
            stream.write(saveFile, saveSize);
        }
        else {
            Result streamRes = stream.result();
            delete[] saveFile;
            stream.close();
            sink.end();
            FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            Logging::error("Failed to write save to the sd card with result 0x{:08X}.", (u32)streamRes);
            return {false, streamRes, BackupStage::WriteFile};
        }

        delete[] saveFile;
        stream.close();
        sink.finishFile();
        sink.end();
        TitleCatalog::get().refreshDirectories(title.id());
    }

    Logging::info("Backup succeeded.");
    return {true, 0, BackupStage::Copy};
}

io::IoOutcome io::restore(const BackupTarget& target, const std::u16string& srcPath, ProgressSink& sink)
{
    Title& title = target.title();
    Result res   = 0;

    Logging::info("Started restore of {}. Title id: 0x{:08X}.", title.shortDescription().c_str(), title.lowId());

    if (title.cardType() == CARD_CTR || title.isDSiWare()) {
        ArchiveHandle handle = target.open(res);
        if (R_FAILED(res)) {
            Logging::error("Failed to open save archive with result 0x{:08X}.", (u32)res);
            return {false, res, BackupStage::OpenArchive};
        }

        std::u16string fullSrc = srcPath + StringUtils::UTF8toUTF16("/");

        if (handle.isRaw()) {
            fullSrc = rawBackupFile(fullSrc);

            sink.begin("Restore", 1);
            res = GbaSave::restore(handle.pxi(), Archive::sdmc(), fullSrc, sink);
            sink.end();
            if (R_FAILED(res)) {
                Logging::error("Failed to restore GBA save. Result {}.", res);
                return {false, res, BackupStage::Copy};
            }
        }
        else {
            const bool isTwl       = title.isDSiWare();
            std::u16string dstPath = isTwl ? Archive::twlSaveDataPath(title.lowId(), title.highId()) : StringUtils::UTF8toUTF16("/");

            // Walk and vet the backup *before* touching what is on the console: a
            // source that can't be enumerated, or that holds a path FS will refuse,
            // must not cost the user the save that is still there.
            std::vector<io::TreeEntry> entries;
            res = io::collectTree(Archive::sdmc(), fullSrc, entries);
            if (R_FAILED(res)) {
                Logging::error("Failed to enumerate backup of {} for restore. Result {}.", target.dataTypeName(), res);
                return {false, res, (u32)res == RES_PATH_TOO_LONG ? BackupStage::PathTooLong : BackupStage::Copy};
            }

            res = io::checkPathLengths(entries, fullSrc, dstPath);
            if (R_FAILED(res)) {
                Logging::error("Refusing to restore {}: the backup holds a path FS cannot open.", target.dataTypeName());
                return {false, res, BackupStage::PathTooLong};
            }

            size_t fileCount = 0;
            for (const auto& e : entries) {
                if (!e.folder) {
                    fileCount++;
                }
            }

            if (isTwl) {
                // The TWL FAT `data` directory must survive; only its contents go.
                sink.begin("Clearing", countFilesRecursively(handle.fs(), dstPath));
                deleteFolderContentsRecursively(handle.fs(), dstPath, &sink);
                sink.end();
            }
            else if (target.kind() != BackupKind::Extdata) {
                // One syscall, nothing to report against.
                FSUSER_DeleteDirectoryRecursively(handle.fs(), fsMakePath(PATH_UTF16, dstPath.data()));
            }
            else {
                sink.begin("Clearing", countFilesRecursively(handle.fs(), dstPath));
                deleteFolderRecursively(handle.fs(), dstPath, &sink);
                sink.end();
            }

            sink.begin("Restore", fileCount);
            res = io::copyTree(Archive::sdmc(), handle.fs(), fullSrc, dstPath, entries, sink);
            sink.end();
            if (R_FAILED(res)) {
                Logging::error("Failed to restore {}. Result {}.", target.dataTypeName(), res);
                return {false, res, BackupStage::Copy};
            }

            // A TWL FAT write needs no commit and has no secure value.
            if (target.kind() == BackupKind::Save && !isTwl) {
                res = FSUSER_ControlArchive(handle.fs(), ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
                if (R_FAILED(res)) {
                    Logging::error("Failed to commit save data with result 0x{:08X}.", (u32)res);
                    return {false, res, BackupStage::Commit};
                }

                u8 out;
                u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (title.uniqueId() << 8);
                res             = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
                if (R_FAILED(res)) {
                    Logging::error("Failed to fix secure value with result 0x{:08X}.", (u32)res);
                    return {false, res, BackupStage::SecureValue};
                }
            }
        }
    }
    else {
        CardType cardType = title.SPICardType();
        u32 saveSize      = SPIGetCapacity(cardType);
        u32 pageSize      = SPIGetPageSize(cardType);
        // NO_CHIP / an unreadable cart reports 0 capacity or page size; guard the divisions below.
        if (saveSize == 0 || pageSize == 0) {
            // See the matching guard in io::backup: on-cart NAND saves have no SPI
            // chip, and writing one back needs card commands 3DS mode never issues.
            const DSCard::NandSave nand = DSCard::probeNandSave(title.mediaType());
            if (nand.present) {
                DSCard::logNandSave(nand);
                Logging::error("Refusing to restore {}: its save is in on-cart NAND, unreachable from 3DS mode.", title.shortDescription().c_str());
                return {false, 0, BackupStage::CardNandSave};
            }
            Logging::error("SPI restore: card reports zero capacity/page size ({}).", (int)cardType);
            return {false, res, BackupStage::OpenArchive};
        }

        std::u16string fileName = StringUtils::UTF8toUTF16(title.shortDescription().c_str()) + StringUtils::UTF8toUTF16(".sav");
        std::u16string fullSrc  = srcPath + StringUtils::UTF8toUTF16("/") + fileName;

        u8* saveFile = new u8[saveSize];
        FSStream stream(Archive::sdmc(), fullSrc, FS_OPEN_READ);

        if (stream.good()) {
            stream.read(saveFile, saveSize);
        }
        res = stream.result();
        stream.close();

        if (R_FAILED(res)) {
            delete[] saveFile;
            Logging::error("Failed to read save file backup with result 0x{:08X}.", (u32)res);
            return {false, res, BackupStage::ReadFile};
        }

        // The 8MB flash cart is write-protected until its vendor unlock runs.
        // Detection already unlocks it, but re-arm it here so a restore is never gated by protection
        // (idempotent and cheap: a handful of short 512KHz frames).
        if (cardType == FLASH_8MB) {
            SPIUnlock(cardType);
        }

        sink.begin("Restore", 1);
        sink.startFile(fileName, saveSize);

        const u32 pages = saveSize / pageSize;
        for (u32 i = 0; i < pages; ++i) {
            res = SPIWriteSaveData(cardType, pageSize * i, saveFile + pageSize * i, pageSize);
            if (R_FAILED(res)) {
                delete[] saveFile;
                sink.end();
                Logging::error("Failed to write save data to SPI with result 0x{:08X}.", (u32)res);
                return {false, res, BackupStage::WriteFile};
            }
            sink.advanceBytes(pageSize * (i + 1));
        }
        sink.finishFile();
        sink.end();

        delete[] saveFile;
    }

    Logging::info("Restore succeeded.");
    return {true, 0, BackupStage::Copy};
}

void io::deleteBackupFolder(const std::u16string& path)
{
    Result res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, path.data()));
    if (R_FAILED(res)) {
        Logging::info("Failed to delete backup folder with result 0x{:08X}.", (u32)res);
    }
}
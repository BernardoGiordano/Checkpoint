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
#include "loader.hpp"

// Synthetic failure Result for a short write (FSFILE_Write reported success but
// committed fewer bytes than requested — typically a full archive). Negative so
// R_FAILED() is true; the exact value is opaque, the BackupStage carries meaning.
static const Result RES_SHORT_WRITE = MAKERESULT(RL_PERMANENT, RS_INTERNAL, RM_FS, RD_TOO_LARGE);

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

size_t io::countFiles(FS_Archive arch, const std::u16string& path)
{
    size_t count = 0;
    Directory items(arch, path);
    if (!items.good()) {
        return 0;
    }
    for (size_t i = 0, sz = items.size(); i < sz; i++) {
        if (items.folder(i)) {
            std::u16string subdir = path + items.entry(i) + StringUtils::UTF8toUTF16("/");
            count += io::countFiles(arch, subdir);
        }
        else {
            count++;
        }
    }
    return count;
}

Result io::copyFile(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath, ProgressSink& sink)
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
    u8* buf    = new u8[size];
    do {
        u32 rd = input.read(buf, size);
        if (R_FAILED(input.result())) {
            res = input.result();
            Logging::error("Read failure during copy of {} with result 0x{:08X}.", StringUtils::UTF16toUTF8(srcPath), res);
            break;
        }
        if (rd == 0) {
            break;
        }
        u32 wt = output.write(buf, rd);
        if (R_FAILED(output.result())) {
            res = output.result();
            Logging::error("Write failure during copy of {} with result 0x{:08X}.", StringUtils::UTF16toUTF8(dstPath), res);
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
    delete[] buf;
    sink.finishFile();

    input.close();
    output.close();
    return res;
}

Result io::copyPxiSaveFile(FSPXI_Archive pxiArch, FS_Archive regularArch, const std::u16string& path, bool fromPxi, ProgressSink& sink)
{
    u32 size       = 0;
    FSStream input = fromPxi ? FSStream(pxiArch, FS_OPEN_READ) : FSStream(regularArch, path, FS_OPEN_READ);
    if (input.good()) {
        size = input.size() > BUFFER_SIZE ? BUFFER_SIZE : input.size();
    }
    else {
        Logging::error("Failed to open source {} during GBA save copy with result {}.",
            fromPxi ? std::string("GBA save") : StringUtils::UTF16toUTF8(path), input.result());
        return input.result();
    }

    FSStream output = fromPxi ? FSStream(regularArch, path, FS_OPEN_WRITE, input.size()) : FSStream(pxiArch, FS_OPEN_WRITE, input.size());
    if (!output.good()) {
        Logging::error("Failed to open destination {} during GBA save copy with result {}.",
            fromPxi ? StringUtils::UTF16toUTF8(path) : std::string("GBA save"), output.result());
        input.close();
        return output.result();
    }

    size_t slashpos = path.rfind(StringUtils::UTF8toUTF16("/"));
    sink.startFile(path.substr(slashpos + 1, path.length() - slashpos - 1), input.size());

    Result res = 0;
    u32 offset = 0;
    auto buf   = std::make_unique<u8[]>(size);
    do {
        u32 rd = input.read(buf.get(), size);
        if (R_FAILED(input.result())) {
            res = input.result();
            Logging::error("Read failure during GBA save copy with result 0x{:08X}.", res);
            break;
        }
        if (rd == 0) {
            break;
        }
        u32 wt = output.write(buf.get(), rd);
        if (R_FAILED(output.result())) {
            res = output.result();
            Logging::error("Write failure during GBA save copy with result 0x{:08X}.", res);
            break;
        }
        if (wt != rd) {
            res = RES_SHORT_WRITE;
            Logging::error("Short write during GBA save copy: wrote {} of {} bytes.", wt, rd);
            break;
        }
        offset += rd;
        sink.advanceBytes(offset);
    } while (!input.eof());
    sink.finishFile();

    input.close();
    output.close();

    return res;
}

Result io::copyDirectory(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath, ProgressSink& sink)
{
    Result res = 0;
    bool quit  = false;
    Directory items(srcArch, srcPath);

    if (!items.good()) {
        return items.error();
    }

    for (size_t i = 0, sz = items.size(); i < sz && !quit; i++) {
        std::u16string newsrc = srcPath + items.entry(i);
        std::u16string newdst = dstPath + items.entry(i);

        if (items.folder(i)) {
            res = io::createDirectory(dstArch, newdst);
            if (R_SUCCEEDED(res) || (u32)res == 0xC82044B9) {
                newsrc += StringUtils::UTF8toUTF16("/");
                newdst += StringUtils::UTF8toUTF16("/");
                res = io::copyDirectory(srcArch, dstArch, newsrc, newdst, sink);
            }
            else {
                quit = true;
            }
        }
        else {
            res = io::copyFile(srcArch, dstArch, newsrc, newdst, sink);
            if (R_FAILED(res)) {
                quit = true;
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

Result io::deleteFolderRecursively(FS_Archive arch, const std::u16string& path)
{
    Directory dir(arch, path);
    if (!dir.good()) {
        return dir.error();
    }

    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            std::u16string newpath = path + dir.entry(i) + StringUtils::UTF8toUTF16("/");
            deleteFolderRecursively(arch, newpath);
            newpath = path + dir.entry(i);
            FSUSER_DeleteDirectory(arch, fsMakePath(PATH_UTF16, newpath.data()));
        }
        else {
            std::u16string newpath = path + dir.entry(i);
            FSUSER_DeleteFile(arch, fsMakePath(PATH_UTF16, newpath.data()));
        }
    }

    FSUSER_DeleteDirectory(arch, fsMakePath(PATH_UTF16, path.data()));
    return 0;
}

io::IoOutcome io::backup(const BackupTarget& target, const std::u16string& dstPath, ProgressSink& sink)
{
    Title& title = target.title();
    Result res   = 0;

    Logging::info("Started backup of {}. Title id: 0x{:08X}.", title.shortDescription().c_str(), title.lowId());

    if (title.cardType() == CARD_CTR) {
        ArchiveHandle handle = target.open(res);
        if (R_FAILED(res)) {
            Logging::error("Failed to open save archive with result 0x{:08X}.", res);
            return {false, res, BackupStage::OpenArchive};
        }

        // Start from a clean destination folder.
        if (io::directoryExists(Archive::sdmc(), dstPath)) {
            res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            if (R_FAILED(res)) {
                Logging::error("Failed to delete the existing backup directory recursively with result 0x{:08X}.", res);
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
            res = io::copyPxiSaveFile(handle.pxi(), Archive::sdmc(), savePath, true, sink);
            sink.end();
            if (R_FAILED(res)) {
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("Failed to backup GBA save. Result {}.", res);
                return {false, res, BackupStage::Copy};
            }
        }
        else {
            std::u16string copyPath = dstPath + StringUtils::UTF8toUTF16("/");

            sink.begin("Backup", io::countFiles(handle.fs(), StringUtils::UTF8toUTF16("/")));
            res = io::copyDirectory(handle.fs(), Archive::sdmc(), StringUtils::UTF8toUTF16("/"), copyPath, sink);
            sink.end();
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
        u32 sectorSize    = (saveSize < 0x10000) ? saveSize : 0x10000;

        // Start from a clean destination folder.
        if (io::directoryExists(Archive::sdmc(), dstPath)) {
            res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            if (R_FAILED(res)) {
                Logging::error("Failed to delete the existing backup directory recursively with result 0x{:08X}.", res);
                return {false, res, BackupStage::DeleteDst};
            }
        }

        res = io::createDirectory(Archive::sdmc(), dstPath);
        if (R_FAILED(res)) {
            Logging::error("Failed to create destination directory with result 0x{:08X}.", res);
            return {false, res, BackupStage::CreateDst};
        }

        std::u16string fileName = StringUtils::UTF8toUTF16(title.shortDescription().c_str()) + StringUtils::UTF8toUTF16(".sav");
        std::u16string copyPath = dstPath + StringUtils::UTF8toUTF16("/") + fileName;

        sink.begin("Backup", 1);
        sink.startFile(fileName, saveSize);

        u8* saveFile      = new u8[saveSize];
        const u32 sectors = saveSize / sectorSize;
        for (u32 i = 0; i < sectors; ++i) {
            res = SPIReadSaveData(cardType, sectorSize * i, saveFile + sectorSize * i, sectorSize);
            if (R_FAILED(res)) {
                delete[] saveFile;
                sink.end();
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("Failed to read save data from SPI with result 0x{:08X}.", res);
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
            Logging::error("Failed to write save to the sd card with result 0x{:08X}.", streamRes);
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

    if (title.cardType() == CARD_CTR) {
        ArchiveHandle handle = target.open(res);
        if (R_FAILED(res)) {
            Logging::error("Failed to open save archive with result 0x{:08X}.", res);
            return {false, res, BackupStage::OpenArchive};
        }

        std::u16string fullSrc = srcPath + StringUtils::UTF8toUTF16("/");

        if (handle.isRaw()) {
            fullSrc += StringUtils::UTF8toUTF16("00000001.sav");

            sink.begin("Restore", 1);
            res = io::copyPxiSaveFile(handle.pxi(), Archive::sdmc(), fullSrc, false, sink);
            sink.end();
            if (R_FAILED(res)) {
                Logging::error("Failed to restore GBA save. Result {}.", res);
                return {false, res, BackupStage::Copy};
            }
        }
        else {
            std::u16string dstPath = StringUtils::UTF8toUTF16("/");

            if (target.kind() != BackupKind::Extdata) {
                FSUSER_DeleteDirectoryRecursively(handle.fs(), fsMakePath(PATH_UTF16, dstPath.data()));
            }
            else {
                deleteFolderRecursively(handle.fs(), dstPath);
            }

            sink.begin("Restore", io::countFiles(Archive::sdmc(), fullSrc));
            res = io::copyDirectory(Archive::sdmc(), handle.fs(), fullSrc, dstPath, sink);
            sink.end();
            if (R_FAILED(res)) {
                Logging::error("Failed to restore {}. Result {}.", target.dataTypeName(), res);
                return {false, res, BackupStage::Copy};
            }

            if (target.kind() == BackupKind::Save) {
                res = FSUSER_ControlArchive(handle.fs(), ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
                if (R_FAILED(res)) {
                    Logging::error("Failed to commit save data with result 0x{:08X}.", res);
                    return {false, res, BackupStage::Commit};
                }

                u8 out;
                u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (title.uniqueId() << 8);
                res             = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
                if (R_FAILED(res)) {
                    Logging::error("Failed to fix secure value with result 0x{:08X}.", res);
                    return {false, res, BackupStage::SecureValue};
                }
            }
        }
    }
    else {
        CardType cardType = title.SPICardType();
        u32 saveSize      = SPIGetCapacity(cardType);
        u32 pageSize      = SPIGetPageSize(cardType);

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
            Logging::error("Failed to read save file backup with result 0x{:08X}.", res);
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
                Logging::error("Failed to write save data to SPI with result 0x{:08X}.", res);
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
        Logging::info("Failed to delete backup folder with result 0x{:08X}.", res);
    }
}
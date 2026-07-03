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
#include "savedatasource.hpp"
#include "titlecatalog.hpp"

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

void io::copyFile(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink)
{
    FILE* src = fopen(srcPath.c_str(), "rb");
    if (src == NULL) {
        Logging::error("Failed to open source file {} during copy with errno {}. Skipping...", srcPath, errno);
        return;
    }
    FILE* dst = fopen(dstPath.c_str(), "wb");
    if (dst == NULL) {
        Logging::error("Failed to open destination file {} during copy with errno {}. Skipping...", dstPath, errno);
        fclose(src);
        return;
    }

    fseek(src, 0, SEEK_END);
    u64 sz = ftell(src);
    rewind(src);

    u8* buf    = new u8[BUFFER_SIZE];
    u64 offset = 0;

    size_t slashpos = srcPath.rfind("/");
    sink.startFile(srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1), sz);

    while (offset < sz) {
        if (sink.cancelled()) {
            break;
        }

        u32 count = fread((char*)buf, 1, BUFFER_SIZE, src);
        if (count == 0) {
            Logging::error("fread returned 0 for file {} at offset {}/{} with errno {}. Aborting copy.", srcPath, offset, sz, errno);
            break;
        }
        fwrite((char*)buf, 1, count, dst);
        offset += count;
        sink.advanceBytes(offset);
    }

    delete[] buf;
    fclose(src);
    fclose(dst);
    sink.finishFile();

    // commit each file to the save
    if (dstPath.rfind("save:/", 0) == 0) {
        Logging::error("Committing file {} to the save archive.", dstPath);
        fsdevCommitDevice("save");
    }
}

Result io::copyDirectory(const std::string& srcPath, const std::string& dstPath, ProgressSink& sink)
{
    Result res = 0;
    bool quit  = false;
    Directory items(srcPath);

    if (!items.good()) {
        return items.error();
    }

    for (size_t i = 0, sz = items.size(); i < sz && !quit; i++) {
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
                res = io::copyDirectory(newsrc, newdst, sink);
            }
            else {
                quit = true;
            }
        }
        else {
            io::copyFile(newsrc, newdst, sink);
        }
    }

    return 0;
}

Result io::createDirectory(const std::string& path)
{
    mkdir(path.c_str(), 777);
    return 0;
}

bool io::directoryExists(const std::string& path)
{
    struct stat sb;
    return (stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
}

Result io::deleteFolderRecursively(const std::string& path)
{
    Directory dir(path);
    if (!dir.good()) {
        return dir.error();
    }

    for (size_t i = 0, sz = dir.size(); i < sz; i++) {
        if (dir.folder(i)) {
            std::string newpath = path + dir.entry(i) + "/";
            deleteFolderRecursively(newpath);
            newpath = path + dir.entry(i);
            rmdir(newpath.c_str());
        }
        else {
            std::string newpath = path + dir.entry(i);
            std::remove(newpath.c_str());
        }
    }

    rmdir(path.c_str());
    return 0;
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

    io::createDirectory(dstPath);
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
    Logging::info("Started restore of {}. Title id: 0x{:016X}; User id: 0x{:X}{:X}.", title.name().c_str(), title.id(), title.userId().uid[1],
        title.userId().uid[0]);

    Result res = SaveDataSource(title.saveDataType()).mount(title);
    if (R_FAILED(res)) {
        Logging::error("Failed to mount filesystem during restore with result 0x{:08X}. Title id: 0x{:016X}.", res, title.id());
        return {false, res, io::BackupStage::OpenArchive};
    }

    std::string dstPath = "save:/";

    res = io::deleteFolderRecursively(dstPath.c_str());
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to recursively delete directory {} with result 0x{:08X}.", dstPath, res);
        return {false, res, io::BackupStage::DeleteDst};
    }

    sink.begin("Restore", io::countFiles(srcPath));
    res = io::copyDirectory(srcPath, dstPath, sink);
    sink.end();
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to copy directory {} to {} with result 0x{:08X}. Skipping...", srcPath, dstPath, res);
        return {false, res, io::BackupStage::Copy};
    }

    res = fsdevCommitDevice("save");
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to commit save with result 0x{:08X}.", res);
        return {false, res, io::BackupStage::Commit};
    }

    FileSystem::unmountDevice();
    Logging::info("Restore succeeded.");
    return {true, 0, io::BackupStage::Copy};
}
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

#include "io.hpp"

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

void io::copyFile(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath)
{
    g_isTransferringFile = true;

    u32 size = 0;
    FSStream input(srcArch, srcPath, FS_OPEN_READ);
    if (input.good()) {
        size = input.size() > BUFFER_SIZE ? BUFFER_SIZE : input.size();
    }
    else {
        Logging::error("Failed to open source file {} during copy with result {}. Skipping...", StringUtils::UTF16toUTF8(srcPath), input.result());
        return;
    }

    FSStream output(dstArch, dstPath, FS_OPEN_WRITE, input.size());
    if (output.good()) {
        size_t slashpos = srcPath.rfind(StringUtils::UTF8toUTF16("/"));
        g_currentFile   = srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1);

        u32 rd;
        u8* buf = new u8[size];
        do {
            rd = input.read(buf, size);
            output.write(buf, rd);

            // avoid freezing the UI
            // this will be made less horrible next time...
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            g_screen->drawTop();
            C2D_SceneBegin(g_bottom);
            g_screen->drawBottom();
            Gui::frameEnd();
        } while (!input.eof());
        delete[] buf;
    }
    else {
        Logging::error(
            "Failed to open destination file {} during copy with result {}. Skipping...", StringUtils::UTF16toUTF8(dstPath), output.result());
    }

    input.close();
    output.close();

    g_isTransferringFile = false;
}

Result io::copyDirectory(FS_Archive srcArch, FS_Archive dstArch, const std::u16string& srcPath, const std::u16string& dstPath)
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
                res = io::copyDirectory(srcArch, dstArch, newsrc, newdst);
            }
            else {
                quit = true;
            }
        }
        else {
            io::copyFile(srcArch, dstArch, newsrc, newdst);
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
            std::u16string newpath = path + StringUtils::UTF8toUTF16("/") + dir.entry(i) + StringUtils::UTF8toUTF16("/");
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

std::tuple<bool, Result, std::string> io::backup(size_t index, size_t cellIndex)
{
    const Mode_t mode      = Archive::mode();
    const bool isNewFolder = cellIndex == 0;
    Result res             = 0;

    Title title;
    getTitle(title, index);

    Logging::info("Started backup of %s. Title id: 0x%08lX.", title.shortDescription().c_str(), title.lowId());

    if (title.cardType() == CARD_CTR) {
        FS_Archive archive;
        if (mode == MODE_SAVE) {
            res = Archive::save(&archive, title.mediaType(), title.lowId(), title.highId());
        }
        else if (mode == MODE_EXTDATA) {
            res = Archive::extdata(&archive, title.extdataId());
        }

        if (R_SUCCEEDED(res)) {
            std::string suggestion = DateTime::dateTimeStr();

            std::u16string customPath;
            if (MS::multipleSelectionEnabled()) {
                customPath = isNewFolder ? StringUtils::UTF8toUTF16(suggestion.c_str()) : StringUtils::UTF8toUTF16("");
            }
            else {
                customPath = isNewFolder ? KeyboardManager::get().keyboard(suggestion) : StringUtils::UTF8toUTF16("");
            }

            std::u16string dstPath;
            if (!isNewFolder) {
                // we're overriding an existing folder
                dstPath = mode == MODE_SAVE ? title.fullSavePath(cellIndex) : title.fullExtdataPath(cellIndex);
            }
            else {
                dstPath = mode == MODE_SAVE ? title.savePath() : title.extdataPath();
                dstPath += StringUtils::UTF8toUTF16("/") + customPath;
            }

            if (!isNewFolder || io::directoryExists(Archive::sdmc(), dstPath)) {
                res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                if (R_FAILED(res)) {
                    FSUSER_CloseArchive(archive);
                    Logging::error("Failed to delete the existing backup directory recursively with result 0x%08lX.", res);
                    return std::make_tuple(false, res, "Failed to delete the existing backup\ndirectory recursively.");
                }
            }

            res = io::createDirectory(Archive::sdmc(), dstPath);
            if (R_FAILED(res)) {
                FSUSER_CloseArchive(archive);
                Logging::error("Failed to create destination directory.");
                return std::make_tuple(false, res, "Failed to create destination directory.");
            }

            std::u16string copyPath = dstPath + StringUtils::UTF8toUTF16("/");

            res = io::copyDirectory(archive, Archive::sdmc(), StringUtils::UTF8toUTF16("/"), copyPath);
            if (R_FAILED(res)) {
                std::string message = mode == MODE_SAVE ? "Failed to backup save." : "Failed to backup extdata.";
                FSUSER_CloseArchive(archive);
                FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
                Logging::error("{} Result {}.", message, res);
                return std::make_tuple(false, res, message);
            }

            refreshDirectories(title.id());
        }
        else {
            Logging::error("Failed to open save archive with result 0x%08lX.", res);
            return std::make_tuple(false, res, "Failed to open save archive.");
        }

        FSUSER_CloseArchive(archive);
    }
    else {
        CardType cardType = title.SPICardType();
        u32 saveSize      = SPIGetCapacity(cardType);
        u32 sectorSize    = (saveSize < 0x10000) ? saveSize : 0x10000;

        std::string suggestion = DateTime::dateTimeStr();

        std::u16string customPath;
        if (MS::multipleSelectionEnabled()) {
            customPath = isNewFolder ? StringUtils::UTF8toUTF16(suggestion.c_str()) : StringUtils::UTF8toUTF16("");
        }
        else {
            customPath = isNewFolder ? KeyboardManager::get().keyboard(suggestion) : StringUtils::UTF8toUTF16("");
        }

        std::u16string dstPath;
        if (!isNewFolder) {
            // we're overriding an existing folder
            dstPath = mode == MODE_SAVE ? title.fullSavePath(cellIndex) : title.fullExtdataPath(cellIndex);
        }
        else {
            dstPath = mode == MODE_SAVE ? title.savePath() : title.extdataPath();
            dstPath += StringUtils::UTF8toUTF16("/") + customPath;
        }

        if (!isNewFolder || io::directoryExists(Archive::sdmc(), dstPath)) {
            res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            if (R_FAILED(res)) {
                Logging::error("Failed to delete the existing backup directory recursively with result 0x%08lX.", res);
                return std::make_tuple(false, res, "Failed to delete the existing\nbackup directory recursively.");
            }
        }

        res = io::createDirectory(Archive::sdmc(), dstPath);
        if (R_FAILED(res)) {
            Logging::error("Failed to create destination directory with result 0x%08lX.", res);
            return std::make_tuple(false, res, "Failed to create destination directory.");
        }

        std::u16string copyPath =
            dstPath + StringUtils::UTF8toUTF16("/") + StringUtils::UTF8toUTF16(title.shortDescription().c_str()) + StringUtils::UTF8toUTF16(".sav");

        u8* saveFile = new u8[saveSize];
        for (u32 i = 0; i < saveSize / sectorSize; ++i) {
            res = SPIReadSaveData(cardType, sectorSize * i, saveFile + sectorSize * i, sectorSize);
            if (R_FAILED(res)) {
                break;
            }
        }

        if (R_FAILED(res)) {
            delete[] saveFile;
            FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            Logging::error("Failed to delete directory recursively after failing to write save to the sd card with result {}.", res);
            return std::make_tuple(false, res, "Failed to backup save.");
        }

        FSStream stream(Archive::sdmc(), copyPath, FS_OPEN_WRITE, saveSize);
        if (stream.good()) {
            stream.write(saveFile, saveSize);
        }
        else {
            delete[] saveFile;
            stream.close();
            FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, dstPath.data()));
            Logging::error("Failed to delete directory recursively after failing to write save to the sd card with result {}.", res);
            return std::make_tuple(false, res, "Failed to backup save.");
        }

        delete[] saveFile;
        stream.close();
        refreshDirectories(title.id());
    }

    Logging::info("Backup succeeded.");
    return std::make_tuple(true, 0, "Progress correctly saved to disk.");
}

std::tuple<bool, Result, std::string> io::restore(size_t index, size_t cellIndex, const std::string& nameFromCell)
{
    const Mode_t mode = Archive::mode();
    Result res        = 0;

    Title title;
    getTitle(title, index);

    Logging::info("Started restore of %s. Title id: 0x%08lX.", title.shortDescription().c_str(), title.lowId());

    if (title.cardType() == CARD_CTR) {
        FS_Archive archive;
        if (mode == MODE_SAVE) {
            res = Archive::save(&archive, title.mediaType(), title.lowId(), title.highId());
        }
        else if (mode == MODE_EXTDATA) {
            res = Archive::extdata(&archive, title.extdataId());
        }

        if (R_SUCCEEDED(res)) {
            std::u16string srcPath = mode == MODE_SAVE ? title.fullSavePath(cellIndex) : title.fullExtdataPath(cellIndex);
            srcPath += StringUtils::UTF8toUTF16("/");
            std::u16string dstPath = StringUtils::UTF8toUTF16("/");

            if (mode != MODE_EXTDATA) {
                FSUSER_DeleteDirectoryRecursively(archive, fsMakePath(PATH_UTF16, dstPath.data()));
            }
            else {
                deleteFolderRecursively(archive, dstPath);
            }

            res = io::copyDirectory(Archive::sdmc(), archive, srcPath, dstPath);
            if (R_FAILED(res)) {
                std::string message = mode == MODE_SAVE ? "Failed to restore save." : "Failed to restore extdata.";
                FSUSER_CloseArchive(archive);
                Logging::error("{}. Result {}.", message, res);
                return std::make_tuple(false, res, message);
            }

            if (mode == MODE_SAVE) {
                res = FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
                if (R_FAILED(res)) {
                    FSUSER_CloseArchive(archive);
                    Logging::error("Failed to commit save data with result 0x%08lX.", res);
                    return std::make_tuple(false, res, "Failed to commit save data.");
                }

                u8 out;
                u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (title.uniqueId() << 8);
                res             = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
                if (R_FAILED(res)) {
                    FSUSER_CloseArchive(archive);
                    Logging::error("Failed to fix secure value with result 0x%08lX.", res);
                    return std::make_tuple(false, res, "Failed to fix secure value.");
                }
            }
        }
        else {
            Logging::error("Failed to open save archive with result 0x%08lX.", res);
            return std::make_tuple(false, res, "Failed to open save archive.");
        }

        FSUSER_CloseArchive(archive);
    }
    else {
        CardType cardType = title.SPICardType();
        u32 saveSize      = SPIGetCapacity(cardType);
        u32 pageSize      = SPIGetPageSize(cardType);

        std::u16string srcPath = title.fullSavePath(cellIndex);
        srcPath += StringUtils::UTF8toUTF16("/") + StringUtils::UTF8toUTF16(title.shortDescription().c_str()) + StringUtils::UTF8toUTF16(".sav");

        u8* saveFile = new u8[saveSize];
        FSStream stream(Archive::sdmc(), srcPath, FS_OPEN_READ);

        if (stream.good()) {
            stream.read(saveFile, saveSize);
        }
        res = stream.result();
        stream.close();

        if (R_FAILED(res)) {
            delete[] saveFile;
            Logging::error("Failed to read save file backup with result 0x%08lX.", res);
            return std::make_tuple(false, res, "Failed to read save file backup.");
        }

        for (u32 i = 0; i < saveSize / pageSize; ++i) {
            res = SPIWriteSaveData(cardType, pageSize * i, saveFile + pageSize * i, pageSize);
            if (R_FAILED(res)) {
                break;
            }
        }

        if (R_FAILED(res)) {
            delete[] saveFile;
            Logging::error("Failed to restore save with result 0x%08lX.", res);
            return std::make_tuple(false, res, "Failed to restore save.");
        }

        delete[] saveFile;
    }

    Logging::info("Restore succeeded.");
    return std::make_tuple(true, 0, nameFromCell + "\nhas been restored successfully.");
}

void io::deleteBackupFolder(const std::u16string& path)
{
    Result res = FSUSER_DeleteDirectoryRecursively(Archive::sdmc(), fsMakePath(PATH_UTF16, path.data()));
    if (R_FAILED(res)) {
        Logging::info("Failed to delete backup folder with result 0x%08lX.", res);
    }
}
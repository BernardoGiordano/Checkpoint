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

void io::copyFile(const std::string& srcPath, const std::string& dstPath)
{
    g_isTransferringFile = true;

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

    size_t slashpos     = srcPath.rfind("/");
    g_currentFile       = srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1);
    g_currentFileSize   = sz;
    g_currentFileOffset = 0;

    while (offset < sz) {
        u32 count = fread((char*)buf, 1, BUFFER_SIZE, src);
        if (count == 0) {
            Logging::error("fread returned 0 for file {} at offset {}/{} with errno {}. Aborting copy.", srcPath, offset, sz, errno);
            break;
        }
        fwrite((char*)buf, 1, count, dst);
        offset += count;
        g_currentFileOffset = offset;

        // avoid freezing the UI
        // this will be made less horrible next time...
        g_screen->draw();
        SDLH_Render();
    }

    delete[] buf;
    fclose(src);
    fclose(dst);
    g_copyCount++;

    // commit each file to the save
    if (dstPath.rfind("save:/", 0) == 0) {
        Logging::error("Committing file {} to the save archive.", dstPath);
        fsdevCommitDevice("save");
    }

    g_isTransferringFile = false;
}

Result io::copyDirectory(const std::string& srcPath, const std::string& dstPath)
{
    Result res = 0;
    bool quit  = false;
    Directory items(srcPath);

    if (!items.good()) {
        return items.error();
    }

    for (size_t i = 0, sz = items.size(); i < sz && !quit; i++) {
        std::string newsrc = srcPath + items.entry(i);
        std::string newdst = dstPath + items.entry(i);

        if (items.folder(i)) {
            res = io::createDirectory(newdst);
            if (R_SUCCEEDED(res)) {
                newsrc += "/";
                newdst += "/";
                res = io::copyDirectory(newsrc, newdst);
            }
            else {
                quit = true;
            }
        }
        else {
            io::copyFile(newsrc, newdst);
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

std::tuple<bool, Result, std::string> io::backup(size_t index, AccountUid uid, size_t cellIndex)
{
    const bool isNewFolder                    = cellIndex == 0;
    Result res                                = 0;
    std::tuple<bool, Result, std::string> ret = std::make_tuple(false, -1, "");
    Title title;
    getTitle(title, uid, index);

    Logging::info("Started backup of {}. Title id: 0x{:016X}; User id: 0x{:X}{:X}.", title.name().c_str(), title.id(), title.userId().uid[1],
        title.userId().uid[0]);

    FsFileSystem fileSystem;
    if (title.saveDataType() == FsSaveDataType_Bcat) {
        res = FileSystem::mountBcatSave(&fileSystem, title.id());
    }
    else if (title.saveDataType() == FsSaveDataType_Device) {
        res = FileSystem::mountDeviceSave(&fileSystem, title.id());
    }
    else if (title.saveDataType() == FsSaveDataType_System) {
        res = FileSystem::mountSystemSave(&fileSystem, title.id(), title.saveDataSpaceId());
    }
    else {
        res = FileSystem::mountSave(&fileSystem, title.id(), title.userId());
    }
    if (R_SUCCEEDED(res)) {
        int rc = FileSystem::mountDevice(fileSystem);
        if (rc == -1) {
            FileSystem::unmountDevice();
            Logging::error("Failed to mount filesystem during backup. Title id: 0x{:016X}.", title.id());
            return std::make_tuple(false, -2, "Failed to mount save.");
        }
    }
    else {
        Logging::error("Failed to mount filesystem during backup with result 0x{:08X}. Title id: 0x{:016X}.", res, title.id());
        return std::make_tuple(false, res, "Failed to mount save.");
    }

    std::string suggestion;
    if (title.saveDataType() == FsSaveDataType_Bcat || title.saveDataType() == FsSaveDataType_Device ||
        title.saveDataType() == FsSaveDataType_System) {
        suggestion = DateTime::dateTimeStr();
    }
    else {
        suggestion = DateTime::dateTimeStr() + " " +
                     (StringUtils::containsInvalidChar(Account::username(title.userId()))
                             ? ""
                             : StringUtils::removeNotAscii(StringUtils::removeAccents(Account::username(title.userId()))));
    }
    std::string customPath;

    if (MS::multipleSelectionEnabled()) {
        customPath = isNewFolder ? suggestion : "";
    }
    else {
        if (isNewFolder) {
            if (KeyboardManager::get().isSystemKeyboardAvailable().first) {
                std::pair<bool, std::string> keyboardResponse = KeyboardManager::get().keyboard(suggestion);
                if (keyboardResponse.first) {
                    customPath = StringUtils::removeForbiddenCharacters(keyboardResponse.second);
                }
                else {
                    FileSystem::unmountDevice();
                    Logging::info("Copy operation aborted by the user through the system keyboard.");
                    return std::make_tuple(false, 0, "Operation aborted by the user.");
                }
            }
            else {
                customPath = suggestion;
            }
        }
        else {
            customPath = "";
        }
    }

    std::string dstPath;
    if (!isNewFolder) {
        // we're overriding an existing folder
        dstPath = title.fullPath(cellIndex);
    }
    else {
        dstPath = title.path() + "/" + customPath;
    }

    if (!isNewFolder || io::directoryExists(dstPath)) {
        int rc = io::deleteFolderRecursively((dstPath + "/").c_str());
        if (rc != 0) {
            FileSystem::unmountDevice();
            Logging::error("Failed to recursively delete directory {} with result {}.", dstPath, rc);
            return std::make_tuple(false, (Result)rc, "Failed to delete the existing backup\ndirectory recursively.");
        }
    }

    io::createDirectory(dstPath);
    g_copyCount    = 0;
    g_copyTotal    = io::countFiles("save:/");
    g_transferMode = "Backup";
    res            = io::copyDirectory("save:/", dstPath + "/");
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        io::deleteFolderRecursively((dstPath + "/").c_str());
        Logging::error("Failed to copy directory {} with result 0x{:08X}. Skipping...", dstPath, res);
        return std::make_tuple(false, res, "Failed to backup save.");
    }

    refreshDirectories(title.id());

    FileSystem::unmountDevice();
    if (!MS::multipleSelectionEnabled()) {
        blinkLed(4);
        ret = std::make_tuple(true, 0, "Progress correctly saved to disk.");
    }

    auto systemKeyboardAvailable = KeyboardManager::get().isSystemKeyboardAvailable();
    if (!systemKeyboardAvailable.first) {
        return std::make_tuple(true, systemKeyboardAvailable.second,
            "Progress correctly saved to disk.\nSystem keyboard applet was not\naccessible. The suggested destination\nfolder was used instead.");
    }

    ret = std::make_tuple(true, 0, "Progress correctly saved to disk.");
    Logging::info("Backup succeeded.");
    return ret;
}

std::tuple<bool, Result, std::string> io::restore(size_t index, AccountUid uid, size_t cellIndex, const std::string& nameFromCell)
{
    Result res                                = 0;
    std::tuple<bool, Result, std::string> ret = std::make_tuple(false, -1, "");
    Title title;
    getTitle(title, uid, index);

    Logging::info("Started restore of {}. Title id: 0x{:016X}; User id: 0x{:X}{:X}.", title.name().c_str(), title.id(), title.userId().uid[1],
        title.userId().uid[0]);

    FsFileSystem fileSystem;
    if (title.saveDataType() == FsSaveDataType_Bcat) {
        res = FileSystem::mountBcatSave(&fileSystem, title.id());
    }
    else if (title.saveDataType() == FsSaveDataType_Device) {
        res = FileSystem::mountDeviceSave(&fileSystem, title.id());
    }
    else if (title.saveDataType() == FsSaveDataType_System) {
        res = FileSystem::mountSystemSave(&fileSystem, title.id(), title.saveDataSpaceId());
    }
    else {
        res = FileSystem::mountSave(&fileSystem, title.id(), title.userId());
    }
    if (R_SUCCEEDED(res)) {
        int rc = FileSystem::mountDevice(fileSystem);
        if (rc == -1) {
            FileSystem::unmountDevice();
            Logging::error("Failed to mount filesystem during restore. Title id: 0x{:016X}.", title.id());
            return std::make_tuple(false, -2, "Failed to mount save.");
        }
    }
    else {
        Logging::error("Failed to mount filesystem during restore with result 0x{:08X}. Title id: 0x{:016X}.", res, title.id());
        return std::make_tuple(false, res, "Failed to mount save.");
    }

    std::string srcPath = title.fullPath(cellIndex) + "/";
    std::string dstPath = "save:/";

    res = io::deleteFolderRecursively(dstPath.c_str());
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to recursively delete directory {} with result 0x{:08X}.", dstPath, res);
        return std::make_tuple(false, res, "Failed to delete save.");
    }

    g_copyCount    = 0;
    g_copyTotal    = io::countFiles(srcPath);
    g_transferMode = "Restore";
    res            = io::copyDirectory(srcPath, dstPath);
    if (R_FAILED(res)) {
        FileSystem::unmountDevice();
        Logging::error("Failed to copy directory {} to {} with result 0x{:08X}. Skipping...", srcPath, dstPath, res);
        return std::make_tuple(false, res, "Failed to restore save.");
    }

    res = fsdevCommitDevice("save");
    if (R_FAILED(res)) {
        Logging::error("Failed to commit save with result 0x{:08X}.", res);
        return std::make_tuple(false, res, "Failed to commit to save device.");
    }
    else {
        blinkLed(4);
        ret = std::make_tuple(true, 0, nameFromCell + "\nhas been restored successfully.");
    }

    FileSystem::unmountDevice();

    Logging::info("Restore succeeded.");
    return ret;
}
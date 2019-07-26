/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

void io::copyFile(const std::string& srcPath, const std::string& dstPath)
{
    FILE* src = fopen(srcPath.c_str(), "rb");
    if (src == NULL) {
        Logger::getInstance().log(Logger::ERROR, "Failed to open source file " + srcPath + " during copy with errno %d. Skipping...", errno);
        return;
    }
    FILE* dst = fopen(dstPath.c_str(), "wb");
    if (dst == NULL) {
        Logger::getInstance().log(Logger::ERROR, "Failed to open destination file " + dstPath + " during copy with errno %d. Skipping...", errno);
        fclose(src);
        return;
    }

    fseek(src, 0, SEEK_END);
    u64 sz = ftell(src);
    rewind(src);

    u8* buf    = new u8[BUFFER_SIZE];
    u64 offset = 0;
    while (offset < sz) {
        u32 count = fread((char*)buf, 1, BUFFER_SIZE, src);
        fwrite((char*)buf, 1, count, dst);
        offset += count;
    }

    delete[] buf;
    fclose(src);
    fclose(dst);

    // commit each file to the save
    if (dstPath.rfind("save:/", 0) == 0) {
        Logger::getInstance().log(Logger::ERROR, "Committing file " + dstPath + " to the save archive.");
        fsdevCommitDevice("save");
    }
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
            std::string newpath = path + "/" + dir.entry(i) + "/";
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

std::tuple<bool, Result, std::string> io::backup(size_t index, u128 uid, size_t cellIndex)
{
    const bool isNewFolder                    = cellIndex == 0;
    Result res                                = 0;
    std::tuple<bool, Result, std::string> ret = std::make_tuple(false, -1, "");
    Title title;
    getTitle(title, uid, index);

    Logger::getInstance().log(Logger::INFO, "Started backup of %s. Title id: 0x%016lX; User id: 0x%lX%lX.", title.name().c_str(), title.id(),
        (u64)(title.userId() >> 8), (u64)(title.userId()));

    FsFileSystem fileSystem;
    res = FileSystem::mount(&fileSystem, title.id(), title.userId());
    if (R_SUCCEEDED(res)) {
        int rc = FileSystem::mount(fileSystem);
        if (rc == -1) {
            FileSystem::unmount();
            Logger::getInstance().log(Logger::ERROR, "Failed to mount filesystem during backup. Title id: 0x%016lX; User id: 0x%lX%lX.", title.id(),
                (u64)(title.userId() >> 8), (u64)(title.userId()));
            return std::make_tuple(false, -2, "Failed to mount save.");
        }
    }
    else {
        Logger::getInstance().log(Logger::ERROR,
            "Failed to mount filesystem during backup with result 0x%08lX. Title id: 0x%016lX; User id: 0x%lX%lX.", res, title.id(),
            (u64)(title.userId() >> 8), (u64)(title.userId()));
        return std::make_tuple(false, res, "Failed to mount save.");
    }

    std::string suggestion = DateTime::dateTimeStr() + " " +
                             (StringUtils::containsInvalidChar(Account::username(title.userId()))
                                     ? ""
                                     : StringUtils::removeNotAscii(StringUtils::removeAccents(Account::username(title.userId()))));
    std::string customPath;

    if (MS::multipleSelectionEnabled()) {
        customPath = isNewFolder ? suggestion : "";
    }
    else {
        if (isNewFolder) {
            std::pair<bool, std::string> keyboardResponse = KeyboardManager::get().keyboard(suggestion);
            if (keyboardResponse.first) {
                customPath = StringUtils::removeForbiddenCharacters(keyboardResponse.second);
            }
            else {
                FileSystem::unmount();
                Logger::getInstance().log(Logger::INFO, "Copy operation aborted by the user through the system keyboard.");
                return std::make_tuple(false, 0, "Operation aborted by the user.");
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
            FileSystem::unmount();
            Logger::getInstance().log(Logger::ERROR, "Failed to recursively delete directory " + dstPath + " with result %d.", rc);
            return std::make_tuple(false, (Result)rc, "Failed to delete the existing backup\ndirectory recursively.");
        }
    }

    io::createDirectory(dstPath);
    res = io::copyDirectory("save:/", dstPath + "/");
    if (R_FAILED(res)) {
        FileSystem::unmount();
        io::deleteFolderRecursively((dstPath + "/").c_str());
        Logger::getInstance().log(Logger::ERROR, "Failed to copy directory " + dstPath + " with result 0x%08lX. Skipping...", res);
        return std::make_tuple(false, res, "Failed to backup save.");
    }

    refreshDirectories(title.id());

    FileSystem::unmount();
    if (!MS::multipleSelectionEnabled()) {
        blinkLed(4);
        ret = std::make_tuple(true, 0, "Progress correctly saved to disk.");
    }
    // TODO: figure out if this code can be accessed at all
    auto systemKeyboardAvailable = KeyboardManager::get().isSystemKeyboardAvailable();
    if (!systemKeyboardAvailable.first) {
        return std::make_tuple(
            false, systemKeyboardAvailable.second, "System keyboard applet not accessible.\nThe suggested destination folder was used\ninstead.");
    }

    Logger::getInstance().log(Logger::INFO, "Backup succeeded.");
    return ret;
}

std::tuple<bool, Result, std::string> io::restore(size_t index, u128 uid, size_t cellIndex, const std::string& nameFromCell)
{
    Result res                                = 0;
    std::tuple<bool, Result, std::string> ret = std::make_tuple(false, -1, "");
    Title title;
    getTitle(title, uid, index);

    Logger::getInstance().log(Logger::INFO, "Started restore of %s. Title id: 0x%016lX; User id: 0x%lX%lX.", title.name().c_str(), title.id(),
        (u64)(title.userId() >> 8), (u64)(title.userId()));

    FsFileSystem fileSystem;
    res = title.systemSave() ? FileSystem::mount(&fileSystem, title.id()) : FileSystem::mount(&fileSystem, title.id(), title.userId());
    if (R_SUCCEEDED(res)) {
        int rc = FileSystem::mount(fileSystem);
        if (rc == -1) {
            FileSystem::unmount();
            Logger::getInstance().log(Logger::ERROR, "Failed to mount filesystem during restore. Title id: 0x%016lX; User id: 0x%lX%lX.", title.id(),
                (u64)(title.userId() >> 8), (u64)(title.userId()));
            return std::make_tuple(false, -2, "Failed to mount save.");
        }
    }
    else {
        Logger::getInstance().log(Logger::ERROR,
            "Failed to mount filesystem during restore with result 0x%08lX. Title id: 0x%016lX; User id: 0x%lX%lX.", res, title.id(),
            (u64)(title.userId() >> 8), (u64)(title.userId()));
        return std::make_tuple(false, res, "Failed to mount save.");
    }

    std::string srcPath = title.fullPath(cellIndex) + "/";
    std::string dstPath = "save:/";

    res = io::deleteFolderRecursively(dstPath.c_str());
    if (R_FAILED(res)) {
        FileSystem::unmount();
        Logger::getInstance().log(Logger::ERROR, "Failed to recursively delete directory " + dstPath + " with result 0x%08lX.", res);
        return std::make_tuple(false, res, "Failed to delete save.");
    }

    res = io::copyDirectory(srcPath, dstPath);
    if (R_FAILED(res)) {
        FileSystem::unmount();
        Logger::getInstance().log(Logger::ERROR, "Failed to copy directory " + srcPath + " to " + dstPath + " with result 0x%08lX. Skipping...", res);
        return std::make_tuple(false, res, "Failed to restore save.");
    }

    res = fsdevCommitDevice("save");
    if (R_FAILED(res)) {
        Logger::getInstance().log(Logger::ERROR, "Failed to commit save with result 0x%08lX.", res);
        return std::make_tuple(false, res, "Failed to commit to save device.");
    }
    else {
        blinkLed(4);
        ret = std::make_tuple(true, 0, nameFromCell + "\nhas been restored successfully.");
    }

    FileSystem::unmount();

    Logger::getInstance().log(Logger::INFO, "Restore succeeded.");
    return ret;
}
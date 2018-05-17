/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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
    return (stat (path.c_str(), &buffer) == 0);
}

void io::copyFile(const std::string& srcPath, const std::string& dstPath)
{
    FILE* src = fopen(srcPath.c_str(), "rb");
    FILE* dst = fopen(dstPath.c_str(), "wb");
    if (src == NULL || dst == NULL)
    {
        return;
    }
     
    fseek(src, 0, SEEK_END);
    u64 sz = ftell(src);
    rewind(src);

    size_t size;
    char* buf = new char[BUFFER_SIZE];

    u64 offset = 0;
    size_t slashpos = srcPath.rfind("/");
    std::string name = srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1);
    while ((size = fread(buf, 1, BUFFER_SIZE, src)) > 0)
    {
        fwrite(buf, 1, size, dst);
        offset += size;
        Gui::drawCopy(name, offset, sz);
    }

    delete[] buf;
    fclose(src);
    fclose(dst);

    // commit each file to the save
    if (dstPath.rfind("save:/", 0) == 0)
    {
        fsdevCommitDevice("save");
    }
}

Result io::copyDirectory(const std::string& srcPath, const std::string& dstPath)
{
    Result res = 0;
    bool quit = false;
    Directory items(srcPath);
    
    if (!items.good())
    {
        return items.error();
    }
    
    for (size_t i = 0, sz = items.size(); i < sz && !quit; i++)
    {
        std::string newsrc = srcPath + items.entry(i);
        std::string newdst = dstPath + items.entry(i);
        
        if (items.folder(i))
        {
            res = io::createDirectory(newdst);
            if (R_SUCCEEDED(res))
            {
                newsrc += "/";
                newdst += "/";
                res = io::copyDirectory(newsrc, newdst);
            }
            else
            {
                quit = true;
            }
        }
        else
        {
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

// https://stackoverflow.com/questions/2256945/removing-a-non-empty-directory-programmatically-in-c-or-c
int io::deleteFolderRecursively(const char* path)
{
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = -1;

    if (d)
    {
        struct dirent *p;

        r = 0;
        while (!r && (p=readdir(d)))
        {
            int r2 = -1;
            char *buf;
            size_t len;

            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
            {
                continue;
            }

            len = path_len + strlen(p->d_name) + 2; 
            buf = new char[len];

            if (buf)
            {
                struct stat statbuf;

                snprintf(buf, len, "%s/%s", path, p->d_name);

                if (!stat(buf, &statbuf))
                {
                    if (S_ISDIR(statbuf.st_mode))
                    {
                        r2 = io::deleteFolderRecursively(buf);
                    }
                    else
                    {
                        r2 = unlink(buf);
                    }
                }

                delete[] buf;
            }

            r = r2;
        }

        closedir(d);
    }

    if (!r)
    {
        r = rmdir(path);
    }

    if (!r)
    {
        return r;
    }

    return 0;
}

void io::backup(size_t index)
{
    // check if multiple selection is enabled and don't ask for confirmation if that's the case
    if (!Gui::multipleSelectionEnabled())
    {
        if (!Gui::askForConfirmation("Backup selected save?"))
        {
            return;
        }
    }

    const size_t cellIndex = Gui::index(CELLS);
    const bool isNewFolder = cellIndex == 0;
    Result res = 0;
    
    Title title;
    getTitle(title, index);
    
    FsFileSystem fileSystem;
    res = FileSystem::mount(&fileSystem, title.id(), title.userId());
    if (R_SUCCEEDED(res))
    {
        int ret = FileSystem::mount(fileSystem);
        if (ret == -1)
        {
            FileSystem::unmount();
            Gui::createError(-2, "Failed to mount save.");
            return;
        }
    }
    else
    {
        Gui::createError(res, "Failed to mount save.");
        return;        
    }

    std::string suggestion = DateTime::dateTimeStr() + " " + StringUtils::removeNotAscii(StringUtils::removeAccents(Account::username(title.userId())));
    std::string customPath;

    if (Gui::multipleSelectionEnabled())
    {
        customPath = isNewFolder ? suggestion : Gui::nameFromCell(CELLS, cellIndex);
    }
    else
    {
        customPath = isNewFolder ? StringUtils::removeForbiddenCharacters(hbkbd::keyboard(suggestion)) : Gui::nameFromCell(CELLS, cellIndex);
    }
    
    std::string dstPath = title.path() + "/" + customPath;
    if (!isNewFolder || io::directoryExists(dstPath))
    {
        int ret = io::deleteFolderRecursively(dstPath.c_str());
        if (ret != 0)
        {
            FileSystem::unmount();
            Gui::createError((Result)ret, "Failed to delete the existing backup directory recursively.");
            return;
        }
    }
    
    res = io::createDirectory(dstPath);
    res = io::copyDirectory("save:/", dstPath + "/");
    if (R_FAILED(res))
    {
        FileSystem::unmount();
        io::deleteFolderRecursively(dstPath.c_str());
        Gui::createError(res, "Failed to backup save.");
        return;
    }
    
    refreshDirectories(title.id());
    
    FileSystem::unmount();
    Gui::createInfo("Success!", "Progress correctly saved to disk.");
}

void io::restore(size_t index)
{
    const size_t cellIndex = Gui::index(CELLS);
    if (cellIndex == 0 || !Gui::askForConfirmation("Restore selected save?"))
    {
        return;
    }
    
    Result res = 0;
    
    Title title;
    getTitle(title, index);
    
    FsFileSystem fileSystem;
    res = FileSystem::mount(&fileSystem, title.id(), title.userId());
    if (R_SUCCEEDED(res))
    {
        int ret = FileSystem::mount(fileSystem);
        if (ret == -1)
        {
            FileSystem::unmount();
            Gui::createError(-2, "Failed to mount save.");
            return;
        }
    }
    else
    {
        Gui::createError(res, "Failed to mount save.");
        return;        
    }

    std::string srcPath = title.path() + "/" + Gui::nameFromCell(CELLS, cellIndex) + "/";
    std::string dstPath = "save:/";
    
    res = io::deleteFolderRecursively(dstPath.c_str());
    if (R_FAILED(res))
    {
        FileSystem::unmount();
        Gui::createError(res, "Failed to delete save.");
        return;
    }
    
    res = io::copyDirectory(srcPath, dstPath);
    if (R_FAILED(res))
    {
        FileSystem::unmount();
        Gui::createError(res, "Failed to restore save.");
        return;
    }
    
    res = fsdevCommitDevice("save");
    if (R_FAILED(res))
    {
        Gui::createError(res, "Failed to commit to save device.");
    }
    else
    {
        Gui::createInfo("Success!", Gui::nameFromCell(CELLS, cellIndex) + " has been restored\nsuccessfully.");
    }
    
    FileSystem::unmount();
}
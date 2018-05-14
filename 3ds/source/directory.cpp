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

#include "directory.hpp"

Directory::Directory(FS_Archive archive, const std::u16string& root)
{
    mGood = false;
    mList.clear();
    Handle handle;
    
    mError = FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_UTF16, root.data()));
    if (R_FAILED(mError))
    {
        return;
    }
    
    u32 result;
    do {
        FS_DirectoryEntry item;
        mError = FSDIR_Read(handle, &result, 1, &item);
        if (result == 1)
        {
            mList.push_back(item);
        }
    } while(result);
    
    mError = FSDIR_Close(handle);
    if (R_FAILED(mError))
    {
        mList.clear();
        return;
    }
    
    mGood = true;
}

Result Directory::error(void)
{
    return mError;
}

bool Directory::good(void)
{
    return mGood;
}

std::u16string Directory::entry(size_t index)
{
    return index < mList.size() ? (char16_t*)mList.at(index).name : StringUtils::UTF8toUTF16("");
}

bool Directory::folder(size_t index)
{
    return index < mList.size() ? mList.at(index).attributes == FS_ATTRIBUTE_DIRECTORY : false; 
}

size_t Directory::size(void)
{
    return mList.size();
}
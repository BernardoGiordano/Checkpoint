/*  This file is part of Checkpoint
>	Copyright (C) 2017 Bernardo Giordano
>
>   This program is free software: you can redistribute it and/or modify
>   it under the terms of the GNU General Public License as published by
>   the Free Software Foundation, either version 3 of the License, or
>   (at your option) any later version.
>
>   This program is distributed in the hope that it will be useful,
>   but WITHOUT ANY WARRANTY; without even the implied warranty of
>   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
>   GNU General Public License for more details.
>
>   You should have received a copy of the GNU General Public License
>   along with this program.  If not, see <http://www.gnu.org/licenses/>.
>   See LICENSE for information.
*/

#include "directory.h"

Directory::Directory(FS_Archive archive, std::u16string root)
{
	loaded = false;
	Handle handle;
	error = 0;
	
	list.clear();
	
	error = FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_UTF16, root.data()));
	if (R_FAILED(error))
	{
		return;
	}
	
	u32 result = 0;
	do {
		FS_DirectoryEntry item;
		error = FSDIR_Read(handle, &result, 1, &item);
		if (result == 1)
		{
			list.push_back(item);
		}
	} while(result);
	
	error = FSDIR_Close(handle);
	if (R_FAILED(error))
	{
		list.clear();
		return;
	}
	
	loaded = true;
}

Result Directory::getError(void)
{
	return error;
}

bool Directory::getLoaded(void)
{
	return loaded;
}

std::u16string Directory::getItem(size_t index)
{
	if (index < list.size())
	{
		std::u16string name = (char16_t*)list.at(index).name;
		return name;
	}
	
	return u8tou16("");
}

bool Directory::isFolder(size_t index)
{
	if (index < list.size())
	{
		return list.at(index).attributes == FS_ATTRIBUTE_DIRECTORY;
	}
	
	return false;
}

size_t Directory::getCount(void)
{
	return list.size();
}
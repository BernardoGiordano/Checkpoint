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

#include "fsstream.h"

FSStream::FSStream(FS_Archive archive, std::u16string path, u32 flags)
{
	loaded = false;
	size = 0;
	
	res = FSUSER_OpenFile(&handle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
	if (R_SUCCEEDED(res))
	{
		FSFILE_GetSize(handle, &size);
		loaded = true;		
	}
}

FSStream::FSStream(FS_Archive archive, std::u16string path, u32 flags, u64 _size)
{
	loaded = false;
	size = _size;
	
	res = FSUSER_OpenFile(&handle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
	if (R_FAILED(res))
	{
		res = FSUSER_CreateFile(archive, fsMakePath(PATH_UTF16, path.data()), flags, size);
		if (R_SUCCEEDED(res))
		{
			res = FSUSER_OpenFile(&handle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
			if (R_SUCCEEDED(res))
			{
				loaded = true;
			}
		}
	}	
}

Result FSStream::close(void)
{
	res = FSFILE_Close(handle);
	return res;
}

bool FSStream::getLoaded(void)
{
	return loaded;
}

Result FSStream::getResult(void)
{
	return res;
}

u32 FSStream::getSize(void)
{
	return size;
}

Result FSStream::read(void *buf)
{
	return FSFILE_Read(handle, NULL, 0, buf, size);
}

Result FSStream::write(void *buf)
{
	return FSFILE_Write(handle, NULL, 0, buf, size, FS_WRITE_FLUSH);
}

u64 getFileSize(FS_Archive archive, std::u16string path)
{
	u64 size = 0;
	FSStream stream(archive, path, FS_OPEN_READ);
	if (stream.getLoaded())
	{
		size = stream.getSize();
	}
	stream.close();
	return size;
}

bool fileExist(FS_Archive archive, std::u16string path)
{
	FSStream stream(archive, path, FS_OPEN_READ);
	bool exist = stream.getLoaded();
	stream.close();
	return exist;
}

Result copyFile(FS_Archive srcArch, FS_Archive dstArch, std::u16string srcPath, std::u16string dstPath)
{
	Result res;
	u64 size = getFileSize(srcArch, srcPath);
	if (size == 0)
	{
		return -1;
	}
	
	u8 *buf = new u8[size];
	res = readFile(srcArch, buf, srcPath);
	if (R_FAILED(res))
	{
		return res;
	}
	
	res = writeFile(dstArch, buf, dstPath, size);
	
	delete[] buf;
	return res;
}

Result copyDirectory(FS_Archive srcArch, FS_Archive dstArch, std::u16string srcPath, std::u16string dstPath)
{
	Result res = 0;
	bool quit = false;
	Directory items(srcArch, srcPath);
	
	if (!items.getLoaded())
	{
		return items.getError();
	}
	
	for (size_t i = 0, sz = items.getCount(); i < sz && !quit; i++)
	{
		std::u16string newsrc = srcPath + items.getItem(i);
		std::u16string newdst = dstPath + items.getItem(i);
		
		if (items.isFolder(i))
		{
			res = createDirectory(dstArch, newdst);
			
			if (R_FAILED(res))
			{
				quit = true;
				createError(res, "CreateDirectory: " + u16tou8(newdst));
			}
			else
			{
				newsrc += u8tou16("/");
				newdst += u8tou16("/");
				res = copyDirectory(srcArch, dstArch, newsrc, newdst);
			}
		}
		else
		{
			res = copyFile(srcArch, dstArch, newsrc, newdst);
			if (R_FAILED(res))
			{
				createError(res, "src: " + u16tou8(newsrc) + " to: " + u16tou8(newdst));
			}
		}
	}
	
	return res;
}

Result readFile(FS_Archive archive, u8* buf, std::u16string path)
{
	Result res;
	FSStream stream(archive, path, FS_OPEN_READ);
	if (stream.getLoaded())
	{
		res = stream.read(buf);
	}
	else
	{
		res = stream.getResult();
	}
	
	stream.close();
	return res;	
}

Result writeFile(FS_Archive archive, u8* buf, std::u16string path, u64 size)
{
	Result res;
	if (fileExist(archive, path))
	{
		res = FSUSER_DeleteFile(archive, fsMakePath(PATH_UTF16, path.data()));
		if (R_FAILED(res))
		{
			return res;
		}
	}
	
	FSStream stream(archive, path, FS_OPEN_WRITE, size);
	if (stream.getLoaded())
	{
		res = stream.write(buf);
	}
	else
	{
		res = stream.getResult();
	}
	
	stream.close();
	return res;
}

Result createDirectory(FS_Archive archive, std::u16string path)
{
	return FSUSER_CreateDirectory(archive, fsMakePath(PATH_UTF16, path.data()), 0);
}

bool directoryExist(FS_Archive archive, std::u16string path)
{
	Handle handle;

	if (R_FAILED(FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_UTF16, path.data()))))
	{
		return false;
	}

	if (R_FAILED(FSDIR_Close(handle)))
	{
		return false;
	}

	return true;
}

void backup(size_t index)
{
	const size_t cellIndex = getScrollableIndex();
	const bool isNewFolder = cellIndex == 0;
	Result res = 0;
	
	Title title;
	getTitle(title, index);
	
	FS_Archive archive;
	res = getArchiveSave(&archive, title.getMediaType(), title.getLowId(), title.getHighId());
	if (R_SUCCEEDED(res))
	{
		std::u16string customPath = isNewFolder ? getPath() : u8tou16(getPathFromCell(cellIndex).c_str());
		if (!customPath.compare(u8tou16(" ")))
		{
			FSUSER_CloseArchive(archive);
			return;
		}
		
		std::u16string dstPath = title.getBackupPath() + u8tou16("/") + customPath;
		
		if (!isNewFolder || directoryExist(getArchiveSDMC(), dstPath))
		{
			res = FSUSER_DeleteDirectoryRecursively(getArchiveSDMC(), fsMakePath(PATH_UTF16, dstPath.data()));
			if (R_FAILED(res))
			{
				FSUSER_CloseArchive(archive);
				createError(res, "Failed to delete the existing backup directory recursively.");
				return;
			}
		}
		
		res = createDirectory(getArchiveSDMC(), dstPath);
		if (R_FAILED(res))
		{
			FSUSER_CloseArchive(archive);
			createError(res, "Failed to create destination directory.");
			return;
		}
		
		dstPath += u8tou16("/");
		
		res = copyDirectory(archive, getArchiveSDMC(), u8tou16("/"), dstPath);
		if (R_FAILED(res))
		{
			FSUSER_CloseArchive(archive);
			createError(res, "Failed to backup save.");
			return;
		}
		refreshDirectories(index);
	}
	else
	{
		createError(res, "Failed to open save archive.");
	}
	
	FSUSER_CloseArchive(archive);
}

void restore(size_t index)
{
	const size_t cellIndex = getScrollableIndex();
	if (cellIndex == 0)
	{
		return;
	}
	
	Result res = 0;
	
	Title title;
	getTitle(title, index);
	
	FS_Archive archive;
	res = getArchiveSave(&archive, title.getMediaType(), title.getLowId(), title.getHighId());
	if (R_SUCCEEDED(res))
	{
		std::u16string srcPath = title.getBackupPath() + u8tou16("/") + u8tou16(getPathFromCell(cellIndex).c_str()) + u8tou16("/");
		std::u16string dstPath = u8tou16("/");
		
		res = FSUSER_DeleteDirectoryRecursively(archive, fsMakePath(PATH_UTF16, dstPath.data()));
		if (R_FAILED(res))
		{
			FSUSER_CloseArchive(archive);
			createError(res, "Failed to delete the save directory recursively.");
			return;
		}
		
		res = copyDirectory(getArchiveSDMC(), archive, srcPath, dstPath);
		if (R_FAILED(res))
		{
			FSUSER_CloseArchive(archive);
			createError(res, "Failed to restore save.");
			return;
		}
		
		res = FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
		if (R_FAILED(res))
		{
			FSUSER_CloseArchive(archive);
			createError(res, "Failed to commit save data.");
			return;			
		}
		
		u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (title.getUniqueId() << 8);
		res = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, NULL, 1);
		if (R_FAILED(res))
		{
			FSUSER_CloseArchive(archive);
			createError(res, "Failed to fix secure value.");
			return;			
		}
	}
	else
	{
		createError(res, "Failed to open save archive.");
	}
	
	FSUSER_CloseArchive(archive);
}
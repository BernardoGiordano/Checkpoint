/*  This file is part of Checkpoint
>	Copyright (C) 2017/2018 Bernardo Giordano
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
	offset = 0;
	
	res = FSUSER_OpenFile(&handle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
	if (R_SUCCEEDED(res))
	{
		FSFILE_GetSize(handle, (u64*)&size);
		loaded = true;		
	}
}

FSStream::FSStream(FS_Archive archive, std::u16string path, u32 flags, u32 _size)
{
	loaded = false;
	size = _size;
	offset = 0;
	
	res = FSUSER_OpenFile(&handle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
	if (R_FAILED(res))
	{
		res = FSUSER_CreateFile(archive, fsMakePath(PATH_UTF16, path.data()), 0, size);
		if (R_SUCCEEDED(res))
		{
			res = FSUSER_OpenFile(&handle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
			if (R_SUCCEEDED(res))
			{
				loaded = true;
			}
		}
	}
	else
	{
		loaded = true;
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

u32 FSStream::read(void *buf, u32 sz)
{
	u32 rd = 0;
	res = FSFILE_Read(handle, &rd, offset, buf, sz);
	offset += rd;
	return rd;
}

u32 FSStream::write(void *buf, u32 sz)
{
	u32 wt = 0;
	res = FSFILE_Write(handle, &wt, offset, buf, sz, FS_WRITE_FLUSH);
	offset += wt;
	return wt;
}

bool FSStream::isEndOfFile(void)
{
	return offset >= size;
}

u32 FSStream::getOffset(void)
{
	return offset;
}
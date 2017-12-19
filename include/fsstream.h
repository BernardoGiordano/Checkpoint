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

#ifndef FSSTREAM_H
#define FSSTREAM_H

#include "common.h"

#define BUFFER_SIZE 0x100000

bool fileExist(FS_Archive archive, std::u16string path);
Result createDirectory(FS_Archive archive, std::u16string path);
bool directoryExist(FS_Archive archive, std::u16string path);
void copyFile(FS_Archive srcArch, FS_Archive dstArch, std::u16string srcPath, std::u16string dstPath);
Result copyDirectory(FS_Archive srcArch, FS_Archive dstArch, std::u16string srcPath, std::u16string dstPath);
Result deleteFilesRecursively(FS_Archive arch, std::u16string path);

void backup(size_t index);
void restore(size_t index);

class FSStream
{
public:
	FSStream(FS_Archive archive, std::u16string path, u32 flags);
	FSStream(FS_Archive archive, std::u16string path, u32 flags, u32 size);
	
	Result close(void);
	bool getLoaded(void);
	Result getResult(void);
	u32 getSize(void);
	
	u32 read(void *buf, u32 size);
	u32 write(void *buf, u32 size);
	
	bool isEndOfFile(void);

private:
	Handle handle;
	u32 size;
	u32 offset;
	Result res;
	bool loaded;
};

#endif
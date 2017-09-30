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

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "common.h"

typedef enum {
	MODE_SAVE,
	MODE_EXTDATA
} Mode_t;

Mode_t getMode(void);
void setMode(Mode_t newmode);

Result archiveInit(void);
void archiveExit(void);
FS_Archive getArchiveSDMC(void);
Result getArchiveSave(FS_Archive* archive, FS_MediaType mediatype, u32 lowid, u32 highid);
Result getArchiveExtdata(FS_Archive* archive, u32 extdata);

bool isSaveAccessible(FS_MediaType mediatype, u32 lowid, u32 highid);
bool isExtdataAccessible(u32 extdata);

#endif
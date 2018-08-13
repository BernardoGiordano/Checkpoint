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

#include "fsstream.hpp"

FSStream::FSStream(FS_Archive archive, const std::u16string& path, u32 flags)
{
    mGood = false;
    mSize = 0;
    mOffset = 0;

    mResult = FSUSER_OpenFile(&mHandle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
    if (R_SUCCEEDED(mResult))
    {
        FSFILE_GetSize(mHandle, (u64*)&mSize);
        mGood = true;
    }
}

FSStream::FSStream(FS_Archive archive, const std::u16string& path, u32 flags, u32 size)
{
    mGood = false;
    mSize = size;
    mOffset = 0;

    mResult = FSUSER_OpenFile(&mHandle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
    if (R_FAILED(mResult))
    {
        mResult = FSUSER_CreateFile(archive, fsMakePath(PATH_UTF16, path.data()), 0, mSize);
        if (R_SUCCEEDED(mResult))
        {
            mResult = FSUSER_OpenFile(&mHandle, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
            if (R_SUCCEEDED(mResult))
            {
                mGood = true;
            }
        }
    }
    else
    {
        mGood = true;
    }
}

Result FSStream::close(void)
{
    mResult = FSFILE_Close(mHandle);
    return mResult;
}

bool FSStream::good(void)
{
    return mGood;
}

Result FSStream::result(void)
{
    return mResult;
}

u32 FSStream::size(void)
{
    return mSize;
}

u32 FSStream::read(void *buf, u32 sz)
{
    u32 rd = 0;
    mResult = FSFILE_Read(mHandle, &rd, mOffset, buf, sz);
    if (R_FAILED(mResult))
    {
        if (rd > sz)
        {
            rd = sz;
        }
    }
    mOffset += rd;
    return rd;
}

u32 FSStream::write(const void *buf, u32 sz)
{
    u32 wt = 0;
    mResult = FSFILE_Write(mHandle, &wt, mOffset, buf, sz, FS_WRITE_FLUSH);
    mOffset += wt;
    return wt;
}

bool FSStream::eof(void)
{
    return mOffset >= mSize;
}

u32 FSStream::offset(void)
{
    return mOffset;
}
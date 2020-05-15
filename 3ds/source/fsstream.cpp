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

#include "fsstream.hpp"
#include "csvc.hpp"

FSStream::FSStream(FS_Archive archive, const std::u16string& path, u32 flags)
{
    mGood   = false;
    mSize   = 0;
    mOffset = 0;
    Handle hnd;

    mResult = FSUSER_OpenFile(&hnd, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
    if (R_SUCCEEDED(mResult)) {
        FSFILE_GetSize(hnd, (u64*)&mSize);
        mHandle = hnd;
        mGood = true;
    }
}

FSStream::FSStream(FS_Archive archive, const std::u16string& path, u32 flags, u32 size)
{
    mGood   = false;
    mSize   = size;
    mOffset = 0;
    Handle hnd;

    mResult = FSUSER_OpenFile(&hnd, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
    if (R_FAILED(mResult)) {
        mResult = FSUSER_CreateFile(archive, fsMakePath(PATH_UTF16, path.data()), 0, mSize);
        if (R_SUCCEEDED(mResult)) {
            mResult = FSUSER_OpenFile(&hnd, archive, fsMakePath(PATH_UTF16, path.data()), flags, 0);
            if (R_SUCCEEDED(mResult)) {
                mGood = true;
            }
        }
    }
    else {
        mGood = true;
    }
    
    mHandle = hnd;
}

static const u32 pxi_path[5] = {
    1,
    1,
    3,
    0,
    0
};
FSStream::FSStream(FSPXI_Archive archive, u32 flags)
{
    mGood   = false;
    mSize   = 0;
    mOffset = 0;
    FSPXI_File hnd;

    mResult = FSPXI_OpenFile(FsPxiHandle, &hnd, archive, {PATH_BINARY, 20, pxi_path}, flags, 0);
    if(R_SUCCEEDED(mResult))
    {
        FSPXI_GetFileSize(FsPxiHandle, hnd, (u64*)&mSize);
        mHandle = hnd;
        mGood = true;
    }
}

FSStream::FSStream(FSPXI_Archive archive, u32 flags, u32 size)
{
    mGood   = false;
    mSize   = size;
    mOffset = 0;
    FSPXI_File hnd;

    mResult = FSPXI_OpenFile(FsPxiHandle, &hnd, archive, {PATH_BINARY, 20, pxi_path}, flags, 0);
    if(R_SUCCEEDED(mResult))
    {
        mHandle = hnd;
        mGood = true;
    }
}

Result FSStream::close(void)
{
    if(mHandle.index() == 0)
    {
        mResult = FSPXI_CloseFile(FsPxiHandle, std::get<0>(mHandle));
    }
    else
    {
        mResult = FSFILE_Close(std::get<1>(mHandle));
    }
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

using ReadFunction = Result(*)(MultiHandle, u32*, u64, void*, u32);
u32 FSStream::read(void* buf, u32 sz)
{
    u32 rd  = 0;
    ReadFunction calling = nullptr;

    if(mHandle.index() == 0)
    {
        calling = [](MultiHandle hnd, u32* readAmount, u64 off, void* b, u32 s) -> Result {
            return FSPXI_ReadFile(FsPxiHandle, std::get<0>(hnd), readAmount, off, b, s);
        };
    }
    else
    {
        calling = [](MultiHandle hnd, u32* readAmount, u64 off, void* b, u32 s) -> Result {
            return FSFILE_Read(std::get<1>(hnd), readAmount, off, b, s);
        };
    }

    mResult = calling(mHandle, &rd, mOffset, buf, sz);
    if (R_FAILED(mResult)) {
        if (rd > sz) {
            rd = sz;
        }
    }
    mOffset += rd;
    return rd;
}

using WriteFunction = Result(*)(MultiHandle, u32*, u64, const void*, u32);
u32 FSStream::write(const void* buf, u32 sz)
{
    u32 wt  = 0;
    WriteFunction calling = nullptr;

    if(mHandle.index() == 0)
    {
        calling = [](MultiHandle hnd, u32* readAmount, u64 off, const void* b, u32 s) -> Result {
            return FSPXI_WriteFile(FsPxiHandle, std::get<0>(hnd), readAmount, off, b, s, FS_WRITE_FLUSH);
        };
    }
    else
    {
        calling = [](MultiHandle hnd, u32* readAmount, u64 off, const void* b, u32 s) -> Result {
            return FSFILE_Write(std::get<1>(hnd), readAmount, off, b, s, FS_WRITE_FLUSH);
        };
    }

    mResult = calling(mHandle, &wt, mOffset, buf, sz);
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

void FSStream::offset(u32 offset)
{
    mOffset = offset;
}
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

#include "fspxi.hpp"
#include "csvc.hpp"

static Handle mFsPxiHandle;

struct PFile {
    u32 lower = 0;
    u32 upper = 0;

    ~PFile()
    {
        if(lower == 0 && upper == 0) return;

        u32 *cmdbuf = getThreadCommandBuffer();
        cmdbuf[0] = IPC_MakeHeader(0xF,2,0); // 0xF0080, FSPXI:CloseFile according to 3dbrew
        cmdbuf[1] = lower;
        cmdbuf[2] = upper;

        svcSendSyncRequest(mFsPxiHandle);
    }
};

static Result openFile(PFile* file, FSPXI::PArchive archive, u32 flags)
{
    static constexpr u32 save_file_path[5] = {
        1,
        1,
        3,
        0,
        0
    };

    Result res = 0;

    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0x1,7,2); // 0x101C2, FSPXI:OpenFile according to 3dbrew
    cmdbuf[1] = 0;
    cmdbuf[2] = archive.lower;
    cmdbuf[3] = archive.upper;
    cmdbuf[4] = PATH_BINARY;
    cmdbuf[5] = sizeof(save_file_path);
    cmdbuf[6] = flags;
    cmdbuf[7] = 0;
    cmdbuf[8] = IPC_Desc_PXIBuffer(sizeof(save_file_path), 0, 1);
    cmdbuf[9] = (uintptr_t)save_file_path;

    res = svcSendSyncRequest(mFsPxiHandle);
    if(R_FAILED(res)) return res;

    res = cmdbuf[1];
    if(R_FAILED(res)) return res;

    file->lower = cmdbuf[2];
    file->upper = cmdbuf[3];

    return 0;
}

Result FSPXI::init(void)
{
    return svcControlService(SERVICEOP_STEAL_CLIENT_SESSION, &mFsPxiHandle, "PxiFS0");
}

Result FSPXI::writeToFile(PArchive archive, const std::vector<u8>& data)
{
    PFile file;
    Result res = openFile(&file, archive, FS_OPEN_WRITE);
    u32 *cmdbuf = getThreadCommandBuffer();

    cmdbuf[0] = IPC_MakeHeader(0xB,6,2); // 0xB0182, FSPXI:WriteFile according to 3dbrew
	cmdbuf[1] = file.lower;
	cmdbuf[2] = file.upper;
	cmdbuf[3] = 0;
	cmdbuf[4] = 0;
	cmdbuf[5] = 0; // FLUSH_FLAGS
	cmdbuf[6] = data.size();
	cmdbuf[7] = IPC_Desc_PXIBuffer(data.size(), 0, 1);
	cmdbuf[8] = (u32)data.data();

    res = svcSendSyncRequest(mFsPxiHandle);
    if(R_FAILED(res)) return res;
    
    return cmdbuf[1];
}

Result FSPXI::readFromFile(PArchive archive, std::vector<u8>& data)
{
    PFile file;
    Result res = openFile(&file, archive, FS_OPEN_READ);
    if(R_FAILED(res)) return res;

    u8 read_data[0x1000];
    u32 read_amount = 0;
    u64 offset = 0;

    do {
        u32 *cmdbuf = getThreadCommandBuffer();

        cmdbuf[0] = IPC_MakeHeader(0x9,5,2); // 0x90142, FSPXI:ReadFile according to 3dbrew
        cmdbuf[1] = file.lower;
        cmdbuf[2] = file.upper;
        cmdbuf[3] = (u32)(offset & 0xFFFFFFFF);
        cmdbuf[4] = (u32)((offset >> 32) & 0xFFFFFFFF);
        cmdbuf[5] = sizeof(read_data);
        cmdbuf[6] = IPC_Desc_PXIBuffer(sizeof(read_data), 0, 0);
        cmdbuf[7] = (u32)read_data;

        res = svcSendSyncRequest(mFsPxiHandle);
        if(R_FAILED(res)) return res;
        
        res = cmdbuf[1];
        if(R_FAILED(res)) return res;

        read_amount = cmdbuf[2];
        data.insert(data.end(), read_data, read_data + read_amount);
        offset += read_amount;
    } while(read_amount == sizeof(read_data));

    return 0;
}

Result FSPXI::closeArchive(PArchive archive)
{
    Result res = 0;

    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x16,2,0);
    cmdbuf[1] = archive.lower;
    cmdbuf[2] = archive.upper;

    res = svcSendSyncRequest(mFsPxiHandle);
    if(R_FAILED(res)) return res;

    return cmdbuf[1];
}

Result FSPXI::gbasave(PArchive* archive, FS_MediaType mediatype, u32 lowid, u32 highid)
{
    const u32 path[4] = {
		lowid,
		highid,
		(u32)mediatype,
		(u32)1
	};

    Result res = 0;

    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x12,3,2); // 0x1200C2, FSPXI:OpenArchive according to 3dbrew
    cmdbuf[1] = ARCHIVE_SAVEDATA_AND_CONTENT;
    cmdbuf[2] = PATH_BINARY;
    cmdbuf[3] = sizeof(path);
    cmdbuf[4] = IPC_Desc_PXIBuffer(sizeof(path), 0, 1);
    cmdbuf[5] = (u32)path;

    res = svcSendSyncRequest(mFsPxiHandle);
    if(R_FAILED(res)) return res;

    archive->lower = cmdbuf[2];
    archive->upper = cmdbuf[3];

    return cmdbuf[1];
}

bool FSPXI::accessible(FS_MediaType mediatype, u32 lowid, u32 highid)
{
    PArchive archive;
    Result res = gbasave(&archive, mediatype, lowid, highid);
    if (R_SUCCEEDED(res)) {
        closeArchive(archive);
        return true;
    }
    return false;
}

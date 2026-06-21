/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#include "archive.hpp"
#include "csvc.hpp"
#include "fsstream.hpp"

void ArchiveHandle::close(void)
{
    if (!mValid) {
        return;
    }
    if (mRaw) {
        FSPXI_CloseArchive(FsPxiHandle, mPxi);
    }
    else {
        FSUSER_CloseArchive(mFs);
    }
    mValid = false;
}

static FS_Archive mSdmc;

Result Archive::init(void)
{
    Result res = FSUSER_OpenArchive(&mSdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    if (R_FAILED(res)) {
        return res;
    }
    // Steal a session to the PxiFS0 service so we can talk to FSPXI directly,
    // which lets us read/write the decrypted raw GBA VC save files.
    return svcControlService(SERVICEOP_STEAL_CLIENT_SESSION, &FsPxiHandle, "PxiFS0");
}

void Archive::exit(void)
{
    FSUSER_CloseArchive(mSdmc);
}

FS_Archive Archive::sdmc(void)
{
    return mSdmc;
}

Result Archive::save(FS_Archive* archive, FS_MediaType mediatype, u32 lowid, u32 highid)
{
    if (mediatype == MEDIATYPE_NAND) {
        const u32 path[2] = {mediatype, (0x00020000 | lowid >> 8)};
        return FSUSER_OpenArchive(archive, ARCHIVE_SYSTEM_SAVEDATA, {PATH_BINARY, 8, path});
    }
    else {
        const u32 path[3] = {mediatype, lowid, highid};
        return FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, {PATH_BINARY, 12, path});
    }
    return 0;
}

Result Archive::rawSave(FSPXI_Archive* archive, FS_MediaType mediatype, u32 lowid, u32 highid)
{
    const u32 path[4] = {lowid, highid, mediatype, 1};
    return FSPXI_OpenArchive(FsPxiHandle, archive, ARCHIVE_SAVEDATA_AND_CONTENT, {PATH_BINARY, 16, path});
}

Result Archive::extdata(FS_Archive* archive, u32 ext)
{
    const u32 path[3] = {MEDIATYPE_SD, ext, 0};
    return FSUSER_OpenArchive(archive, ARCHIVE_EXTDATA, {PATH_BINARY, 12, path});
}

SaveDataSource SaveDataSource::ctrSave(FS_MediaType media, u32 lowid, u32 highid)
{
    return SaveDataSource(Kind::CtrSave, media, lowid, highid);
}

SaveDataSource SaveDataSource::rawGba(FS_MediaType media, u32 lowid, u32 highid)
{
    return SaveDataSource(Kind::RawGbaSave, media, lowid, highid);
}

SaveDataSource SaveDataSource::extdata(u32 extdataId)
{
    return SaveDataSource(Kind::Extdata, MEDIATYPE_SD, extdataId, 0);
}

ArchiveHandle SaveDataSource::open(Result& res) const
{
    switch (mKind) {
        case Kind::CtrSave: {
            FS_Archive archive;
            res = Archive::save(&archive, mMedia, mA, mB);
            return R_SUCCEEDED(res) ? ArchiveHandle::fromFs(archive) : ArchiveHandle();
        }
        case Kind::RawGbaSave: {
            FSPXI_Archive archive;
            res = Archive::rawSave(&archive, mMedia, mA, mB);
            return R_SUCCEEDED(res) ? ArchiveHandle::fromPxi(archive) : ArchiveHandle();
        }
        case Kind::Extdata: {
            FS_Archive archive;
            res = Archive::extdata(&archive, mA);
            return R_SUCCEEDED(res) ? ArchiveHandle::fromFs(archive) : ArchiveHandle();
        }
    }
    res = -1;
    return ArchiveHandle();
}

bool SaveDataSource::accessible(void) const
{
    Result res         = 0;
    ArchiveHandle handle = open(res);
    if (!handle) {
        return false;
    }
    // A raw GBA VC archive opens even when no save is present; it is only really
    // accessible if the embedded save file opens cleanly.
    if (mKind == Kind::RawGbaSave) {
        FSStream file(handle.pxi(), FS_OPEN_READ);
        bool good = file.good();
        file.close();
        return good;
    }
    return true;
}

bool Archive::setPlayCoins(void)
{
    FS_Archive archive;
    const u32 path[3] = {MEDIATYPE_NAND, 0xF000000B, 0x00048000};
    Result res        = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, {PATH_BINARY, 0xC, path});
    if (R_SUCCEEDED(res)) {
        FSStream s(archive, StringUtils::UTF8toUTF16("/gamecoin.dat"), FS_OPEN_READ | FS_OPEN_WRITE);
        if (s.good()) {
            int coinAmount = KeyboardManager::get().numericPad();
            if (coinAmount >= 0) {
                coinAmount = coinAmount > 300 ? 300 : coinAmount;
                s.offset(4);
                u8 buf[2] = {(u8)coinAmount, (u8)(coinAmount >> 8)};
                s.write(buf, 2);
            }

            s.close();
            FSUSER_CloseArchive(archive);
            return true;
        }
        FSUSER_CloseArchive(archive);
    }
    return false;
}
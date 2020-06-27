/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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
#include "fileptr.hpp"
#include "keyboard.hpp"

Result Archive::mount(FS_ArchiveID archiveID, FS_Path archivePath)
{
    return archiveMount(archiveID, archivePath, MOUNT_ARCHIVE_NAME);
}
Result Archive::commitSave()
{
    return archiveCommitSaveData(MOUNT_ARCHIVE_NAME);
}
Result Archive::unmount()
{
    return archiveUnmount(MOUNT_ARCHIVE_NAME);
}
bool Archive::setPlayCoins()
{
    const u32 path[3] = {MEDIATYPE_NAND, 0xF000000B, 0x00048000};
    Result res        = mount(ARCHIVE_SHARED_EXTDATA, {PATH_BINARY, 12, path});
    bool success = false;
    if (R_SUCCEEDED(res)) {
        { // extra scope to make raii kick in for the unique_ptr, having an open FILE* outlive its device is not good
            FilePtr fh = openFile("/gamecoin.dat", "r+");
            if(fh) {
                int coinAmount = Keyboard::numpad();
                if (coinAmount >= 0) {
                    coinAmount = coinAmount > 300 ? 300 : coinAmount;
                    fseek(fh.get(), 4, SEEK_SET);
                    u8 buf[2] = {u8(coinAmount & 0xFF), u8((coinAmount >> 8) & 0xFF)};
                    fwrite(buf, 1, 2, fh.get());
                    success = true;
                }
            }
        }
        Archive::unmount();
    }
    return success;
}
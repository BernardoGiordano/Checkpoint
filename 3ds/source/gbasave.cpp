/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
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

#include "gbasave.hpp"
#include "csvc.hpp"
#include "fsstream.hpp"
#include "logging.hpp"
#include "util.hpp"
#include <cstring>
#include <memory>

namespace {
    // See https://www.3dbrew.org/wiki/3DS_Virtual_Console#NAND_Savegame
    struct AgbSaveHeader {
        u8 magic[4]; // ".SAV"
        u8 reserved0[0xC];
        u8 cmac[0x10];
        u8 reserved1[0x10];
        u32 unknown0;
        u32 timesSaved;
        u64 titleId;
        u8 sdCid[0x10];
        u32 saveStart; // always 0x200
        u32 saveSize;
        u8 reserved2[0x8];
        u32 unknown1;
        u32 unknown2;
        u8 reserved3[0x198];
    } __attribute__((packed));
    static_assert(sizeof(AgbSaveHeader) == 0x200, "AgbSaveHeader must be 0x200 bytes");

    constexpr u32 kHeaderSize = sizeof(AgbSaveHeader);
    constexpr u32 kCmacOffset = offsetof(AgbSaveHeader, cmac);
    // The slot CMAC covers the header from unknown0 (0x30) to the end plus the
    // whole save; magic/reserved0/cmac/reserved1 stay out of the hash.
    constexpr u32 kHashStart = 0x30;

    // Every save size a GBA VC footer can configure (EEPROM 512/8K, SRAM 32K,
    // flash 64K/128K); doubles as the bottom-slot search list when the top
    // slot is uninitialized.
    constexpr u32 kValidSaveSizes[] = {512, 8 * 1024, 32 * 1024, 64 * 1024, 128 * 1024};

    bool validSaveSize(u32 size)
    {
        for (u32 candidate : kValidSaveSizes) {
            if (size == candidate) {
                return true;
            }
        }
        return false;
    }

    bool validHeader(const AgbSaveHeader& hdr)
    {
        return std::memcmp(hdr.magic, ".SAV", 4) == 0 && hdr.saveStart == kHeaderSize && validSaveSize(hdr.saveSize);
    }

    bool readHeaderAt(FSStream& file, u32 offset, AgbSaveHeader& out)
    {
        file.offset(offset);
        return file.read(&out, kHeaderSize) == kHeaderSize && R_SUCCEEDED(file.result()) && validHeader(out);
    }

    // Finds the newest slot of the container behind `file` (an AGBSAVE
    // container: PXI save file or legacy SD dump). Returns false when no slot
    // validates, i.e. the game never saved. On true, `hdr`/`slotOffset`
    // describe the slot whose save is current (higher timesSaved; the top
    // slot wins ties, matching GM9).
    bool locateCurrentSlot(FSStream& file, AgbSaveHeader& hdr, u32& slotOffset)
    {
        AgbSaveHeader top;
        if (readHeaderAt(file, 0, top)) {
            AgbSaveHeader bottom;
            u32 bottomOffset = kHeaderSize + top.saveSize;
            if (readHeaderAt(file, bottomOffset, bottom) && bottom.timesSaved > top.timesSaved) {
                hdr        = bottom;
                slotOffset = bottomOffset;
            }
            else {
                hdr        = top;
                slotOffset = 0;
            }
            return true;
        }
        // Top slot uninitialized: the bottom slot can still hold a save. Its
        // offset depends on the (unknown) save size, so probe every valid one.
        for (u32 size : kValidSaveSizes) {
            u32 bottomOffset = kHeaderSize + size;
            if (readHeaderAt(file, bottomOffset, hdr)) {
                slotOffset = bottomOffset;
                return true;
            }
        }
        return false;
    }

    // AES-CMAC of the slot as AGB_FIRM expects it. FSPXI_CalcSavegameMAC does
    // the console/title-specific keying over the SHA256 of the hashed region;
    // the first call after opening the file can return a stale result, so call
    // until two consecutive results agree (same workaround as PKSM).
    Result calcSlotCmac(FSPXI_File file, const AgbSaveHeader& hdr, const u8* save, u8 outCmac[0x10])
    {
        u8 hash[SHA256_BLOCK_SIZE];
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, (const BYTE*)&hdr + kHashStart, kHeaderSize - kHashStart);
        sha256_update(&ctx, save, hdr.saveSize);
        sha256_final(&ctx, hash);

        u8 prev[0x10];
        Result res = 0;
        for (int tries = 0; tries < 10; tries++) {
            std::memcpy(prev, outCmac, sizeof(prev));
            res = FSPXI_CalcSavegameMAC(FsPxiHandle, file, hash, sizeof(hash), outCmac, 0x10);
            if (R_FAILED(res)) {
                return res;
            }
            if (tries > 0 && std::memcmp(prev, outCmac, sizeof(prev)) == 0) {
                break;
            }
        }
        return res;
    }

    // Pulls the current slot's bare save out of the AGBSAVE container behind
    // `file` into a fresh buffer. Used both on the PXI file (backup) and on a
    // legacy full-container SD dump (restore of a pre-format-change backup).
    Result extractCurrentSave(FSStream& file, AgbSaveHeader& hdr, std::unique_ptr<u8[]>& save)
    {
        u32 slotOffset = 0;
        if (!locateCurrentSlot(file, hdr, slotOffset)) {
            return GbaSave::resNoSave;
        }
        save = std::make_unique<u8[]>(hdr.saveSize);
        file.offset(slotOffset + kHeaderSize);
        if (file.read(save.get(), hdr.saveSize) != hdr.saveSize || R_FAILED(file.result())) {
            return R_FAILED(file.result()) ? file.result() : GbaSave::resBadBackup;
        }
        return 0;
    }
}

Result GbaSave::backup(FSPXI_Archive arch, FS_Archive sdmc, const std::u16string& dstPath, ProgressSink& sink)
{
    FSStream input(arch, FS_OPEN_READ);
    if (!input.good()) {
        Logging::error("Failed to open GBA save for backup with result 0x{:08X}.", (u32)input.result());
        return input.result();
    }

    AgbSaveHeader hdr;
    std::unique_ptr<u8[]> save;
    Result res = extractCurrentSave(input, hdr, save);
    input.close();
    if (R_FAILED(res)) {
        Logging::error("No initialized GBA save slot to backup (result 0x{:08X}).", (u32)res);
        return res;
    }

    size_t slashpos = dstPath.rfind(StringUtils::UTF8toUTF16("/"));
    sink.startFile(dstPath.substr(slashpos + 1, dstPath.length() - slashpos - 1), hdr.saveSize);
    if (sink.cancelled()) {
        return 0;
    }

    FSStream output(sdmc, dstPath, FS_OPEN_WRITE, hdr.saveSize);
    if (!output.good()) {
        Logging::error(
            "Failed to open destination {} during GBA save backup with result 0x{:08X}.", StringUtils::UTF16toUTF8(dstPath), (u32)output.result());
        return output.result();
    }
    u32 wt = output.write(save.get(), hdr.saveSize);
    res    = output.result();
    output.close();
    if (R_FAILED(res) || wt != hdr.saveSize) {
        Logging::error("Failed to write GBA save backup ({} of {} bytes, result 0x{:08X}).", wt, (u32)hdr.saveSize, (u32)res);
        return R_FAILED(res) ? res : resBadBackup;
    }

    sink.advanceBytes(hdr.saveSize);
    sink.finishFile();
    Logging::info("GBA save backed up: {} bytes from slot with {} saves.", (u32)hdr.saveSize, (u32)hdr.timesSaved);
    return 0;
}

Result GbaSave::restore(FSPXI_Archive arch, FS_Archive sdmc, const std::u16string& srcPath, ProgressSink& sink)
{
    FSStream input(sdmc, srcPath, FS_OPEN_READ);
    if (!input.good()) {
        Logging::error(
            "Failed to open source {} during GBA save restore with result 0x{:08X}.", StringUtils::UTF16toUTF8(srcPath), (u32)input.result());
        return input.result();
    }

    // A bare save is exactly one of the valid GBA save sizes; anything else
    // must parse as an AGBSAVE container (legacy Checkpoint dump), whose
    // newest slot then provides the save.
    u32 srcSize = input.size();
    std::unique_ptr<u8[]> save;
    u32 saveSize = 0;
    Result res   = 0;
    if (validSaveSize(srcSize)) {
        save     = std::make_unique<u8[]>(srcSize);
        saveSize = srcSize;
        if (input.read(save.get(), srcSize) != srcSize || R_FAILED(input.result())) {
            res = R_FAILED(input.result()) ? input.result() : resBadBackup;
            Logging::error("Failed to read GBA save backup {} with result 0x{:08X}.", StringUtils::UTF16toUTF8(srcPath), (u32)res);
        }
    }
    else {
        AgbSaveHeader srcHdr;
        res = extractCurrentSave(input, srcHdr, save);
        if (R_FAILED(res)) {
            Logging::error("Backup {} ({} bytes) is neither a bare GBA save nor an AGBSAVE container (result 0x{:08X}).",
                StringUtils::UTF16toUTF8(srcPath), srcSize, (u32)res);
            res = resBadBackup;
        }
        else {
            saveSize = srcHdr.saveSize;
        }
    }
    input.close();
    if (R_FAILED(res)) {
        return res;
    }

    FSStream output(arch, FS_OPEN_READ | FS_OPEN_WRITE);
    if (!output.good()) {
        Logging::error("Failed to open GBA save for restore with result 0x{:08X}.", (u32)output.result());
        return output.result();
    }

    // The console's own slot header stays: it carries this console's SD CID
    // and title id, and its save size tells whether the backup even fits.
    AgbSaveHeader hdr;
    u32 slotOffset = 0;
    if (!locateCurrentSlot(output, hdr, slotOffset)) {
        output.close();
        Logging::error("GBA save container is uninitialized; launch the game and save once before restoring.");
        return resNoSave;
    }
    if (hdr.saveSize != saveSize) {
        output.close();
        Logging::error("GBA save size mismatch: backup holds {} bytes, title expects {}.", saveSize, (u32)hdr.saveSize);
        return resSizeMismatch;
    }

    size_t slashpos = srcPath.rfind(StringUtils::UTF8toUTF16("/"));
    sink.startFile(srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1), saveSize);

    output.offset(slotOffset + kHeaderSize);
    u32 wt = output.write(save.get(), saveSize);
    if (R_FAILED(output.result()) || wt != saveSize) {
        res = output.result();
        output.close();
        Logging::error("Failed to write GBA save ({} of {} bytes, result 0x{:08X}).", wt, saveSize, (u32)res);
        return R_FAILED(res) ? res : resBadBackup;
    }
    sink.advanceBytes(saveSize);

    // Re-sign the slot for this console, or AGB_FIRM will reject the save on
    // next launch. This is what makes foreign (transferred) backups restorable.
    u8 cmac[0x10] = {0};
    res           = calcSlotCmac(output.pxiFile(), hdr, save.get(), cmac);
    if (R_FAILED(res)) {
        output.close();
        Logging::error("FSPXI_CalcSavegameMAC failed with result 0x{:08X}.", (u32)res);
        return res;
    }
    output.offset(slotOffset + kCmacOffset);
    wt  = output.write(cmac, sizeof(cmac));
    res = output.result();
    output.close();
    if (R_FAILED(res) || wt != sizeof(cmac)) {
        Logging::error("Failed to write GBA save CMAC ({} of {} bytes, result 0x{:08X}).", wt, sizeof(cmac), (u32)res);
        return R_FAILED(res) ? res : resBadBackup;
    }

    sink.finishFile();
    Logging::info("GBA save restored: {} bytes into slot at 0x{:X}, CMAC refreshed.", saveSize, slotOffset);
    return 0;
}

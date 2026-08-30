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

#ifndef DSCARDNAND_HPP
#define DSCARDNAND_HPP

#include "dscard.hpp"
#include "ntrcard.hpp"
#include "progress.hpp"
#include <3ds.h>
#include <3ds/types.h>
#include <string>

// Reading the save out of a DS cartridge that keeps it in on-cart NAND
// (WarioWare D.I.Y. and its siblings) rather than on an SPI EEPROM.
//
// This is not an ordinary save archive. 3DS mode cannot address the cartridge's
// NAND through FS at all, so the bytes are read by driving the NTRCARD
// controller directly while Process9 still owns the slot and polls it roughly
// every 713 ms. Two of those polls with the cartridge in NAND RW mode reset the
// slot, so the read cannot outrun the poll and does not try: it aligns to it.
namespace DSCardNand {
    // Whether this build has the NAND read path compiled in at all. Everything
    // else here is a safe no-op when it returns false.
    bool available(void);

    struct Session {
        bool ready = false;
        bool requiresPowerCycle = false;
        NtrCard::NandSectorReadResult sentinel{};
    };

    // The caller must hold a TitleCatalog::CartScanPause across this call and
    // any dump that follows it.
    Session prepare(FS_MediaType media, const DSCard::NandSave& nandSave, const u8* fsBanner);

    // Why a dump ended. Anything but Ok means no usable image was produced;
    // there is deliberately no "finished with warnings" state.
    enum class DumpStatus {
        Ok,
        NotAvailable,      // build has no NAND read path
        NotReady,          // the Session never passed its gates
        InvalidSaveSize,   // not a whole number of 128-KiB windows
        CartridgeDisturbed,// the cartridge stopped answering as it did at Gate 3
        FileError,         // could not create or write the destination
        OutOfMemory,
        Cancelled,         // the caller's ProgressSink asked to stop
        ReadFailed         // a guard refused the bytes; see the log
    };

    struct DumpOutcome {
        DumpStatus status = DumpStatus::NotAvailable;
        // The cartridge was left in a state that needs a console power cycle
        // before any further card IO. Independent of `status`: a dump can fail
        // safely, and a successful one can still end this way.
        bool requiresPowerCycle = false;
        u32 windowsDone         = 0;
        u32 windows             = 0;
        u32 bytesWritten        = 0;
        // SHA-256 of the image, lowercase hex, NUL-terminated. Set on Ok only.
        char sha256[65] = {0};

        bool ok(void) const { return status == DumpStatus::Ok; }
    };

    // Why a restore ended. Anything but Ok means the cartridge does not hold the
    // image that was asked for; `pagesWritten` says how much of it it does hold.
    enum class RestoreStatus {
        Ok,
        NotAvailable,       // build has no NAND card path
        NotReady,           // the Session never passed its gates
        InvalidSaveSize,    // not a whole number of 128-KiB windows
        SourceSizeMismatch, // the .sav is not exactly the save's size
        OutOfMemory,
        FileError,  // could not open or read the source
        Cancelled,  // the caller's ProgressSink asked to stop
        ReadFailed, // a guard refused the bytes read back
        WriteFailed // a page write did not complete; see the log
    };

    struct RestoreOutcome {
        RestoreStatus status     = RestoreStatus::NotAvailable;
        bool requiresPowerCycle  = false;
        // True once any page has been committed. From this point the cartridge
        // is a mixture of its old contents and the source image, and only
        // finishing the restore or replaying a backup makes it one thing again.
        bool modified   = false;
        u32 windowsDone = 0;
        u32 windows     = 0;
        // Pages the source and the cartridge already agreed on, so nothing was
        // written. A restore of the image that was just backed up writes none.
        u32 pagesSkipped = 0;
        u32 pagesWritten = 0;
        // SHA-256 of the source image, lowercase hex, NUL-terminated.
        char sourceSha256[65] = {0};

        bool ok(void) const { return status == RestoreStatus::Ok; }
    };

    // Writes `path` back to the cartridge's on-cart NAND, one 0x800-byte page at
    // a time. Three things shape it, each one earned on hardware:
    //
    //   - **Only differing pages are written.** Each window is read with the
    //     dump's double-read discipline and compared with the source first, so a
    //     restore of an unmodified image writes nothing at all and a restore of
    //     a changed save writes only what changed. Fewer commits is less risk,
    //     not merely less time.
    //   - **Page 0 is written last, on its own.** Its first sector is the Gate-3
    //     sentinel that every step here brackets itself with, so writing it
    //     earlier would invalidate the very check that guards the rest of the
    //     restore. Deferring it also means an interrupted restore leaves the old
    //     header in place rather than a new header over old data.
    //   - **Every written page is read back twice and compared** before the
    //     restore moves on, for the same reason the dump verifies every burst.
    RestoreOutcome restoreFromFile(
        FS_MediaType media, const DSCard::NandSave& nandSave, const Session& session, const std::u16string& path, ProgressSink& sink);

    // Reads the whole save to `path` on the SD card, one 128-KiB window at a
    // time. Roughly 64 s of card time for 16 MiB: each window waits for a
    // natural Process9 poll, brackets its bursts with the Session's sentinel,
    // and reads every burst twice, accepting only identical copies.
    //
    // Reports through `sink`: one file of `nandSave.saveSize` bytes, advanced
    // per completed window, and `sink.cancelled()` is polled between windows.
    // A cancel stops cleanly, returns the cartridge to ROM mode, and deletes
    // the partial file — a half-image must never look like a backup.
    //
    // The caller must hold a TitleCatalog::CartScanPause, and must have gotten
    // `session` from prepare() in this same launch.
    DumpOutcome dumpToFile(
        FS_MediaType media, const DSCard::NandSave& nandSave, const Session& session, const std::u16string& path, ProgressSink& sink);
}

#endif

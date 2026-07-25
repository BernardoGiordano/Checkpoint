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

#ifndef SCRIPTHOST_HPP
#define SCRIPTHOST_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The seam under the script bindings: everything libctru and libnx genuinely
// disagree about, and nothing else.
//
// The bindings themselves (common/script/checkpoint_api.cpp) are one copy for
// both consoles. What is left here is the catalog's index space and the save
// archive's handle table — the two things that cannot be written once, because
// a 3DS save is an FS archive addressed with UTF-16 paths and a Switch save is
// a directory tree mounted as an fsdev device.
//
// Everything crossing this interface is a plain value, so the implementation
// (3ds/source/script/scripthost.cpp, switch/source/script/scripthost.cpp) is
// the only place a platform type appears. A third adapter that records calls
// is all a headless test of the bindings would need.
//
// Threading: every call runs on the script worker thread, one run at a time.
// Error codes cross as ints and keep their platform meaning: a negative value
// is either one of the small sentinels the <checkpoint.h> API documents or a
// raw platform Result, exactly as the bindings returned them before the split.

struct HostTitle {
    uint64_t id = 0;
    std::string name;
    std::string productCode; // "" where the platform has no such concept
    bool isCart     = false;
    bool hasSave    = false;
    bool hasExtdata = false;
};

struct HostDirEntry {
    std::string name; // leaf name, no path and no trailing slash
    bool folder = false;
};

// Where a console-bound key came from. A sealed blob records the source it was
// made with and asks for that exact one back, so a source that works today and
// stops working after a firmware or CFW change cannot silently derive a
// different key and lock the user out of their own credential.
enum DeviceKeySource : int {
    DeviceKeySourceNone    = 0, // no console binding (passphrase-only blob)
    DeviceKeySourceBest    = 1, // request-only: give me the strongest you have
    DeviceKeySourceCtrNand = 2, // 3DS: NAND CID
    DeviceKeySourceNxSpl   = 3, // Switch: SPL device-unique key derivation
    DeviceKeySourceNxCal   = 4, // Switch: console serial number (fallback)
};

class ScriptHost {
public:
    virtual ~ScriptHost(void) = default;

    // The adapter this build links. Defined per platform.
    static ScriptHost& get(void);

    /* ---- titles: the catalog index space every titles_* binding shares --- */

    virtual int titleCount(void) = 0;

    // False when idx is out of range, leaving `out` untouched; the binding
    // fails the script itself, before any C++ object of its own exists.
    virtual bool titleAt(int idx, HostTitle& out) = 0;

    // Index of the title with this id, -1 if the catalog has none. Separate
    // from titleAt because scanning with it would copy every name and product
    // code out of the catalog to compare one integer — title_find in a script
    // loop would copy the whole catalog per call.
    virtual int titleIndexOf(uint64_t id) = 0;

    // Backup root for the title, without a trailing slash (the binding adds
    // it). "" when the platform has no such backup for that kind — Switch
    // extdata, for one — which the binding hands to the script verbatim.
    virtual std::string titleBackupPath(int idx, int kind) = 0;

    /* ---- save archives: the handle table --------------------------------- */

    // A slot index (0 .. capacity - 1) on success. -1 if the title has no
    // archive of that kind, -2 if the table is full, or a platform Result.
    virtual int savOpen(int titleIdx, int kind) = 0;

    // Console-wide (not title-owned) extdata, keyed by id. -1 where the
    // platform has no such archive.
    virtual int savOpenShared(uint64_t id) = 0;

    virtual bool savValid(int handle) = 0;

    // Reads the whole file into a ScriptHeap block, NUL-terminated one byte
    // past *outSize, and hands it to the script. 0 on success; the caller has
    // already cleared *outBuf/*outSize. -3 = no room for the buffer, -4 = the
    // read came up short (nothing is handed over: a partial file must never
    // reach the script as a whole one), otherwise a platform Result.
    virtual int savRead(int handle, const char* path, char** outBuf, int* outSize) = 0;

    // Create-or-replace: an existing file is dropped first, so a shrinking
    // write never leaves the old tail behind. 0 on success.
    virtual int savWrite(int handle, const char* path, const void* data, size_t size) = 0;

    virtual int savDelete(int handle, const char* path) = 0;

    // Entries of one archive-absolute directory. False if it cannot be
    // listed — the binding answers the script with a null directory.
    virtual bool savList(int handle, const std::string& dir, std::vector<HostDirEntry>& out) = 0;

    virtual int savCommit(int handle) = 0;

    // Lenient by contract: an already-closed or out-of-range handle is a
    // no-op, so a script's cleanup path can close unconditionally.
    virtual void savClose(int handle) = 0;

    // Called after every run whatever the exit path, so a script that forgot
    // sav_close never leaks an open archive into the next one.
    virtual void savCloseAll(void) = 0;

    /* ---- console-bound key material -------------------------------------- */

    // 32 bytes derived from state only the console's own services can answer
    // for — never from anything stored on the SD card. That is the whole
    // property the seal bindings rest on and the exact limit of what they can
    // claim: an SD card read on a PC, or a config folder the user shares, does
    // not contain it, while other homebrew on the same console can ask the same
    // services and is not kept out by it.
    //
    // `want` is DeviceKeySourceBest to take the strongest source this platform
    // has, or a specific DeviceKeySource to reproduce one earlier call's key.
    // Returns the source actually used (> 0) so a caller can record it, or a
    // negative value if that source is unavailable. `out` is untouched on
    // failure. Deliberately not cached: a caller that needs the bytes twice
    // asks twice, so nothing holds console key material alive between runs.
    virtual int deviceSecret(uint8_t* out, int want) = 0;

    // Cryptographically random bytes for salts and nonces. False if the
    // platform could not produce them, which is a hard failure for a caller
    // that was about to encrypt with them.
    virtual bool randomBytes(void* out, size_t size) = 0;

    /* ---- worker thread --------------------------------------------------- */

    // Drops the worker's scheduler priority just below the main thread; see
    // ckpt_script_lower_priority in checkpoint_api.h for why the abort kill
    // switch depends on it. The step and the direction are platform numbers.
    virtual void lowerPriority(void) = 0;
};

#endif

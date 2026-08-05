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

// The Switch side of the script bindings' seam (common/script/scripthost.hpp).
//
// Everything runs on the script worker thread. TitleCatalog reads are safe
// there because MainScreen ignores every catalog-restructuring input while a
// script runs (same gate policy as TransferJob), so no writer can race them.
//
// Semantic differences from the 3DS adapter, dictated by the platform:
// - the catalog index space is the current user's raw title list (account
//   saves plus the BCAT/Device/System singletons appended to it);
// - there is no extdata: kind 1 is rejected by savOpen, HostTitle::hasExtdata
//   is false and titleBackupPath(idx, 1) is "";
// - saves are directory trees mounted as fsdev devices, not FS archives: each
//   handle is its own "scrN:" mount and savCommit is fsdevCommitDevice, with
//   no secure-value business;
// - product codes and game carts do not apply: HostTitle::productCode is ""
//   and HostTitle::isCart is false.

#include "scripthost.hpp"
#include "filesystem.hpp"
#include "logging.hpp"
#include "main.hpp"
#include "scriptheap.hpp"
#include "title.hpp"
#include "titlecatalog.hpp"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mbedtls/sha256.h>
#include <string>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

    // sav_* handle table. Each open slot is its own fsdev mount ("scr0:" ..
    // "scr7:"), so slots never collide with each other or with the "save:"
    // device the backup/restore worker uses (which cannot run concurrently
    // anyway); savCloseAll wipes the table after every run.
    constexpr int MAX_SAV_HANDLES = 8;
    struct SavSlot {
        bool live = false;
        char dev[8]; // "scr0" .. "scr7"
    };

    // One digest of a fixed label plus one blob. The label keeps two sources
    // that happen to hash the same bytes from ever landing on the same key.
    void hashInto(const char* label, const void* data, size_t size, uint8_t* out)
    {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx, 0);
        mbedtls_sha256_update_ret(&ctx, (const unsigned char*)label, strlen(label));
        mbedtls_sha256_update_ret(&ctx, (const unsigned char*)data, size);
        mbedtls_sha256_finish_ret(&ctx, out);
        mbedtls_sha256_free(&ctx);
    }

    class NxScriptHost : public ScriptHost {
    public:
        int titleCount(void) override { return (int)TitleCatalog::get().getTitleCount(g_currentUId); }

        bool titleAt(int idx, HostTitle& out) override
        {
            if (idx < 0 || idx >= titleCount()) {
                return false;
            }
            Title title;
            TitleCatalog::get().getTitle(title, g_currentUId, (size_t)idx);
            out.id   = title.id();
            out.name = title.displayName();
            // Every catalog entry on Switch *is* an installed save.
            out.hasSave = true;
            return true;
        }

        int titleIndexOf(uint64_t id) override { return TitleCatalog::get().indexById(g_currentUId, (u64)id); }

        std::string titleBackupPath(int idx, int kind) override
        {
            if (kind != 0 || idx < 0 || idx >= titleCount()) {
                return "";
            }
            Title title;
            TitleCatalog::get().getTitle(title, g_currentUId, (size_t)idx);
            return title.path();
        }

        int savOpen(int titleIdx, int kind) override
        {
            if (kind != 0 || titleIdx < 0 || titleIdx >= titleCount()) {
                return -1; // no extdata on Switch
            }
            const int slot = freeSlot();
            if (slot < 0) {
                return -2;
            }

            Title title;
            TitleCatalog::get().getTitle(title, g_currentUId, (size_t)titleIdx);

            // Mount the save on the slot's own device name (the SaveDataSource::mount
            // path hardcodes "save:", which belongs to the backup/restore worker).
            FsFileSystem fileSystem;
            Result res = 0;
            switch (title.saveDataType()) {
                case FsSaveDataType_Bcat:
                    res = FileSystem::mountBcatSave(&fileSystem, title.id());
                    break;
                case FsSaveDataType_Device:
                    res = FileSystem::mountDeviceSave(&fileSystem, title.id());
                    break;
                case FsSaveDataType_System:
                    res = FileSystem::mountSystemSave(&fileSystem, title.id(), title.saveDataSpaceId());
                    break;
                default:
                    res = FileSystem::mountSave(&fileSystem, title.id(), title.userId());
                    break;
            }
            if (R_FAILED(res)) {
                return (int)res;
            }

            snprintf(mSlots[slot].dev, sizeof(mSlots[slot].dev), "scr%d", slot);
            if (fsdevMountDevice(mSlots[slot].dev, fileSystem) == -1) {
                fsFsClose(&fileSystem);
                return -1;
            }

            mSlots[slot].live = true;
            return slot;
        }

        // Shared extdata is a 3DS concept (e.g. the Home Menu Play Coin
        // archive); the Switch has no console-wide extdata, so this is always
        // unsupported.
        int savOpenShared(uint64_t) override { return -1; }

        bool savValid(int handle) override { return handle >= 0 && handle < MAX_SAV_HANDLES && mSlots[handle].live; }

        int savRead(int handle, const char* path, char** outBuf, int* outSize) override
        {
            FILE* f = fopen(devPath(handle, path).c_str(), "rb");
            if (!f) {
                return -1;
            }

            fseek(f, 0, SEEK_END);
            const long size = ftell(f);
            rewind(f);
            if (size < 0) {
                fclose(f);
                return -1;
            }

            char* buf = (char*)ScriptHeap::get().alloc((size_t)size + 1);
            if (!buf) {
                fclose(f);
                return -3;
            }

            const size_t read = fread(buf, 1, (size_t)size, f);
            fclose(f);
            if (read != (size_t)size) {
                // Same rule as the 3DS adapter: a short read never reaches the
                // script as a whole file.
                Logging::warning("[script] sav_read '{}': {} of {} bytes", path, read, size);
                ScriptHeap::get().release(buf);
                return -4;
            }

            buf[read] = '\0';
            *outBuf   = buf;
            *outSize  = (int)read;
            return 0;
        }

        int savWrite(int handle, const char* path, const void* data, size_t size) override
        {
            // Create/replace: drop any existing file first so a shrinking write
            // can't leave the old tail behind (fsdev truncates on "wb", but be
            // explicit and symmetric with the 3DS adapter).
            const std::string p = devPath(handle, path);
            remove(p.c_str());

            FILE* f = fopen(p.c_str(), "wb");
            if (!f) {
                return -1;
            }

            const size_t written = size > 0 ? fwrite(data, 1, size, f) : 0;
            fclose(f);
            return written == size ? 0 : -3;
        }

        int savDelete(int handle, const char* path) override { return remove(devPath(handle, path).c_str()) == 0 ? 0 : -1; }

        // A save is an fsdev mount here, so the folder calls are the ordinary
        // POSIX ones on the slot's device. rmdir is non-recursive by contract,
        // which is also all fsdev offers.
        int savMkdir(int handle, const char* path) override { return mkdir(devPath(handle, path).c_str(), 0777) == 0 ? 0 : -1; }

        int savRmdir(int handle, const char* path) override { return rmdir(devPath(handle, path).c_str()) == 0 ? 0 : -1; }

        int savRename(int handle, const char* from, const char* to) override
        {
            const std::string src = devPath(handle, from);
            return rename(src.c_str(), devPath(handle, to).c_str()) == 0 ? 0 : -1;
        }

        bool savList(int handle, const std::string& dir, std::vector<HostDirEntry>& out) override
        {
            DIR* d = opendir((std::string(mSlots[handle].dev) + ":" + dir).c_str());
            if (!d) {
                return false;
            }
            while (struct dirent* ent = readdir(d)) {
                const std::string name = ent->d_name;
                if (name != "." && name != "..") {
                    out.push_back({name, ent->d_type == DT_DIR});
                }
            }
            closedir(d);
            return true;
        }

        int savCommit(int handle) override { return (int)fsdevCommitDevice(mSlots[handle].dev); }

        void savClose(int handle) override
        {
            if (handle >= 0 && handle < MAX_SAV_HANDLES) {
                unmount(mSlots[handle]);
            }
        }

        void savCloseAll(void) override
        {
            for (int i = 0; i < MAX_SAV_HANDLES; i++) {
                unmount(mSlots[i]);
            }
        }

        int deviceSecret(uint8_t* out, int want) override
        {
            if (want == DeviceKeySourceBest || want == DeviceKeySourceNxSpl) {
                if (splSecret(out)) {
                    return DeviceKeySourceNxSpl;
                }
                if (want == DeviceKeySourceNxSpl) {
                    return -1;
                }
                Logging::warning("script seal: SPL device key unavailable, falling back to the serial number");
            }
            if (want == DeviceKeySourceBest || want == DeviceKeySourceNxCal) {
                if (serialSecret(out)) {
                    return DeviceKeySourceNxCal;
                }
            }
            return -1;
        }

        bool randomBytes(void* out, size_t size) override
        {
            // libnx's randomGet is the kernel CSPRNG; it has no failure mode.
            randomGet(out, size);
            return true;
        }

        void lowerPriority(void) override
        {
            // The worker was spawned at the main thread's priority (0x2C); +2 puts
            // it a step below so the UI thread always preempts to sample the hold-B
            // abort, even against a syscall-free compute loop that never yields the
            // core.
            s32 prio = 0;
            if (R_SUCCEEDED(svcGetThreadPriority(&prio, CUR_THREAD_HANDLE))) {
                svcSetThreadPriority(CUR_THREAD_HANDLE, (u32)(prio + 2));
            }
        }

    private:
        // The security engine's device-unique key derivation: the two constants
        // below are public domain-separation labels (they are in this binary and
        // in this repository), and the console-unique part is the master key the
        // engine mixes in, which never leaves the engine. Generation 0 so a
        // firmware update does not re-derive a different key under us.
        bool splSecret(uint8_t* out)
        {
            static const u8 kWrappedKek[16] = "CheckpointSeal1";
            static const u8 kWrappedKey[16] = "CheckpointKey_1";
            if (R_FAILED(splCryptoInitialize())) {
                return false;
            }
            u8 sealedKek[16] = {0};
            u8 key[16]       = {0};
            bool ok          = R_SUCCEEDED(splCryptoGenerateAesKek(kWrappedKek, 0, 1 /* device unique */, sealedKek)) &&
                      R_SUCCEEDED(splCryptoGenerateAesKey(sealedKek, kWrappedKey, key));
            splCryptoExit();
            if (ok) {
                hashInto("Checkpoint NX SPL device key v1", key, sizeof(key), out);
            }
            memset(sealedKek, 0, sizeof(sealedKek));
            memset(key, 0, sizeof(key));
            return ok;
        }

        // Fallback only. The serial number is console-unique and is not on the SD
        // card, which is the whole claim the seal makes — but unlike the SPL key
        // it is printed on the console and its box, so a blob sealed with this
        // source is weaker. Used when SPL is unreachable, rather than refusing to
        // store a credential at all.
        bool serialSecret(uint8_t* out)
        {
            if (R_FAILED(setsysInitialize())) {
                return false;
            }
            SetSysSerialNumber serial = {};
            const bool ok             = R_SUCCEEDED(setsysGetSerialNumber(&serial));
            setsysExit();
            if (ok) {
                hashInto("Checkpoint NX serial v1", serial.number, sizeof(serial.number), out);
            }
            memset(&serial, 0, sizeof(serial));
            return ok;
        }

        int freeSlot(void) const
        {
            for (int i = 0; i < MAX_SAV_HANDLES; i++) {
                if (!mSlots[i].live) {
                    return i;
                }
            }
            return -1;
        }

        static void unmount(SavSlot& slot)
        {
            if (slot.live) {
                fsdevUnmountDevice(slot.dev);
                slot.live = false;
            }
        }

        // Script paths are archive-absolute ("/file.bin"); build the full stdio
        // path on the slot's device, tolerating a missing leading slash.
        std::string devPath(int handle, const char* path) const
        {
            std::string p = path;
            if (p.empty() || p[0] != '/') {
                p = "/" + p;
            }
            return std::string(mSlots[handle].dev) + ":" + p;
        }

        SavSlot mSlots[MAX_SAV_HANDLES];
    };
}

ScriptHost& ScriptHost::get(void)
{
    // Deliberately never destroyed, like the 3DS adapter: the slots hold fsdev
    // mounts, and a static's destructor runs after the filesystem is torn down.
    // ScriptRunner's ckpt_sav_close_all() empties the table after every run
    // whatever the exit path, so there is nothing left to unmount anyway.
    static NxScriptHost* host = new NxScriptHost();
    return *host;
}

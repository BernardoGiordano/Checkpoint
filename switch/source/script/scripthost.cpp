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
#include "main.hpp"
#include "scriptheap.hpp"
#include "title.hpp"
#include "titlecatalog.hpp"
#include <cstdio>
#include <dirent.h>
#include <string>
#include <switch.h>
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
                ScriptHeap::get().release(buf);
                return -1;
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
    // The slots hold fsdev mounts, which nothing unmounts on the way out of the
    // app: ScriptRunner calls ckpt_sav_close_all() after every run, whatever
    // the exit path, so the table is empty long before this instance dies.
    static NxScriptHost host;
    return host;
}

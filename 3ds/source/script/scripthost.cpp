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

// The 3DS side of the script bindings' seam (common/script/scripthost.hpp).
//
// The catalog index space is the Save list; a save handle is an open FS archive
// addressed with UTF-16 paths. Everything runs on the script worker thread:
// TitleCatalog reads are safe there (recursive mutex since arch-review S4), and
// nothing here may trigger a catalog refresh.

#include "scripthost.hpp"
#include "backuptarget.hpp"
#include "common.hpp"
#include "directory.hpp"
#include "fsstream.hpp"
#include "loader.hpp"
#include "scriptheap.hpp"
#include "title.hpp"
#include <3ds.h>
#include <string>
#include <vector>

namespace {

    // sav_* handle table. Slots hold the opened archive plus what commit means
    // for it; savCloseAll wipes it after every run.
    constexpr int MAX_SAV_HANDLES = 8;
    struct SavSlot {
        ArchiveHandle arch;
        bool commitable = false; // CTR save archive: commit + secure-value fix
        u32 uniqueId    = 0;     // for the secure-value fix
    };

    // Script paths are archive-absolute; tolerate a missing leading slash.
    std::u16string archivePath(const char* path)
    {
        std::string p = path;
        if (p.empty() || p[0] != '/') {
            p = "/" + p;
        }
        return StringUtils::UTF8toUTF16(p.c_str());
    }

    class CtrScriptHost : public ScriptHost {
    public:
        int titleCount(void) override { return TitleCatalog::get().getTitleCount(BackupKind::Save); }

        bool titleAt(int idx, HostTitle& out) override
        {
            if (idx < 0 || idx >= titleCount()) {
                return false;
            }
            Title title;
            TitleCatalog::get().getTitle(title, idx, BackupKind::Save);
            out.id          = title.id();
            out.name        = title.longDescription();
            out.productCode = title.productCode;
            out.isCart      = title.mediaType() == MEDIATYPE_GAME_CARD;
            out.hasSave     = title.accessibleSave();
            out.hasExtdata  = title.accessibleExtdata();
            return true;
        }

        std::string titleBackupPath(int idx, int kind) override
        {
            if (idx < 0 || idx >= titleCount()) {
                return "";
            }
            Title title;
            TitleCatalog::get().getTitle(title, idx, BackupKind::Save);
            BackupTarget target = title.backup(kind == 0 ? BackupKind::Save : BackupKind::Extdata);
            return StringUtils::UTF16toUTF8(target.rootPath());
        }

        int savOpen(int titleIdx, int kind) override
        {
            if (titleIdx < 0 || titleIdx >= titleCount()) {
                return -1;
            }
            const int slot = freeSlot();
            if (slot < 0) {
                return -2;
            }

            Title title;
            TitleCatalog::get().getTitle(title, titleIdx, BackupKind::Save);

            // Only regular CTR saves and extdata are file-level archives; GBA VC
            // (FSPXI raw), DSiWare (TWL FAT) and SPI cart saves are not reachable here.
            if (kind == 0) {
                const bool spiCart = title.mediaType() == MEDIATYPE_GAME_CARD && title.cardType() != CARD_CTR;
                if (!title.accessibleSave() || title.isGBAVC() || title.isDSiWare() || spiCart) {
                    return -1;
                }
            }
            else if (!title.accessibleExtdata()) {
                return -1;
            }

            Result res           = 0;
            ArchiveHandle handle = title.backup(kind == 0 ? BackupKind::Save : BackupKind::Extdata).open(res);
            if (!handle) {
                return R_FAILED(res) ? (int)res : -1;
            }

            mSlots[slot].arch       = std::move(handle);
            mSlots[slot].commitable = kind == 0;
            mSlots[slot].uniqueId   = title.uniqueId();
            return slot;
        }

        int savOpenShared(uint64_t id) override
        {
            // The id's low 32 bits are the extdata id, its high 32 the archive
            // magic (0x00048000 for the Home Menu shared extdata that holds
            // Play Coins).
            const int slot = freeSlot();
            if (slot < 0) {
                return -2;
            }

            FS_Archive archive;
            const u32 path[3] = {MEDIATYPE_NAND, (u32)id, (u32)(id >> 32)};
            Result res        = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, {PATH_BINARY, 0xC, path});
            if (R_FAILED(res)) {
                return (int)res;
            }

            // Shared extdata is a file-level archive like ordinary extdata: it needs
            // no commit and no secure-value fix, so savCommit is a no-op on this
            // handle.
            mSlots[slot].arch       = ArchiveHandle::fromFs(archive);
            mSlots[slot].commitable = false;
            mSlots[slot].uniqueId   = 0;
            return slot;
        }

        bool savValid(int handle) override { return handle >= 0 && handle < MAX_SAV_HANDLES && mSlots[handle].arch; }

        int savRead(int handle, const char* path, char** outBuf, int* outSize) override
        {
            FSStream stream(mSlots[handle].arch.fs(), archivePath(path), FS_OPEN_READ);
            if (!stream.good()) {
                const Result res = stream.result();
                stream.close();
                return (int)res;
            }

            const u32 size = stream.size();
            char* buf      = (char*)ScriptHeap::get().alloc(size + 1);
            if (!buf) {
                stream.close();
                return -3;
            }

            const u32 read   = stream.read(buf, size);
            const Result res = stream.result();
            stream.close();
            if (read != size && R_FAILED(res)) {
                ScriptHeap::get().release(buf);
                return (int)res;
            }

            buf[read] = '\0';
            *outBuf   = buf;
            *outSize  = (int)read;
            return 0;
        }

        int savWrite(int handle, const char* path, const void* data, size_t size) override
        {
            // Create/replace: the create-on-open path keeps an existing file's size,
            // so drop it first (a missing file fails harmlessly).
            const std::u16string p = archivePath(path);
            FSUSER_DeleteFile(mSlots[handle].arch.fs(), fsMakePath(PATH_UTF16, p.data()));

            FSStream stream(mSlots[handle].arch.fs(), p, FS_OPEN_WRITE, (u32)size);
            if (!stream.good()) {
                const Result res = stream.result();
                stream.close();
                return (int)res;
            }

            const u32 written = stream.write(data, (u32)size);
            const Result res  = stream.result();
            stream.close();
            return written == (u32)size ? 0 : (R_FAILED(res) ? (int)res : -3);
        }

        int savDelete(int handle, const char* path) override
        {
            const std::u16string p = archivePath(path);
            return (int)FSUSER_DeleteFile(mSlots[handle].arch.fs(), fsMakePath(PATH_UTF16, p.data()));
        }

        bool savList(int handle, const std::string& dir, std::vector<HostDirEntry>& out) override
        {
            Directory items(mSlots[handle].arch.fs(), StringUtils::UTF8toUTF16(dir.c_str()));
            if (!items.good()) {
                return false;
            }
            for (size_t i = 0, sz = items.size(); i < sz; i++) {
                out.push_back({StringUtils::UTF16toUTF8(items.entry(i)), items.folder(i)});
            }
            return true;
        }

        int savCommit(int handle) override
        {
            if (!mSlots[handle].commitable) {
                return 0; // extdata needs no commit
            }

            Result res = FSUSER_ControlArchive(mSlots[handle].arch.fs(), ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
            if (R_SUCCEEDED(res)) {
                // Same epilogue as restore: drop the secure value so the game accepts
                // the modified save instead of flagging a rollback.
                u8 out;
                u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (mSlots[handle].uniqueId << 8);
                res             = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
            }
            return (int)res;
        }

        void savClose(int handle) override
        {
            if (handle >= 0 && handle < MAX_SAV_HANDLES) {
                mSlots[handle] = SavSlot{};
            }
        }

        void savCloseAll(void) override
        {
            for (int i = 0; i < MAX_SAV_HANDLES; i++) {
                mSlots[i] = SavSlot{};
            }
        }

        void lowerPriority(void) override
        {
            // The thread pool spawns the worker one step ABOVE the main thread
            // (prio-1), so a syscall-free compute loop would starve the UI thread
            // outright; +2 lands it just below main, letting the UI thread always
            // preempt to sample the hold-B abort.
            s32 prio = 0;
            if (R_SUCCEEDED(svcGetThreadPriority(&prio, CUR_THREAD_HANDLE))) {
                svcSetThreadPriority(CUR_THREAD_HANDLE, prio + 2);
            }
        }

    private:
        int freeSlot(void) const
        {
            for (int i = 0; i < MAX_SAV_HANDLES; i++) {
                if (!mSlots[i].arch) {
                    return i;
                }
            }
            return -1;
        }

        SavSlot mSlots[MAX_SAV_HANDLES];
    };
}

ScriptHost& ScriptHost::get(void)
{
    // The slots hold FS archive handles and this outlives servicesExit(), so
    // its destructor must never find an open one. That holds because
    // ScriptRunner calls ckpt_sav_close_all() after every run, whatever the
    // exit path — not because the table happens to be empty.
    static CtrScriptHost host;
    return host;
}

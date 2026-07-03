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

#ifndef MAINSCREEN_HPP
#define MAINSCREEN_HPP

#include "BackupList.hpp"
#include "ErrorOverlay.hpp"
#include "InfoOverlay.hpp"
#include "Screen.hpp"
#include "TransferOverlay.hpp"
#include "YesNoOverlay.hpp"
#include "archive.hpp"
#include "clickable.hpp"
#include "gui.hpp"
#include "multiselection.hpp"
#include "thread.hpp"
#include "transferstatus.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

class Title;

// v4 redesign of the main page: top = title library grid, bottom = title detail
// (backups list + actions). The backend wiring mirrors MainScreen exactly; only
// the presentation differs. Built as a parallel Screen so both can coexist while
// the redesign is wired up gradually.
class MainScreen : public Screen {
public:
    MainScreen(void);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;

protected:
    int cellX(size_t i) const;
    int cellY(size_t i) const;
    void drawSelector(void) const;
    void drawTile(size_t k) const;
    void handleEvents(const InputState& input);
    void updateSelector(void);
    void updateButtons(void);
    void refreshTitlesFull(void);
    std::string nameFromCell(size_t index) const;
    void startTransferSend(void);
    // Rebuilds the SelectedTitle snapshot (and the grid favorite pips) when the
    // selection, backup kind, catalog generation, or size-cache generation moved
    // since the last frame; no-op otherwise. Also rebuilds directoryList's rows.
    void refreshSelected(void);
    void doBackup(size_t fullIndex, size_t cellIndex);
    void doRestore(size_t fullIndex, size_t cellIndex);
    // Runs a restore of the current title's cellIndex backup, gated by the
    // "Confirm before restore" setting: shows a Yes/No prompt when enabled,
    // otherwise restores immediately.
    void requestRestore(size_t cellIndex);

private:
    Hid<HidDirection::HORIZONTAL, HidDirection::VERTICAL> hid;
    std::unique_ptr<Clickable> buttonBackup, buttonRestore, buttonPlayCoins, buttonTransfer;
    std::unique_ptr<Clickable> buttonBackupAL, buttonRestoreAL; // narrower Backup/Restore laid out alongside Coins on Activity Log
    std::unique_ptr<Clickable> buttonBackupAll;                 // full-width batch Backup shown in multi-select, replacing the two action buttons
    std::unique_ptr<BackupList> directoryList;
    std::string ver;

    C2D_ImageTint flagTint;     // teal brand mark
    C2D_ImageTint checkboxTint; // dark check on the multi-select badge
    C2D_ImageTint starTint;     // dark star on the gold favorite pip
    int selectionTimer;
    int refreshTimer;
    bool transferEnabled;
    BackupKind backupKind = BackupKind::Save;

    // Value snapshot of everything the detail card (and the grid favorite pips)
    // needs, rebuilt by refreshSelected() in update() only when its inputs move.
    // draw*() read this and never query TitleCatalog/BackupSizeCache themselves,
    // so drawing takes no locks and copies no Title.
    struct SelectedTitle {
        bool valid       = false;
        size_t fullIndex = 0;
        BackupKind kind  = BackupKind::Save;
        u32 catalogGen   = 0; // TitleCatalog::generation() at snapshot time
        u32 sizeGen      = 0; // BackupSizeCache::generation() at snapshot time
        u64 id           = 0;
        std::u16string rootPath; // backup root, for the async size-walk request
        std::string name;        // shortDescription
        std::string cartId;      // productCode or "System title"
        std::string mediaType;
        bool favorite      = false;
        bool activityLog   = false;
        size_t backupCount = 0;       // existing backups (entry 0 "New..." excluded)
        std::optional<u64> totalSize; // async total; nullopt while computing
    };
    SelectedTitle selected;
    std::vector<u8> gridFavorites; // favorite pip per title of the current kind

    // Live transfer state, snapshotted once per frame in update() so drawTop and
    // drawBottom don't each take the TransferStatus lock and copy the snapshot.
    TransferSnapshot mTransfer;
};

#endif

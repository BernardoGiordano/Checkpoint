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

#ifndef MAINSCREENV4_HPP
#define MAINSCREENV4_HPP

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
#include <algorithm>
#include <memory>
#include <tuple>

class Title;

// v4 redesign of the main page: top = title library grid, bottom = title detail
// (backups list + actions). The backend wiring mirrors MainScreen exactly; only
// the presentation differs. Built as a parallel Screen so both can coexist while
// the redesign is wired up gradually.
class MainScreenV4 : public Screen {
public:
    MainScreenV4(void);
    ~MainScreenV4(void);
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
    char ver[10];

    C2D_TextBuf dynamicBuf;

    C2D_ImageTint flagTint;     // teal brand mark
    C2D_ImageTint checkboxTint; // dark check on the multi-select badge
    C2D_ImageTint starTint;     // dark star on the gold favorite pip
    int selectionTimer;
    int refreshTimer;
    bool transferEnabled;
    BackupKind backupKind = BackupKind::Save;

    // The drawn title's identity + backup root, stashed by drawBottom so update()
    // can ask BackupSizeCache for its size without copying a Title again. The
    // async cache owns the SD walk and the memoized results.
    mutable u64 selectedId = 0;
    mutable std::u16string selectedRoot;
    mutable bool selectedValid = false;
};

#endif

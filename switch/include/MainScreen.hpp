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
#include "InputState.hpp"
#include "Screen.hpp"
#include "YesNoOverlay.hpp"
#include "clickable.hpp"
#include "io.hpp"
#include "main.hpp"
#include "multiselection.hpp"
#include "savekind.hpp"
#include "title.hpp"
#include "transferstatus.hpp"
#include <array>
#include <tuple>

typedef enum { TITLES, CELLS } entryType_t;

class Clickable;

class MainScreen : public Screen {
public:
    MainScreen(const InputState&);
    void draw(void) const override;
    void update(const InputState& input) override;

protected:
    int selectorX(size_t i) const;
    int selectorY(size_t i) const;
    // Vertical-scroll grid cursor: D-pad moves the cursor over the full
    // filtered list (up/down by a row, left/right by one) with held-key
    // repeat; the 3-row window scrolls to keep the cursor row fully visible.
    void updateGridCursor(const InputState& input, size_t count);
    // Clamp mScrollRow so the cursor row is inside the 3 fully visible rows
    // (and never past the end of the list).
    void scrollToCursor(size_t count);
    void updateSelector(const InputState& input);
    void handleEvents(const InputState& input);
    std::string nameFromCell(size_t index) const;
    void entryType(entryType_t type);
    size_t index(entryType_t type) const;
    void index(entryType_t type, size_t i);
    void resetIndex(entryType_t type);
    void setSaveTypeFilter(saveTypeFilter_t filter);
    size_t rawIndex(void) const;
    void doBackup(size_t rawIdx, size_t cellIndex);
    void doRestore(size_t rawIdx, size_t cellIndex);
    // Restore the currently selected backup: asks for a YesNo confirmation first
    // unless Settings' confirm-restore toggle is off, then enqueues + starts.
    void requestRestoreSelected(void);
    // Wireless transfer entry points (gated behind the transfer setting). Send is
    // contextual (an existing backup must be selected); Receive is global.
    void startTransferSend(void);
    void startTransferReceive(void);
    // Scans the script folders and raises the picker (Scripts action, R3).
    void startScriptPicker(void);
    // While a script runs: raises the overlay for a pending ScriptUiBridge
    // request (or answers Keyboard inline via swkbd).
    void pumpScriptRequests(void);

private:
    entryType_t type;
    int selectionTimer;
    bool sidebarFocused              = false;
    bool sidebarExitFrame            = false;
    bool backupScrollEnabled         = false;
    int sidebarCursor                = 0;
    saveTypeFilter_t mSaveTypeFilter = FILTER_SAVES;
    // Last catalog generation the selector reconciled against; a bump means
    // titles were hidden/shown and the cursor/selection must be clamped.
    u32 mLastGeneration = 0;
    // Grid cursor over the full filtered title list (not page-local) and the
    // first fully visible row of the scrolled 5-column grid.
    size_t mCursor    = 0;
    int mScrollRow    = 0;
    u64 mLastMoveTick = 0; // held-D-pad repeat timing
    std::unique_ptr<BackupList> backupList;
    std::unique_ptr<Clickable> buttonBackup, buttonRestore;
    // Wireless Send touch button (drawn/handled only when the transfer setting is
    // on), stacked above Backup and lit only when an existing backup is selected.
    // Receive is a row inside the backup list, not a button.
    std::unique_ptr<Clickable> buttonSend;
    // Frames B has been held to cancel an in-flight network send (parity with the
    // 45-frame threshold used by ReceiveOverlay).
    int mCancelHoldFrames = 0;
    // Save-kind rail buttons in UI order, indexed by saveTypeFilter_t. Kept as
    // Clickables purely for their stateful touch-release hit-testing; the rail
    // look is drawn by hand, not through Clickable::draw.
    std::array<std::unique_ptr<Clickable>, 4> filterButtons;
    // Rail scripts button, touch shortcut to the script picker; stacked directly
    // above the gear.
    std::unique_ptr<Clickable> scriptButton;
    // Rail gear button, touch shortcut to Settings; also opened by Minus.
    std::unique_ptr<Clickable> settingsButton;
    // Account avatar, touch shortcut to the account picker. Release-triggered so
    // the picker applet can't relaunch every frame the finger stays down.
    std::unique_ptr<Clickable> avatarButton;
    char ver[16];
};

#endif

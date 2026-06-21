/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#include "CheatManagerOverlay.hpp"
#include "ErrorOverlay.hpp"
#include "InfoOverlay.hpp"
#include "Screen.hpp"
#include "TransferOverlay.hpp"
#include "YesNoOverlay.hpp"
#include "archive.hpp"
#include "clickable.hpp"
#include "gui.hpp"
#include "multiselection.hpp"
#include "scrollable.hpp"
#include "thread.hpp"
#include <algorithm>
#include <memory>
#include <tuple>

class MainScreen : public Screen {
public:
    MainScreen(void);
    ~MainScreen(void);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;

protected:
    int selectorX(size_t i) const;
    int selectorY(size_t i) const;
    void drawSelector(void) const;
    void handleEvents(const InputState& input);
    void updateSelector(void);
    void updateButtons(void);
    void refreshTitlesFull(void);
    std::string nameFromCell(size_t index) const;
    void startTransferSend(void);
    // Resolve the title, pick the destination/source path (prompting the keyboard
    // for a new backup folder), run the io operation and raise the result overlay.
    void doBackup(size_t fullIndex, size_t cellIndex);
    void doRestore(size_t fullIndex, size_t cellIndex);

private:
    Hid<HidDirection::HORIZONTAL, HidDirection::VERTICAL> hid;
    std::unique_ptr<Clickable> buttonBackup, buttonRestore, buttonCheats, buttonPlayCoins, buttonTransfer;
    std::unique_ptr<Scrollable> directoryList;
    char ver[10];

    C2D_Text ins1, ins2, ins3, ins4, c2dId, c2dMediatype;
    C2D_Text checkpoint, version;
    // instructions text
    C2D_Text top_move, top_a, top_y, top_my, top_b, top_hb, bot_ts, bot_x, coins;
    C2D_TextBuf dynamicBuf, staticBuf;

    const float scaleInst = 0.7f;
    C2D_ImageTint checkboxTint;
    C2D_ImageTint flagTint;
    int selectionTimer;
    int refreshTimer;
    bool transferEnabled;
    // Which backup facet the UI is currently showing. The single owner of this
    // selection (there is no global mode flag); toggled by the user and passed
    // down to the loader and io layers explicitly.
    BackupKind backupKind = BackupKind::Save;

    // Multi-select batch progress: how many of the selected saves have been
    // processed (multiSelectCount) out of the total (multiSelectTotal). Owned
    // here because the batch loop and the progress modal both live on this
    // screen and run single-threaded on the main thread.
    size_t multiSelectCount = 0;
    size_t multiSelectTotal = 0;
};

#endif

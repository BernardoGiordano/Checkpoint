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

#ifndef SETTINGSSCREEN_HPP
#define SETTINGSSCREEN_HPP

#include "Screen.hpp"
#include <functional>
#include <memory>
#include <string>
#include <switch.h>
#include <vector>

// Local-only settings surface. Every change writes config.json synchronously,
// there is no save button. Reached by Minus from MainScreen and returns to the
// exact MainScreen instance it came from (state preserved) on B — see the
// g_pendingScreen swap in main().
class SettingsScreen : public Screen {
public:
    // The category-rail entries, switched by L/R.
    enum class Category { General, Library, SaveFolders, Connectivity, Logs, About, COUNT };

    // One control per row. Segmented (Theme), Spinner (Default sort), Toggle
    // (FTP/PKSM); ActionPill/Static are used by the later-phase tabs.
    enum class Control { Segmented, Spinner, Toggle, ActionPill, Static };

    // A single settings row. `options`/`getIndex` back Segmented+Spinner;
    // `getOn` backs Toggle; `onCycle(delta)` handles left/right on a
    // Spinner/Segmented; `onActivate` handles A (toggle flip, pill press).
    // `section` (when non-empty) prints a section label above the row.
    struct Row {
        std::string title;
        std::string subtitle;
        Control control = Control::Static;
        std::string section;
        std::vector<std::string> options;
        std::function<int()> getIndex;
        std::function<bool()> getOn;
        std::function<void(int)> onCycle;
        std::function<void()> onActivate;
        // Optional status text drawn in `success` after the subtitle (e.g.
        // FTP's "· running on 192.168.1.34:50000"); empty return = nothing.
        std::function<std::string()> statusSuffix;
        // ActionPill label (Library "Unhide"/"Remove", Save folders "Remove").
        std::string pillLabel;
        // Leading 34px title icon (Library/Save folders rows); 0 = no icon.
        u64 iconId     = 0;
        bool focusable = true;
    };

    explicit SettingsScreen(std::shared_ptr<Screen> returnTo);
    void draw(void) const override;
    void update(const InputState& input) override;

private:
    void rebuildRows(void);
    // Snapshot the in-memory log, wrap each line to the pane width (mono font),
    // and cache the display lines; scrolls to the newest line. Called when the
    // Logs category is (re)built.
    void rebuildLogLines(void);
    // Number of log lines that fit the pane at once (for paging/clamping).
    int logLinesPerPage(void) const;
    // Draws the scrollable log pane for the Logs category.
    void drawLogs(void) const;
    void switchCategory(int delta);
    // Flashes the config-path label text-3 -> success -> text-3 on a write.
    void flashSaved(void);
    // Move the cursor to the next focusable row in `dir` (+1/-1), wrapping.
    void moveCursor(int dir);
    // Whether the current category has at least one selectable row.
    bool hasFocusableRow(void) const;
    // Last row index that still fits in the viewport when the list is scrolled
    // so `scroll` is the first drawn row (accounts for section labels). Used to
    // page long lists (Library / Save folders) instead of letting rows spill
    // past the hint bar where they can't be reached.
    int lastVisibleRow(int scroll) const;
    // The section row `i` belongs to: its own `section`, or the nearest one set
    // on an earlier row (group headers are only stored on the first row of a
    // group). Empty if none. Backs the sticky top-of-list header.
    const std::string& sectionAt(int i) const;
    // Nudge mScroll so mCursor is on screen (called after the cursor moves or
    // the row set is rebuilt).
    void ensureCursorVisible(void);

    // The MainScreen we came from, kept alive so B restores its exact state.
    std::shared_ptr<Screen> mReturnTo;
    Category mCategory = Category::General;
    std::vector<Row> mRows;
    int mCursor     = 0;
    int mScroll     = 0; // index of the first drawn row (long-list paging)
    int mFlashTimer = 0;
    // Master-detail focus (like the Switch's own Settings app): true = the
    // category rail owns the cursor (Up/Down pick a category, Right/A enters the
    // rows), false = a row is focused. Starts on the rail so every category is
    // reachable with the d-pad alone.
    bool mCatFocused = true;
    // Set by an action that changes row membership (Library unhide, Save-folder
    // add/remove); update() rebuilds at the top of the next frame so it never
    // dereferences a Row the action just invalidated.
    bool mNeedsRebuild = false;
    char mVer[8];

    // Logs category: wrapped display lines of the in-memory log, the first drawn
    // line, and a d-pad auto-repeat frame counter for held scrolling.
    std::vector<std::string> mLogLines;
    int mLogScroll    = 0;
    int mLogHeldTimer = 0;
};

#endif // SETTINGSSCREEN_HPP

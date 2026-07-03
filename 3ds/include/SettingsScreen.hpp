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
#include "hid.hpp"
#include <citro2d.h>
#include <memory>

// v4 Settings page. Top screen = section navigation (a vertical list of
// sections) plus a short blurb; bottom screen = the selected section's content.
//
//   General  toggles that map 1:1 to config.json (interactive)
//   Library  favorites + hidden (filter) titles, with remove (interactive)
//   Folders  per-title extra save / extdata folders, with remove (interactive)
//   Network  Wi-Fi transfer status + this console's address (informational)
//   About    version, credits, storage usage (informational)
//
// Focus model mirrors the main page: the section list on top holds focus first
// (Up/Down to pick a section, A to enter an interactive section, B to leave to
// the parent screen). Entering moves focus to the content on the bottom (Up/Down
// to move the row cursor, A to toggle / X to remove, B back to the section list).
//
// Opened as a full Screen swapped into g_screen; the parent (main page) is kept
// alive through mParent so leaving Settings restores it with its state intact.
class SettingsScreen : public Screen {
public:
    explicit SettingsScreen(std::shared_ptr<Screen> parent);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;
    bool allowsExit() const override { return false; }

private:
    bool sectionInteractive(size_t section) const;
    size_t contentRowCount(size_t section) const;
    // Toggles the General row at idx and flags it as changed (D-Pad+A or touch).
    void toggleGeneral(int idx);

    void drawGeneral(void) const;
    void drawLibrary(void) const;
    void drawFolders(void) const;
    void drawNetwork(void) const;
    void drawAbout(void) const;

    void drawToggleRow(int y, const char* name, const char* sub, bool on, bool focused) const;
    void drawListRow(int y, const std::string& primary, const std::string& secondary, u32 pipColor, bool focused, bool removable) const;
    void drawEmptyState(const char* title, const char* body) const;
    void drawHints(int screenW, int y, const std::string& text) const;
    // Right-edge position indicator for scrollable content lists, mirroring the
    // main page's backup list. No-op when everything fits in one viewport.
    void drawScrollbar(int totalRows) const;

    std::shared_ptr<Screen> mParent;
    C2D_ImageTint flagTint;

    Hid<HidDirection::VERTICAL, HidDirection::VERTICAL> navHid;
    bool contentFocus                 = false; // false: section list (top) focused; true: content (bottom)
    int contentCursor                 = 0;     // selected row within the focused section's content
    int contentOffset                 = 0;     // first visible content row (simple vertical scroll)
    int savedTimer                    = 0;     // frames left to show the "Saved" flash on General
    static constexpr int VISIBLE_ROWS = 5;
};

#endif

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

#ifndef FOLDERBROWSEROVERLAY_HPP
#define FOLDERBROWSEROVERLAY_HPP

#include "Overlay.hpp"
#include <functional>
#include <string>
#include <switch.h>
#include <vector>

// A full SD-card directory browser (the Switch counterpart of the 3DS
// FolderBrowserOverlay). Raised by Settings > Save folders instead of the
// on-screen keyboard so a save folder is chosen by navigating to it:
//   A     descend into the highlighted folder
//   X     use the current folder (invokes onPick with its "sdmc:/..." path,
//         then dismisses)
//   B     go up one level; at the root it cancels the overlay
//
// Styled with the shared design tokens like TitlePickerOverlay.
class FolderBrowserOverlay : public Overlay {
public:
    FolderBrowserOverlay(Screen& screen, const std::string& heading, std::function<void(const std::string&)> onPick);
    void draw(void) const override;
    void update(const InputState& input) override;

private:
    // Full "sdmc:/..." path of the current folder, for onPick and the header.
    std::string currentPath(void) const;
    // (Re)reads mCurrent's sub-directories into mFolders and resets the cursor.
    void readFolders(void);

    std::string mHeading;
    std::function<void(const std::string&)> mOnPick;

    // Path of the current folder relative to the SD root, always starting with
    // '/' ("/" is the root). Descending appends "/name"; going up strips a level.
    std::string mCurrent = "/";
    std::vector<std::string> mFolders; // sub-folder names of mCurrent
    int mCursor = 0;
    int mScroll = 0;
};

#endif // FOLDERBROWSEROVERLAY_HPP

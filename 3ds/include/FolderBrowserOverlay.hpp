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
#include "hid.hpp"
#include <citro2d.h>
#include <functional>
#include <string>
#include <vector>

// Starts at the SD root and lists only sub-directories of the current folder.
//   A     descend into the highlighted folder
//   X     use the current folder (invokes onPick with its full "sdmc:/..." path,
//         then dismisses)
//   B     go up one level; at the root it cancels the overlay
//
// The picked path is all the caller needs — what it does with the choice
// (attach it to a title as an extra save / extdata folder) lives in onPick.
class FolderBrowserOverlay : public Overlay {
public:
    FolderBrowserOverlay(Screen& screen, const std::string& prompt, const std::function<void(const std::u16string&)>& onPick);
    ~FolderBrowserOverlay(void);
    void drawTop(void) const override;
    void drawBottom(void) const override;
    void update(const InputState& input) override;

private:
    // Full "sdmc:/..." path of the current folder, for onPick and the header.
    std::u16string currentPath(void) const;
    // (Re)reads mCurrent's sub-directories into mFolders and resets the cursor.
    void readFolders(void);

    std::string mPrompt;
    std::function<void(const std::u16string&)> mOnPick;
    C2D_TextBuf mBuf;
    Hid<HidDirection::VERTICAL, HidDirection::VERTICAL> mHid;

    // Archive-relative path of the current folder, always starting with '/'
    // ("/" is the SD root). Descending appends "/name"; going up strips a level.
    std::u16string mCurrent = u"/";
    std::vector<std::u16string> mFolders; // sub-folder names of mCurrent

    static constexpr size_t VISIBLE = 6;
};

#endif

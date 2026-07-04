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

#ifndef LISTPICKEROVERLAY_HPP
#define LISTPICKEROVERLAY_HPP

#include "Overlay.hpp"
#include "hid.hpp"
#include <string>

// Deep home of the full-screen list-picker chrome: top-screen backdrop + card,
// prompt header with "n / total" counter, separator, per-row selection
// highlight, and the bottom-screen hint line. Subclasses supply the rows and
// the input policy; TitlePickerOverlay and FolderBrowserOverlay are thin
// configurations over this chrome.
class ListPickerOverlay : public Overlay {
public:
    void drawTop(void) const override;
    void drawBottom(void) const override;

protected:
    static constexpr size_t VISIBLE = 6;
    // Overlay text draws above the screen content layer.
    static constexpr float OVERLAY_Z = 0.6f;

    // `listTop` is the y of the first row (the separator sits 6px above it),
    // `rowH` the row pitch, `hintScale` the bottom hint text scale.
    ListPickerOverlay(Screen& screen, const std::string& prompt, int listTop, int rowH, float hintScale);

    virtual int rowCount(void) const = 0;
    // Extra header content between the prompt and the separator (e.g. a path).
    virtual void drawHeaderExtra(void) const {}
    virtual void drawEmptyMessage(void) const                         = 0;
    virtual void drawRowContent(int k, int rowY, bool selected) const = 0;
    virtual std::string bottomHints(void) const                       = 0;

    Hid<HidDirection::VERTICAL, HidDirection::VERTICAL> mHid;

private:
    std::string mPrompt;
    int mListTop, mRowH;
    float mHintScale;
};

#endif

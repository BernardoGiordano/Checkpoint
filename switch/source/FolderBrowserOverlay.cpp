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

#include "FolderBrowserOverlay.hpp"
#include "SDLHelper.hpp"
#include "colors.hpp"
#include "common.hpp"
#include "directory.hpp"
#include "shapes.hpp"
#include "uikit.hpp"
#include <algorithm>

namespace {
    constexpr int PANEL_X = 290, PANEL_Y = 90, PANEL_W = 700, PANEL_H = 540;
    constexpr int LIST_X  = PANEL_X + 20;
    constexpr int LIST_Y  = PANEL_Y + 80;
    constexpr int LIST_W  = PANEL_W - 40;
    constexpr int ROW_H   = 44;
    constexpr int ROW_GAP = 6;
    constexpr int VISIBLE = (PANEL_H - 80 - 20) / (ROW_H + ROW_GAP);
}

FolderBrowserOverlay::FolderBrowserOverlay(Screen& screen, const std::string& heading, std::function<void(const std::string&)> onPick)
    : Overlay(screen), mHeading(heading), mOnPick(std::move(onPick))
{
    readFolders();
}

std::string FolderBrowserOverlay::currentPath(void) const
{
    // mCurrent is SD-relative ("/" == root); config paths carry the "sdmc:"
    // prefix, matching what the old keyboard prompt produced.
    return "sdmc:" + mCurrent;
}

void FolderBrowserOverlay::readFolders(void)
{
    mFolders.clear();
    mCursor = 0;
    mScroll = 0;

    Directory dir(currentPath());
    if (dir.good()) {
        for (size_t i = 0, sz = dir.size(); i < sz; i++) {
            // Sub-folders only; skip "." / ".." and dotfiles.
            if (dir.folder(i) && dir.entry(i).size() && dir.entry(i)[0] != '.') {
                mFolders.push_back(dir.entry(i));
            }
        }
    }
    std::sort(mFolders.begin(), mFolders.end());
}

void FolderBrowserOverlay::draw(void) const
{
    SDLH_DrawRect(0, 0, 1280, 720, COLOR_SCRIM);
    Shapes::cardRound(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 16, COLOR_SURFACE, COLOR_STROKE2, 1);

    // Heading + a right-aligned "i / n" counter on the same line.
    SDLH_DrawText(20, LIST_X, PANEL_Y + 18, COLOR_TEXT, mHeading.c_str());
    if (!mFolders.empty()) {
        std::string counter = StringUtils::format("%d / %d", mCursor + 1, (int)mFolders.size());
        u32 cw, ch;
        SDLH_GetTextDimensions(13, counter.c_str(), &cw, &ch);
        SDLH_DrawText(13, PANEL_X + PANEL_W - 20 - (int)cw, PANEL_Y + 24, COLOR_TEXT3, counter.c_str());
    }

    // Current path, then a hairline under the header.
    SDLH_DrawText(13, LIST_X, PANEL_Y + 48, COLOR_ACCENT_LIGHT, trimToFit(currentPath(), LIST_W, 13).c_str());
    SDLH_DrawRect(LIST_X, PANEL_Y + 70, LIST_W, 1, COLOR_STROKE1);

    if (mFolders.empty()) {
        SDLH_DrawText(15, LIST_X, LIST_Y + 8, COLOR_TEXT2, "No sub-folders here.");
        SDLH_DrawText(13, LIST_X, LIST_Y + 34, COLOR_TEXT3, "Press X to use this folder.");
    }

    for (int i = mScroll; i < (int)mFolders.size() && i < mScroll + VISIBLE; i++) {
        const int y      = LIST_Y + (i - mScroll) * (ROW_H + ROW_GAP);
        const bool focus = i == mCursor;
        Shapes::fillRound(LIST_X, y, LIST_W, ROW_H, 12, focus ? COLOR_ACCENT_TINT : COLOR_FILL1);
        u32 nh;
        SDLH_GetTextDimensions(15, "Ag", NULL, &nh);
        std::string name = trimToFit(mFolders[i], LIST_W - 28, 15);
        SDLH_DrawText(15, LIST_X + 14, y + (ROW_H - (int)nh) / 2, focus ? COLOR_TEXT : COLOR_TEXT2, name.c_str());
        if (focus) {
            Shapes::focusRing(LIST_X, y, LIST_W, ROW_H, 12, COLOR_ACCENT);
        }
    }

    UiKit::drawHintBar({
        {"A", "Open"},
        {"X", "Use folder"},
        {"B", mCurrent == "/" ? "Cancel" : "Up"},
    });
}

void FolderBrowserOverlay::update(const InputState& input)
{
    const u64 kdown = input.kDown;

    if (kdown & HidNpadButton_X) {
        // Copy to the stack: me.reset() destroys *this before the callback runs.
        auto pick        = mOnPick;
        std::string path = currentPath();
        me.reset();
        if (pick)
            pick(path);
        return;
    }

    if (kdown & HidNpadButton_B) {
        if (mCurrent == "/") {
            me.reset();
            return;
        }
        size_t slash = mCurrent.find_last_of('/');
        mCurrent     = slash == 0 ? "/" : mCurrent.substr(0, slash);
        readFolders();
        return;
    }

    if (!mFolders.empty()) {
        if (kdown & HidNpadButton_Up) {
            mCursor = mCursor > 0 ? mCursor - 1 : (int)mFolders.size() - 1;
        }
        else if (kdown & HidNpadButton_Down) {
            mCursor = mCursor < (int)mFolders.size() - 1 ? mCursor + 1 : 0;
        }
        if (mCursor < mScroll)
            mScroll = mCursor;
        else if (mCursor >= mScroll + VISIBLE)
            mScroll = mCursor - VISIBLE + 1;

        if (kdown & HidNpadButton_A) {
            mCurrent += (mCurrent == "/" ? "" : "/") + mFolders[mCursor];
            readFolders();
            return;
        }
    }
}

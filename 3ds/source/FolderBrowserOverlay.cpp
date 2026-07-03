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
#include "archive.hpp"
#include "directory.hpp"
#include "glyphs.hpp"
#include "gui.hpp"
#include "textpool.hpp"
#include "util.hpp"
#include <3ds.h>
#include <algorithm>

namespace {
    // Overlay text draws above the screen content layer.
    constexpr float OVERLAY_Z = 0.6f;

    // A small folder glyph drawn from primitives (no gfx asset): a tab + body.
    void drawFolderMark(int x, int y, int side, u32 color)
    {
        const int h = side - 4;
        C2D_DrawRectSolid(x, y + 3, 0.65f, side * 0.45f, 2, color);
        C2D_DrawRectSolid(x, y + 5, 0.65f, side, h - 3, color);
    }
}

FolderBrowserOverlay::FolderBrowserOverlay(Screen& screen, const std::string& prompt, const std::function<void(const std::u16string&)>& onPick)
    : Overlay(screen), mPrompt(prompt), mOnPick(onPick), mHid(VISIBLE, 1)
{
    readFolders();
}

std::u16string FolderBrowserOverlay::currentPath(void) const
{
    // mCurrent is archive-relative ("/" == SD root); config paths carry the
    // "sdmc:" prefix, matching what the old keyboard prompt produced.
    return u"sdmc:" + mCurrent;
}

void FolderBrowserOverlay::readFolders(void)
{
    mFolders.clear();
    Directory dir(Archive::sdmc(), mCurrent);
    if (dir.good()) {
        for (size_t i = 0, sz = dir.size(); i < sz; i++) {
            if (dir.folder(i)) {
                mFolders.push_back(dir.entry(i));
            }
        }
    }
    std::sort(mFolders.begin(), mFolders.end());
    mHid.reset();
}

void FolderBrowserOverlay::drawTop(void) const
{
    TextPool& text  = TextPool::get();
    const int count = (int)mFolders.size();

    C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(24, 14, 0.6f, 352, 212, COLOR_CARD);
    Gui::drawOutline(24, 14, 352, 212, 2, COLOR_ACCENT);

    // Header: the prompt, then the current path on its own line (monospace-ish).
    text.draw(mPrompt, 36, 22, 0.5f, COLOR_TEXT, OVERLAY_Z);
    if (count > 0) {
        std::string counter = StringUtils::format("%d / %d", mHid.fullIndex() + 1, count);
        float w             = text.width(counter, 0.42f);
        text.draw(counter, 364 - w, 24, 0.42f, COLOR_FAINT, OVERLAY_Z);
    }
    std::string path = StringUtils::UTF16toUTF8(currentPath());
    text.draw(text.truncate(path, 328, 0.42f), 36, 40, 0.42f, COLOR_TEAL, OVERLAY_Z);
    C2D_DrawRectSolid(36, 56, 0.6f, 328, 1, COLOR_LINE);

    if (count == 0) {
        text.drawCentered("No sub-folders here.", 0, 400, 120, 0.46f, COLOR_MUTED, OVERLAY_Z);
        text.drawCentered("Press X to use this folder.", 0, 400, 142, 0.42f, COLOR_FAINT, OVERLAY_Z);
        return;
    }

    const int rowH  = 26;
    const int start = mHid.page() * (int)VISIBLE;
    for (int i = 0; i < (int)VISIBLE && start + i < count; i++) {
        const int k    = start + i;
        const int rowY = 62 + i * rowH;
        const bool sel = i == (int)mHid.index();
        if (sel) {
            C2D_DrawRectSolid(30, rowY, 0.6f, 340, rowH - 2, COLOR_ROW_SELECT);
            Gui::drawOutline(30, rowY, 340, rowH - 2, 1, COLOR_ACCENT);
        }
        drawFolderMark(38, rowY + 4, 16, sel ? COLOR_TEAL : COLOR_MUTED);
        std::string name = StringUtils::UTF16toUTF8(mFolders[k]);
        text.draw(text.truncate(name, 292, 0.46f), 64, rowY + 4, 0.46f, sel ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
    }
}

void FolderBrowserOverlay::drawBottom(void) const
{
    C2D_DrawRectSolid(0, 0, 0.6f, 320, 240, COLOR_OVERLAY);
    const bool atRoot = mCurrent == u"/";
    std::string hints = std::string(GLYPH_A) + " Open   " + GLYPH_X + " Use folder   " + GLYPH_B + (atRoot ? " Cancel" : " Up");
    TextPool::get().drawCentered(hints, 0, 320, 112, 0.46f, COLOR_TEXT, OVERLAY_Z);
}

void FolderBrowserOverlay::update(const InputState& input)
{
    (void)input;
    const int count = (int)mFolders.size();
    mHid.update(count > 0 ? count : 1);

    u32 kDown = hidKeysDown();

    if ((kDown & KEY_A) && count > 0) {
        const std::u16string& name = mFolders[mHid.fullIndex()];
        mCurrent += (mCurrent == u"/" ? u"" : u"/") + name;
        readFolders();
        return;
    }

    if (kDown & KEY_X) {
        std::u16string path = currentPath();
        auto pick           = mOnPick; // copy to the stack: removeOverlay() destroys *this
        screen.removeOverlay();        // dismiss before running the callback
        pick(path);
        return;
    }

    if (kDown & KEY_B) {
        if (mCurrent == u"/") {
            screen.removeOverlay();
            return;
        }
        size_t slash = mCurrent.find_last_of(u'/');
        mCurrent     = slash == 0 ? u"/" : mCurrent.substr(0, slash);
        readFolders();
        return;
    }
}

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
#include "gui.hpp"
#include "util.hpp"
#include <3ds.h>
#include <algorithm>

static const char* GLYPH_A = "\xEE\x80\x80"; // U+E000
static const char* GLYPH_B = "\xEE\x80\x81"; // U+E001
static const char* GLYPH_X = "\xEE\x80\x82"; // U+E002

namespace {
    float drawRun(C2D_TextBuf buf, const std::string& s, float x, float y, float scale, u32 color)
    {
        C2D_Text t;
        C2D_TextParse(&t, buf, s.c_str());
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_WithColor, x, y, 0.6f, scale, scale, color);
        return StringUtils::textWidth(t, scale);
    }

    float runWidth(C2D_TextBuf buf, const std::string& s, float scale)
    {
        C2D_Text t;
        C2D_TextParse(&t, buf, s.c_str());
        C2D_TextOptimize(&t);
        return StringUtils::textWidth(t, scale);
    }

    std::string truncateToWidth(C2D_TextBuf buf, const std::string& s, float maxWidth, float scale)
    {
        if (runWidth(buf, s, scale) <= maxWidth) {
            return s;
        }
        std::string truncated = s;
        while (!truncated.empty() && runWidth(buf, truncated + "...", scale) > maxWidth) {
            truncated.pop_back();
        }
        return truncated + "...";
    }

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
    mBuf = C2D_TextBufNew(4096);
    readFolders();
}

FolderBrowserOverlay::~FolderBrowserOverlay(void)
{
    C2D_TextBufDelete(mBuf);
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
    C2D_TextBufClear(mBuf);
    const int count = (int)mFolders.size();

    C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(24, 14, 0.6f, 352, 212, COLOR_V4_CARD);
    Gui::drawOutline(24, 14, 352, 212, 2, COLOR_V4_ACCENT);

    // Header: the prompt, then the current path on its own line (monospace-ish).
    drawRun(mBuf, mPrompt, 36, 22, 0.5f, COLOR_V4_TEXT);
    if (count > 0) {
        std::string counter = StringUtils::format("%d / %d", mHid.fullIndex() + 1, count);
        float w             = runWidth(mBuf, counter, 0.42f);
        drawRun(mBuf, counter, 364 - w, 24, 0.42f, COLOR_V4_FAINT);
    }
    std::string path = StringUtils::UTF16toUTF8(currentPath());
    drawRun(mBuf, truncateToWidth(mBuf, path, 328, 0.42f), 36, 40, 0.42f, COLOR_V4_TEAL);
    C2D_DrawRectSolid(36, 56, 0.6f, 328, 1, COLOR_V4_LINE);

    if (count == 0) {
        std::string msg = "No sub-folders here.";
        drawRun(mBuf, msg, ceilf((400 - runWidth(mBuf, msg, 0.46f)) / 2), 120, 0.46f, COLOR_V4_MUTED);
        std::string hint = "Press X to use this folder.";
        drawRun(mBuf, hint, ceilf((400 - runWidth(mBuf, hint, 0.42f)) / 2), 142, 0.42f, COLOR_V4_FAINT);
        return;
    }

    const int rowH  = 26;
    const int start = mHid.page() * (int)VISIBLE;
    for (int i = 0; i < (int)VISIBLE && start + i < count; i++) {
        const int k    = start + i;
        const int rowY = 62 + i * rowH;
        const bool sel = i == (int)mHid.index();
        if (sel) {
            C2D_DrawRectSolid(30, rowY, 0.6f, 340, rowH - 2, C2D_Color32(122, 66, 196, 60));
            Gui::drawOutline(30, rowY, 340, rowH - 2, 1, COLOR_V4_ACCENT);
        }
        drawFolderMark(38, rowY + 4, 16, sel ? COLOR_V4_TEAL : COLOR_V4_MUTED);
        std::string name = StringUtils::UTF16toUTF8(mFolders[k]);
        drawRun(mBuf, truncateToWidth(mBuf, name, 292, 0.46f), 64, rowY + 4, 0.46f, sel ? COLOR_V4_TEXT : COLOR_V4_MUTED);
    }
}

void FolderBrowserOverlay::drawBottom(void) const
{
    C2D_DrawRectSolid(0, 0, 0.6f, 320, 240, COLOR_OVERLAY);
    const bool atRoot = mCurrent == u"/";
    std::string hints = std::string(GLYPH_A) + " Open   " + GLYPH_X + " Use folder   " + GLYPH_B + (atRoot ? " Cancel" : " Up");
    C2D_Text t;
    C2D_TextParse(&t, mBuf, hints.c_str());
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, ceilf((320 - StringUtils::textWidth(t, 0.46f)) / 2), 112, 0.6f, 0.46f, 0.46f, COLOR_V4_TEXT);
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

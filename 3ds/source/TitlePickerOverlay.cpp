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

#include "TitlePickerOverlay.hpp"
#include "archive.hpp"
#include "gui.hpp"
#include "loader.hpp"
#include "title.hpp"
#include "util.hpp"
#include <3ds.h>

static const char* GLYPH_A = "\xEE\x80\x80"; // U+E000
static const char* GLYPH_B = "\xEE\x80\x81"; // U+E001

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

    // Icon sized into a `side`x`side` box at (x, y): SMDH icons scale down, small
    // ones sit centered.
    void drawIcon(C2D_Image icon, int x, int y, int side)
    {
        if (icon.subtex->width == 48) {
            C2D_DrawImageAt(icon, x, y, 0.65f, nullptr, (float)side / 48.0f, (float)side / 48.0f);
        }
        else {
            const int off = (side - icon.subtex->width) / 2;
            C2D_DrawImageAt(icon, x + off, y + off, 0.65f, nullptr, 1.0f, 1.0f);
        }
    }
}

TitlePickerOverlay::TitlePickerOverlay(Screen& screen, const std::string& prompt, const std::function<void(u64)>& onPick)
    : Overlay(screen), mPrompt(prompt), mOnPick(onPick), mHid(VISIBLE, 1)
{
    mBuf = C2D_TextBufNew(4096);
}

TitlePickerOverlay::~TitlePickerOverlay(void)
{
    C2D_TextBufDelete(mBuf);
}

void TitlePickerOverlay::drawTop(void) const
{
    C2D_TextBufClear(mBuf);
    const int count = TitleCatalog::get().getTitleCount(BackupKind::Save);

    C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, COLOR_OVERLAY);
    C2D_DrawRectSolid(24, 14, 0.6f, 352, 212, COLOR_V4_CARD);
    Gui::drawOutline(24, 14, 352, 212, 2, COLOR_V4_ACCENT);

    // Header.
    drawRun(mBuf, mPrompt, 36, 22, 0.5f, COLOR_V4_TEXT);
    if (count > 0) {
        std::string counter = StringUtils::format("%d / %d", mHid.fullIndex() + 1, count);
        float w             = runWidth(mBuf, counter, 0.42f);
        drawRun(mBuf, counter, 364 - w, 24, 0.42f, COLOR_V4_FAINT);
    }
    C2D_DrawRectSolid(36, 42, 0.6f, 328, 1, COLOR_V4_LINE);

    if (count == 0) {
        std::string msg = "No titles available.";
        drawRun(mBuf, msg, ceilf((400 - runWidth(mBuf, msg, 0.5f)) / 2), 110, 0.5f, COLOR_V4_MUTED);
        return;
    }

    const int rowH  = 28;
    const int start = mHid.page() * (int)VISIBLE;
    for (int i = 0; i < (int)VISIBLE && start + i < count; i++) {
        const int k    = start + i;
        const int rowY = 48 + i * rowH;
        const bool sel = i == (int)mHid.index();
        if (sel) {
            C2D_DrawRectSolid(30, rowY, 0.6f, 340, rowH - 2, C2D_Color32(122, 66, 196, 60));
            Gui::drawOutline(30, rowY, 340, rowH - 2, 1, COLOR_V4_ACCENT);
        }
        drawIcon(TitleCatalog::get().icon(k, BackupKind::Save), 36, rowY + 1, 24);

        Title title;
        TitleCatalog::get().getTitle(title, k, BackupKind::Save);
        drawRun(mBuf, truncateToWidth(mBuf, title.shortDescription(), 290, 0.46f), 66, rowY + 5, 0.46f, sel ? COLOR_V4_TEXT : COLOR_V4_MUTED);
    }
}

void TitlePickerOverlay::drawBottom(void) const
{
    C2D_DrawRectSolid(0, 0, 0.6f, 320, 240, COLOR_OVERLAY);
    std::string hints = std::string(GLYPH_A) + " Select      " + GLYPH_B + " Cancel";
    C2D_Text t;
    C2D_TextParse(&t, mBuf, hints.c_str());
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, ceilf((320 - StringUtils::textWidth(t, 0.5f)) / 2), 112, 0.6f, 0.5f, 0.5f, COLOR_V4_TEXT);
}

void TitlePickerOverlay::update(const InputState& input)
{
    (void)input;
    const int count = TitleCatalog::get().getTitleCount(BackupKind::Save);
    mHid.update(count > 0 ? count : 1);

    u32 kDown = hidKeysDown();
    if ((kDown & KEY_A) && count > 0) {
        Title title;
        TitleCatalog::get().getTitle(title, mHid.fullIndex(), BackupKind::Save);
        u64 id    = title.id();
        auto pick = mOnPick;    // copy to the stack: removeOverlay() destroys *this
        screen.removeOverlay(); // dismiss the picker before running the callback
        pick(id);
        return;
    }
    if (kDown & KEY_B) {
        screen.removeOverlay();
        return;
    }
}

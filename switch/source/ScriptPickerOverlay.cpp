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

#include "ScriptPickerOverlay.hpp"
#include "colors.hpp"
#include "gfxutils.hpp"
#include "i18n.hpp"
#include "shapes.hpp"
#include "uikit.hpp"

// Same panel geometry as TitlePickerOverlay, so the script chooser reads as
// the same kind of modal.
namespace {
    constexpr int PANEL_X = 290, PANEL_Y = 100, PANEL_W = 700, PANEL_H = 520;
    constexpr int LIST_X = PANEL_X + 20, LIST_Y = PANEL_Y + 64;
    constexpr int LIST_W  = PANEL_W - 40;
    constexpr int ROW_H   = 52;
    constexpr int ROW_GAP = 6;
    constexpr int VISIBLE = (PANEL_H - 64 - 20) / (ROW_H + ROW_GAP);
}

ScriptPickerOverlay::ScriptPickerOverlay(
    Screen& screen, std::vector<ScriptCatalog::Entry> entries, const std::string& titleTag, std::function<void(const ScriptCatalog::Entry&)> onPick)
    : Overlay(screen), mEntries(std::move(entries)), mTitleTag(titleTag), mOnPick(std::move(onPick))
{
}

void ScriptPickerOverlay::draw(void) const
{
    Gfx::DrawRect(0, 0, 1280, 720, COLOR_SCRIM);
    Shapes::cardRound(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 0, COLOR_SURFACE, COLOR_STROKE2, 1);

    const std::string heading = i18n::t("scripts.pick_prompt");
    Gfx::DrawText(20, LIST_X, PANEL_Y + 20, COLOR_TEXT, heading.c_str());

    if (mEntries.empty()) {
        Gfx::DrawText(15, LIST_X, LIST_Y + 8, COLOR_TEXT2, i18n::t("scripts.empty").c_str());
    }

    for (int i = mScroll; i < (int)mEntries.size() && i < mScroll + VISIBLE; i++) {
        const int y      = LIST_Y + (i - mScroll) * (ROW_H + ROW_GAP);
        const bool focus = i == mCursor;
        Shapes::fillRound(LIST_X, y, LIST_W, ROW_H, 0, focus ? COLOR_ACCENT_TINT : COLOR_FILL1);
        const Color fg = focus ? COLOR_TEXT : COLOR_TEXT2;

        // Section tag on the right: "universal" or the selected title's name.
        const std::string tag = mEntries[i].universal ? i18n::t("scripts.universal_tag") : mTitleTag;
        u32 tagW, tagH;
        std::string tagFit = trimToFit(tag, LIST_W / 3, 12);
        Gfx::GetTextDimensions(12, tagFit.c_str(), &tagW, &tagH);
        Gfx::DrawText(12, LIST_X + LIST_W - 14 - (int)tagW, y + (ROW_H - (int)tagH) / 2, COLOR_TEXT3, tagFit.c_str());

        // "override" marker (an SD file shadowing a bundled one), left of the tag.
        u32 ovrW = 0;
        if (mEntries[i].overridden) {
            const std::string ovr = i18n::t("scripts.overridden_tag");
            u32 oh;
            Gfx::GetTextDimensions(12, ovr.c_str(), &ovrW, &oh);
            Gfx::DrawText(12, LIST_X + LIST_W - 14 - (int)tagW - 12 - (int)ovrW, y + (ROW_H - (int)oh) / 2, COLOR_ACCENT, ovr.c_str());
            ovrW += 12;
        }

        u32 nh;
        Gfx::GetTextDimensions(15, "Ag", NULL, &nh);
        std::string name = trimToFit(mEntries[i].name, LIST_W - 42 - (int)tagW - (int)ovrW, 15);
        Gfx::DrawText(15, LIST_X + 14, y + (ROW_H - (int)nh) / 2, fg, name.c_str());
        if (focus) {
            Shapes::focusRing(LIST_X, y, LIST_W, ROW_H, 0, COLOR_ACCENT);
        }
    }

    UiKit::drawHintBar({
        {"A", i18n::t("overlay.choose")},
        {"B", i18n::t("common.cancel")},
    });
}

void ScriptPickerOverlay::update(const InputState& input)
{
    const u64 kdown = input.kDown;

    if (kdown & HidNpadButton_B) {
        me.reset();
        return;
    }

    if (!mEntries.empty()) {
        if (kdown & HidNpadButton_Up) {
            mCursor = mCursor > 0 ? mCursor - 1 : (int)mEntries.size() - 1;
        }
        else if (kdown & HidNpadButton_Down) {
            mCursor = mCursor < (int)mEntries.size() - 1 ? mCursor + 1 : 0;
        }
        if (mCursor < mScroll)
            mScroll = mCursor;
        else if (mCursor >= mScroll + VISIBLE)
            mScroll = mCursor - VISIBLE + 1;

        if (kdown & HidNpadButton_A) {
            // Dismiss first: mOnPick raises the confirm overlay and captures
            // state that outlives this overlay.
            ScriptCatalog::Entry entry = mEntries[mCursor];
            auto pick                  = mOnPick;
            me.reset();
            if (pick)
                pick(entry);
            return;
        }
    }
}

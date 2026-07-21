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
#include "glyphs.hpp"
#include "i18n.hpp"
#include "textpool.hpp"
#include <3ds.h>

ScriptPickerOverlay::ScriptPickerOverlay(Screen& screen, std::vector<ScriptCatalog::Entry> entries, const std::string& titleTag,
    const std::function<void(const ScriptCatalog::Entry&)>& onPick)
    : ListPickerOverlay(screen, i18n::t("scripts.pick_prompt"), 48, 28, 0.5f), mEntries(std::move(entries)), mTitleTag(titleTag), mOnPick(onPick)
{
}

int ScriptPickerOverlay::rowCount(void) const
{
    return (int)mEntries.size();
}

void ScriptPickerOverlay::drawEmptyMessage(void) const
{
    TextPool::get().drawCentered(i18n::t("scripts.empty"), 0, 400, 110, 0.5f, COLOR_MUTED, OVERLAY_Z);
}

void ScriptPickerOverlay::drawRowContent(int k, int rowY, bool selected) const
{
    TextPool& text                    = TextPool::get();
    const ScriptCatalog::Entry& entry = mEntries[k];
    const std::string tag             = entry.universal ? i18n::t("scripts.universal_tag") : mTitleTag;
    const float tagW                  = text.width(tag, 0.42f);
    const float tagX                  = 364 - tagW;

    // "override" marker (an SD file shadowing a bundled one), left of the tag.
    float ovrW = 0;
    if (entry.overridden) {
        const std::string ovr = i18n::t("scripts.overridden_tag");
        ovrW                  = text.width(ovr, 0.42f) + 10;
        text.draw(ovr, tagX - ovrW, rowY + 6, 0.42f, COLOR_GOLD, OVERLAY_Z);
    }

    text.draw(text.truncate(entry.name, 320 - (int)tagW - (int)ovrW, 0.46f), 40, rowY + 5, 0.46f, selected ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
    text.draw(tag, tagX, rowY + 6, 0.42f, COLOR_FAINT, OVERLAY_Z);
}

std::string ScriptPickerOverlay::bottomHints(void) const
{
    return std::string(GLYPH_A) + " " + i18n::t("overlay.select") + "      " + GLYPH_B + " " + i18n::t("common.cancel");
}

void ScriptPickerOverlay::update(const InputState& input)
{
    (void)input;
    const int count = rowCount();
    mHid.update(count > 0 ? count : 1);

    u32 kDown = hidKeysDown();
    if ((kDown & KEY_A) && count > 0) {
        ScriptCatalog::Entry entry = mEntries[mHid.fullIndex()];
        auto pick                  = mOnPick; // copy to the stack: removeOverlay() destroys *this
        screen.removeOverlay();               // dismiss the picker before running the callback
        pick(entry);
        return;
    }
    if (kDown & KEY_B) {
        screen.removeOverlay();
        return;
    }
}

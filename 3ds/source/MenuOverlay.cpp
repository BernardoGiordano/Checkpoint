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

#include "MenuOverlay.hpp"
#include "colors.hpp"
#include "glyphs.hpp"
#include "i18n.hpp"
#include "textpool.hpp"
#include <3ds.h>

MenuOverlay::MenuOverlay(Screen& screen, const std::string& prompt, std::vector<Item> items)
    : ListPickerOverlay(screen, prompt, 48, 28, 0.5f), mItems(std::move(items))
{
}

int MenuOverlay::rowCount(void) const
{
    return (int)mItems.size();
}

void MenuOverlay::drawEmptyMessage(void) const
{
    // Menus are built from a fixed item list, so this never renders; the base
    // class demands the hook regardless.
}

void MenuOverlay::drawRowContent(int k, int rowY, bool selected) const
{
    TextPool::get().draw(mItems[k].label, 40, rowY + 5, 0.46f, selected ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
}

std::string MenuOverlay::bottomHints(void) const
{
    return std::string(GLYPH_A) + " " + i18n::t("overlay.select") + "      " + GLYPH_B + " " + i18n::t("common.cancel");
}

void MenuOverlay::update(const InputState& input)
{
    (void)input;
    const int count = rowCount();
    mHid.update(count > 0 ? count : 1);

    u32 kDown = hidKeysDown();
    if ((kDown & KEY_A) && count > 0) {
        // dismissThen copies the callback to the stack before destroying *this,
        // so an action that raises a follow-up overlay is safe.
        dismissThen(mItems[mHid.fullIndex()].action);
        return;
    }
    if (kDown & KEY_B) {
        screen.removeOverlay();
        return;
    }
}

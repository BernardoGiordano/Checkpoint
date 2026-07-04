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
#include "glyphs.hpp"
#include "gui.hpp"
#include "loader.hpp"
#include "textpool.hpp"
#include "title.hpp"
#include "util.hpp"
#include <3ds.h>

namespace {
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
    : ListPickerOverlay(screen, prompt, 48, 28, 0.5f), mOnPick(onPick)
{
}

int TitlePickerOverlay::rowCount(void) const
{
    return TitleCatalog::get().getTitleCount(BackupKind::Save);
}

void TitlePickerOverlay::drawEmptyMessage(void) const
{
    TextPool::get().drawCentered("No titles available.", 0, 400, 110, 0.5f, COLOR_MUTED, OVERLAY_Z);
}

void TitlePickerOverlay::drawRowContent(int k, int rowY, bool selected) const
{
    drawIcon(TitleCatalog::get().icon(k, BackupKind::Save), 36, rowY + 1, 24);

    std::string name;
    TitleCatalog::get().descriptionByIndex(name, k, BackupKind::Save);
    TextPool& text = TextPool::get();
    text.draw(text.truncate(name, 290, 0.46f), 66, rowY + 5, 0.46f, selected ? COLOR_TEXT : COLOR_MUTED, OVERLAY_Z);
}

std::string TitlePickerOverlay::bottomHints(void) const
{
    return std::string(GLYPH_A) + " Select      " + GLYPH_B + " Cancel";
}

void TitlePickerOverlay::update(const InputState& input)
{
    (void)input;
    const int count = rowCount();
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

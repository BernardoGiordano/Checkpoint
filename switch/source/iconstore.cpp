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

#include "iconstore.hpp"

static constexpr Color systemSavePalette[] = {
    {45, 80, 140, 255},  // muted blue
    {120, 50, 130, 255}, // muted purple
    {40, 110, 100, 255}, // teal
    {130, 60, 70, 255},  // muted red
    {60, 100, 60, 255},  // forest green
    {130, 95, 45, 255},  // muted amber
    {80, 65, 120, 255},  // lavender
    {50, 90, 110, 255},  // steel blue
};

void TextureIconStore::loadIcon(u64 id, NsApplicationControlData* nsacd, size_t iconSize)
{
    if (mIcons.find(id) != mIcons.end()) {
        return;
    }
    Texture* texture = nullptr;
    SDLH_LoadImage(&texture, nsacd->icon, iconSize);
    SDLH_SetTextureOpaque(texture);
    mIcons.insert({id, texture});
}

void TextureIconStore::loadPlaceholderIcon(u64 id)
{
    if (mIcons.find(id) != mIcons.end()) {
        return;
    }
    Color color      = systemSavePalette[id % (sizeof(systemSavePalette) / sizeof(systemSavePalette[0]))];
    Texture* texture = nullptr;
    SDLH_CreateColorTexture(&texture, 256, 256, color);
    if (texture) {
        mIcons.insert({id, texture});
    }
}

Texture* TextureIconStore::get(u64 id) const
{
    auto it = mIcons.find(id);
    return it != mIcons.end() ? it->second : NULL;
}

void TextureIconStore::clear(void)
{
    for (auto& i : mIcons) {
        SDLH_DestroyTexture(i.second);
    }
    mIcons.clear();
}

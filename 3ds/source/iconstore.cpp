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
#include "gui.hpp"
#include <cstring>

namespace {
    // CTR icon: 48x48 pixels living in the bottom of a 64x64 RGB565 texture.
    const Tex3DS_SubTexture ctrSubt3x = {48, 48, 0.0f, 48 / 64.0f, 48 / 64.0f, 0.0f};
    // DS legacy card icon: a full 32x32 RGB565 texture.
    const Tex3DS_SubTexture dsSubt3x = {32, 32, 0.0f, 1.0f, 1.0f, 0.0f};

    constexpr size_t CTR_PIXELS = 0x900; // 48x48 packed source pixels
    constexpr size_t DS_PIXELS  = 32 * 32;
}

IconStore::~IconStore()
{
    clear();
}

void IconStore::clear()
{
    for (auto& kv : mMap) {
        if (kv.second.tex) {
            C3D_TexDelete(kv.second.tex);
            free(kv.second.tex);
        }
    }
    mMap.clear();
}

void IconStore::erase(u64 id)
{
    auto it = mMap.find(id);
    if (it == mMap.end()) {
        return;
    }
    if (it->second.tex) {
        C3D_TexDelete(it->second.tex);
        free(it->second.tex);
    }
    mMap.erase(it);
}

void IconStore::mergeFrom(IconStore& o)
{
    for (auto& kv : o.mMap) {
        erase(kv.first); // free any displaced texture before overwriting
        mMap.emplace(kv.first, std::move(kv.second));
    }
    o.mMap.clear();
}

void IconStore::storeCtrIcon(u64 id, const u16* bigIconData)
{
    if (mMap.find(id) != mMap.end()) {
        return;
    }
    Entry e;
    e.isDs = false;
    e.pixels.assign(bigIconData, bigIconData + CTR_PIXELS);
    mMap.emplace(id, std::move(e));
}

void IconStore::storeDsIcon(u64 id, const u8* banner)
{
    if (mMap.find(id) != mMap.end()) {
        return;
    }

    struct bannerData {
        u16 version;
        u16 crc;
        u8 reserved[28];
        u8 data[512];
        u16 palette[16];
    };
    const bannerData* iconData = reinterpret_cast<const bannerData*>(banner);

    // Decode the 4bpp paletted banner into the swizzled 32x32 RGB565 layout the
    // texture expects (CPU-only; the upload happens later in realize()).
    Entry e;
    e.isDs = true;
    e.pixels.assign(DS_PIXELS, 0);
    u16* output = e.pixels.data();
    for (size_t x = 0; x < 32; x++) {
        for (size_t y = 0; y < 32; y++) {
            u32 srcOff   = (((y >> 3) * 4 + (x >> 3)) * 8 + (y & 7)) * 4 + ((x & 7) >> 1);
            u32 srcShift = (x & 1) * 4;

            u16 pIndex = (iconData->data[srcOff] >> srcShift) & 0xF;
            u16 color  = 0xFFFF;
            if (pIndex != 0) {
                u16 r = iconData->palette[pIndex] & 0x1F;
                u16 g = (iconData->palette[pIndex] >> 5) & 0x1F;
                u16 b = (iconData->palette[pIndex] >> 10) & 0x1F;
                color = (r << 11) | (g << 6) | (g >> 4) | (b);
            }

            u32 dst     = ((((y >> 3) * (32 >> 3) + (x >> 3)) << 6) +
                       ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
            output[dst] = color;
        }
    }

    mMap.emplace(id, std::move(e));
}

void IconStore::realize(Entry& e)
{
    e.tex = (C3D_Tex*)malloc(sizeof(C3D_Tex));

    if (e.isDs) {
        C3D_TexInit(e.tex, 32, 32, GPU_RGB565);
        memcpy(e.tex->data, e.pixels.data(), DS_PIXELS * sizeof(u16));
    }
    else {
        C3D_TexInit(e.tex, 64, 64, GPU_RGB565);
        // Bilinear filtering: the grid tile scales this 48px icon down to 44px,
        // and the default nearest filter makes that shrink look like a bad
        // downsample.
        C3D_TexSetFilter(e.tex, GPU_LINEAR, GPU_LINEAR);

        u16* dest = (u16*)e.tex->data + (64 - 48) * 64;
        u16* src  = e.pixels.data();
        for (int j = 0; j < 48; j += 8) {
            memcpy(dest, src, 48 * 8 * sizeof(u16));
            src += 48 * 8;
            dest += 64 * 8;
        }
    }

    // Pixels are only needed for this one upload; reclaim the RAM.
    std::vector<u16>().swap(e.pixels);
}

C2D_Image IconStore::get(u64 id)
{
    auto it = mMap.find(id);
    if (it == mMap.end()) {
        return Gui::noIcon();
    }

    Entry& e = it->second;
    if (!e.tex) {
        realize(e);
    }
    return (C2D_Image){e.tex, e.isDs ? &dsSubt3x : &ctrSubt3x};
}

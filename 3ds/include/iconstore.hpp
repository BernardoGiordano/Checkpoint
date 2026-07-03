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

#ifndef ICONSTORE_HPP
#define ICONSTORE_HPP

#include <3ds.h>
#include <citro2d.h>
#include <unordered_map>
#include <vector>

// The single owner of every title icon's C3D_Tex. The malloc'd textures used to
// live inside each Title by value, so a Title copy aliased the pointer and every
// catalog reload leaked the whole icon set. Here they belong to one store keyed
// by title id: the store is swapped in alongside the catalog lists (see
// TitleCatalog::loadTitles) and the outgoing store's destructor frees the old
// textures, so the leak dies structurally.
//
// Producer/consumer split keeps GPU work on the UI thread. The loader worker
// only *stores raw pixel bytes* (storeCtrIcon / storeDsIcon — no GPU calls,
// safe off-thread). The texture itself is created lazily by get(), which the
// draw path calls on the main thread; C3D's context is never touched from the
// worker. Only icons that are actually drawn get a texture, so the linear heap
// holds a handful of visible icons instead of every title's.
class IconStore {
public:
    IconStore() = default;
    ~IconStore();

    IconStore(const IconStore&)            = delete;
    IconStore& operator=(const IconStore&) = delete;
    IconStore(IconStore&& o) noexcept { mMap.swap(o.mMap); }
    IconStore& operator=(IconStore&& o) noexcept
    {
        if (this != &o) {
            clear();
            mMap.swap(o.mMap);
        }
        return *this;
    }

    // Producer side — worker thread. Copy the raw icon bytes; no GPU calls.
    // CTR: `bigIconData` is 0x900 RGB565 pixels straight from the SMDH/cache.
    void storeCtrIcon(u64 id, const u16* bigIconData);
    // DS legacy card: decode the 0x23C0 banner blob into the swizzled 32x32
    // buffer now (CPU only); the texture is uploaded later by get().
    void storeDsIcon(u64 id, const u8* banner);

    // Consumer side — main/draw thread. Create-on-demand, cached, owned. Returns
    // Gui::noIcon() (owned by the sprite sheet, never by this store) when `id`
    // has no stored icon.
    C2D_Image get(u64 id);

    bool has(u64 id) const { return mMap.find(id) != mMap.end(); }
    // Copy the stored raw CTR pixels (0x900 u16) for `id` into `out`. Returns
    // false if `id` has no stored icon, is a DS icon, or its pixels were already
    // realized into a texture (reclaimed). Lets the cache export reuse the icon
    // bytes the probe already read from the SMDH instead of re-reading it.
    bool copyCtrPixels(u64 id, u16* out) const;
    void clear();
    void swap(IconStore& o) noexcept { mMap.swap(o.mMap); }

    // Move every entry out of `o` into this store, replacing any id already
    // present (frees the displaced texture). Used by the live cart-scan path to
    // fold a freshly-probed card icon into the catalog's store.
    void mergeFrom(IconStore& o);
    // Drop the icon for `id`, freeing its texture. No-op if absent.
    void erase(u64 id);

private:
    struct Entry {
        std::vector<u16> pixels; // raw bytes until the texture is created, then freed
        bool isDs    = false;
        C3D_Tex* tex = nullptr; // owned; nullptr until first get() uploads it
    };

    // Upload `e.pixels` into a freshly-init'd C3D_Tex and release the pixels.
    void realize(Entry& e);

    std::unordered_map<u64, Entry> mMap;
};

#endif // ICONSTORE_HPP

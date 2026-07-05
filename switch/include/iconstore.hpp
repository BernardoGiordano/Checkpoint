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

#include "DekoHelper.hpp"
#include <switch.h>
#include <unordered_map>
#include <vector>

// Injectable seam through which TitleProbe stores a title's icon without knowing
// whether it ends up as a real SDL texture or is discarded. The producer reports
// the icon honestly; the adapter decides what to do with it. Two adapters keep
// the seam real: TextureIconStore decodes and owns the textures (the catalog
// holds one), RecordingIconStore captures the ids for headless use and serves as
// the test surface.
struct IconStore {
    virtual ~IconStore() = default;

    // Decode and keep the application-control-data icon for title `id`.
    virtual void loadIcon(u64 id, NsApplicationControlData* nsacd, size_t iconSize) = 0;
    // Keep a generated placeholder icon for title `id` (system saves have no SMDH icon).
    virtual void loadPlaceholderIcon(u64 id) = 0;
};

// Real adapter: decodes icons into SDL textures and owns them for the process
// lifetime. The single owner of the texture map that used to be a file-scope
// global in title.cpp.
class TextureIconStore : public IconStore {
public:
    void loadIcon(u64 id, NsApplicationControlData* nsacd, size_t iconSize) override;
    void loadPlaceholderIcon(u64 id) override;

    // Texture for title `id`, or NULL when none was loaded.
    Texture* get(u64 id) const;
    // Destroy every owned texture (called on exit).
    void clear(void);

private:
    std::unordered_map<u64, Texture*> mIcons;
};

// Headless adapter: records which ids were asked for, decodes nothing. The
// second adapter that keeps the seam real and lets TitleProbe be exercised
// without SDL.
struct RecordingIconStore : public IconStore {
    std::vector<u64> loaded;
    std::vector<u64> placeholders;

    void loadIcon(u64 id, NsApplicationControlData*, size_t) override { loaded.push_back(id); }
    void loadPlaceholderIcon(u64 id) override { placeholders.push_back(id); }
};

#endif // ICONSTORE_HPP

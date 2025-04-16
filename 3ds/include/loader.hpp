/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#ifndef LOADER_HPP
#define LOADER_HPP

#include "title.hpp"
#include <atomic>
#include <vector>

namespace TitleLoader {
    void getTitle(Title& dst, int i);
    int getTitleCount(void);
    C2D_Image icon(int i);
    bool favorite(int i);

    void loadTitles(bool forceRefreshParam);
    void refreshDirectories(u64 id);
    void loadTitlesThread(void);
    void cartScan(void);
    void cartScanFlagTestAndSet(void);
    void clearCartScanFlag(void);

    bool validId(u64 id);
    bool scanCard(void);
    void exportTitleListCache(std::vector<Title>& list, const std::u16string& path);
    void importTitleListCache(void);
}

#endif // LOADER_HPP
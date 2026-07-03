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

#include "BackupList.hpp"
#include "backupsize.hpp"
#include "colors.hpp"
#include "io.hpp"
#include "savedatasource.hpp"
#include "shapes.hpp"
#include "titlecatalog.hpp"
#include <optional>

BackupList::BackupList(int x, int y, int w, int h, size_t visibleRows) : mListX(x), mListY(y), mListW(w), mScrollable(x, y, w, h, visibleRows) {}

void BackupList::refreshSelected(AccountUid uid, saveTypeFilter_t filter, size_t filteredIdx, u32 catalogGen)
{
    const u32 sizeGen = BackupSizeCache::get().generation();
    if (mValid && mUid == uid && mFilter == filter && mFilteredIdx == filteredIdx && mCatalogGen == catalogGen && mSizeGen == sizeGen) {
        return; // cached title/rows still describe what is on screen
    }

    // A bumped size generation (an async walk landed, or an invalidation) means
    // only the size labels changed: keep the resolved title and scroll position,
    // just re-pull the sizes. flush() preserves the Scrollable index.
    const bool sizesOnly = mValid && mUid == uid && mFilter == filter && mFilteredIdx == filteredIdx && mCatalogGen == catalogGen;

    mUid         = uid;
    mFilter      = filter;
    mFilteredIdx = filteredIdx;
    mCatalogGen  = catalogGen;
    mSizeGen     = sizeGen;
    mValid       = true;
    rebuild(sizesOnly);
}

namespace {
    // "1.2 MB" / "48.0 KB" / "512 B". One decimal above KB; bytes shown whole.
    std::string humanSize(u64 bytes)
    {
        if (bytes < 1024) {
            return StringUtils::format("%llu B", (unsigned long long)bytes);
        }
        const char* units[] = {"KB", "MB", "GB", "TB"};
        double v            = (double)bytes / 1024.0;
        int u               = 0;
        while (v >= 1024.0 && u < 3) {
            v /= 1024.0;
            u++;
        }
        return StringUtils::format("%.1f %s", v, units[u]);
    }
}

void BackupList::rebuild(bool sizesOnly)
{
    if (!sizesOnly) {
        TitleCatalog::get().getFilteredTitle(mTitle, mUid, mFilter, mFilteredIdx);
    }

    mScrollable.flush();
    std::vector<std::string> dirs = mTitle.saves();

    // The recursive directory walk is expensive (many small SD reads), so it runs
    // on BackupSizeCache's worker rather than here — rebuild() is reached from
    // draw() on every title change and must not stall the UI. We only request the
    // compute and read whatever the cache already holds; the labels stay blank
    // until the walk lands, at which point the cache bumps its generation and
    // refreshSelected() re-pulls (see there).
    BackupSizeCache::get().request(mTitle.id(), mTitle.path());

    mSizes.assign(dirs.size(), std::string());
    for (size_t i = 0; i < dirs.size(); i++) {
        mScrollable.push_back(COLOR_SURFACE2, COLOR_WHITE, dirs.at(i), i == mScrollable.index());
        if (i == 0) {
            continue; // the synthetic "New..." row has no folder
        }
        if (auto sz = BackupSizeCache::get().backupSize(mTitle.id(), mTitle.fullPath(i))) {
            mSizes[i] = humanSize(*sz);
        }
    }
    auto total = BackupSizeCache::get().total(mTitle.id());
    mTotalSize = (total && *total > 0) ? humanSize(*total) : std::string();
}

namespace {
    // Account backups are named "YYYYMMDD-HHMMSS username": the timestamp is the
    // row's mono title, the trailing username (if any) becomes the right-hand
    // user chip. Splits on the first space; no space → whole name, no chip.
    void splitBackupName(const std::string& full, std::string& name, std::string& chip)
    {
        size_t sp = full.find(' ');
        if (sp == std::string::npos) {
            name = full;
            chip.clear();
        }
        else {
            name = full.substr(0, sp);
            chip = full.substr(sp + 1);
        }
    }
}

void BackupList::draw(bool focused)
{
    const size_t visible = mScrollable.visibleEntries();
    const size_t total   = mScrollable.size();
    const size_t base    = (size_t)mScrollable.page() * visible;
    const size_t shown   = total > base ? std::min(visible, total - base) : 0;
    const size_t sel     = mScrollable.index();

    for (size_t r = 0; r < shown; r++) {
        const size_t i = base + r;
        const int ry   = mListY + (int)r * ROW_PITCH;
        const bool cur = focused && i == sel;

        Shapes::fillRound(mListX, ry, mListW, ROW_H, 12, cur ? COLOR_ACCENT_TINT : COLOR_FILL1);

        if (i == 0) {
            // The synthetic "New..." entry, rendered as the create-backup row.
            u32 th;
            SDLH_GetTextDimensions(14, "New backup", NULL, &th);
            SDLH_DrawText(14, mListX + 16, ry + (ROW_H - (int)th) / 2, COLOR_ACCENT_LIGHT, " New backup");
            continue;
        }

        // The folder name is "YYYYMMDD-HHMMSS[ username]"; show only the
        // timestamp on the left and the folder's on-disk size on the right (the
        // trailing username is dropped — the size is the useful per-row fact).
        std::string name, chip;
        splitBackupName(mScrollable.cellName(i), name, chip);

        u32 nameH;
        SDLH_GetTextDimensions(13, name.c_str(), NULL, &nameH, FontFamily::Mono);
        SDLH_DrawText(13, mListX + 16, ry + (ROW_H - (int)nameH) / 2, cur ? COLOR_TEXT : COLOR_MONO_VAL, name.c_str(), FontFamily::Mono);

        std::string sizeStr = i < mSizes.size() ? mSizes[i] : std::string();
        if (!sizeStr.empty()) {
            u32 sw, sh;
            SDLH_GetTextDimensions(12, sizeStr.c_str(), &sw, &sh, FontFamily::Mono);
            SDLH_DrawText(
                12, mListX + mListW - 12 - (int)sw, ry + (ROW_H - (int)sh) / 2, cur ? COLOR_TEXT2 : COLOR_TEXT3, sizeStr.c_str(), FontFamily::Mono);
        }
    }
}

namespace {
    // The date-stamped folder name suggested for a new backup. Account saves also
    // append the (ASCII-folded) user name; the special save kinds use the bare date.
    std::string backupSuggestion(Title& title)
    {
        if (!SaveDataSource(title.saveDataType()).isUserAccount()) {
            return DateTime::dateTimeStr();
        }
        return DateTime::dateTimeStr() + " " +
               (StringUtils::containsInvalidChar(Account::username(title.userId()))
                       ? ""
                       : StringUtils::removeNotAscii(StringUtils::removeAccents(Account::username(title.userId()))));
    }
}

std::optional<std::string> BackupList::chooseDst(Title& title, size_t cellIndex, bool& usedKeyboardFallback)
{
    usedKeyboardFallback = false;
    if (cellIndex != 0) {
        return title.fullPath(cellIndex);
    }
    std::string suggestion = backupSuggestion(title);
    std::string name;
    if (MS::multipleSelectionEnabled()) {
        name = suggestion;
    }
    else if (KeyboardManager::get().isSystemKeyboardAvailable().first) {
        std::pair<bool, std::string> response = KeyboardManager::get().keyboard(suggestion);
        if (!response.first) {
            return std::nullopt;
        }
        name = StringUtils::removeForbiddenCharacters(response.second);
    }
    else {
        name                 = suggestion;
        usedKeyboardFallback = true;
    }
    return title.path() + "/" + name;
}

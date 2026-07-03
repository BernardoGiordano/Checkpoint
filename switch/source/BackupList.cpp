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
#include "colors.hpp"
#include "io.hpp"
#include "savedatasource.hpp"
#include "titlecatalog.hpp"
#include <optional>

BackupList::BackupList(int x, int y, int w, int h, size_t visibleRows) : mScrollable(x, y, w, h, visibleRows) {}

void BackupList::refreshSelected(AccountUid uid, saveTypeFilter_t filter, size_t filteredIdx, u32 catalogGen)
{
    if (mValid && mUid == uid && mFilter == filter && mFilteredIdx == filteredIdx && mCatalogGen == catalogGen) {
        return; // cached title/rows still describe what is on screen
    }

    mUid         = uid;
    mFilter      = filter;
    mFilteredIdx = filteredIdx;
    mCatalogGen  = catalogGen;
    mValid       = true;
    rebuild();
}

void BackupList::rebuild(void)
{
    TitleCatalog::get().getFilteredTitle(mTitle, mUid, mFilter, mFilteredIdx);

    mScrollable.flush();
    std::vector<std::string> dirs = mTitle.saves();
    for (size_t i = 0; i < dirs.size(); i++) {
        mScrollable.push_back(COLOR_BLACK_DARKER, COLOR_WHITE, dirs.at(i), i == mScrollable.index());
    }
}

void BackupList::draw(bool focused)
{
    // The scroll cursor can move independently of a rebuild (the user
    // scrolling the same title's list), so the highlighted row is kept in
    // sync every frame without touching the underlying Clickables otherwise.
    for (size_t i = 0; i < mScrollable.size(); i++) {
        mScrollable.selectRow(i, i == mScrollable.index());
    }
    mScrollable.draw(focused);
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

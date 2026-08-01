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
#include "configuration.hpp"
#include "i18n.hpp"
#include "io.hpp"
#include "main.hpp"
#include "savedatasource.hpp"
#include "shapes.hpp"
#include "titlecatalog.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

namespace {
    // D-Pad auto-repeat delay, in system ticks (~156ms at the 19.2MHz Switch
    // tick clock) — the same feel the old paged Hid used.
    constexpr u64 REPEAT_DELAY_TICKS = 3000000;
}

BackupList::BackupList(int x, int y, int w, int h, size_t visibleRows)
    : mListX(x), mListY(y), mListW(w), mBaseVisibleRows(visibleRows), mVisibleRows(visibleRows)
{
    (void)h; // row geometry is derived from ROW_PITCH, not the passed pixel height
}

void BackupList::clampCursor(void)
{
    const size_t count = displayCount();
    if (count == 0) {
        mIndex  = 0;
        mOffset = 0;
        return;
    }
    if (mIndex >= count) {
        mIndex = count - 1;
    }
    if (mIndex < mOffset) {
        mOffset = mIndex;
    }
    else if (mIndex >= mOffset + mVisibleRows) {
        mOffset = mIndex - mVisibleRows + 1;
    }
    const size_t maxOffset = count > mVisibleRows ? count - mVisibleRows : 0;
    if (mOffset > maxOffset) {
        mOffset = maxOffset;
    }
}

void BackupList::updateSelection(void)
{
    const size_t count = displayCount();
    if (count == 0) {
        return;
    }

    const u64 now   = armGetSystemTick();
    const u64 kDown = g_input->kDown;
    const u64 kHeld = g_input->kHeld;
    auto moveDown   = [&]() { mIndex = (mIndex + 1 < count) ? mIndex + 1 : 0; };
    auto moveUp     = [&]() { mIndex = (mIndex > 0) ? mIndex - 1 : count - 1; };

    if (kDown & HidNpadButton_AnyDown) {
        moveDown();
        mLastTick = now;
    }
    else if (kDown & HidNpadButton_AnyUp) {
        moveUp();
        mLastTick = now;
    }
    else if ((kHeld & HidNpadButton_AnyDown) && now > mLastTick + REPEAT_DELAY_TICKS) {
        moveDown();
        mLastTick = now;
    }
    else if ((kHeld & HidNpadButton_AnyUp) && now > mLastTick + REPEAT_DELAY_TICKS) {
        moveUp();
        mLastTick = now;
    }

    // Touch picks a visible row directly.
    if (g_input->touch.count > 0) {
        const auto& t = g_input->touch.touches[0];
        if ((int)t.x >= mListX && (int)t.x < mListX + mListW && (int)t.y >= mListY && (int)t.y < mListY + (int)mVisibleRows * ROW_PITCH) {
            const size_t row = mOffset + (size_t)(((int)t.y - mListY) / ROW_PITCH);
            if (row < count) {
                mIndex = row;
            }
        }
    }

    clampCursor();
}

void BackupList::refreshSelected(AccountUid uid, saveTypeFilter_t filter, size_t filteredIdx, u32 catalogGen)
{
    const u32 sizeGen = BackupSizeCache::get().generation();
    // The wireless "Receive" row is gated behind the transfer setting; fold it
    // into the change check so toggling the feature (in Settings) re-lays out the
    // list even when nothing else moved.
    const bool xfer = Configuration::getInstance().isTransferEnabled();
    if (mValid && mUid == uid && mFilter == filter && mFilteredIdx == filteredIdx && mCatalogGen == catalogGen && mSizeGen == sizeGen &&
        mHasReceiveRow == xfer) {
        return; // cached title/rows still describe what is on screen
    }

    // A bumped size generation (an async walk landed, or an invalidation) means
    // only the size labels changed: keep the resolved title and scroll position,
    // just re-pull the sizes. flush() preserves the Scrollable index.
    const bool sizesOnly =
        mValid && mUid == uid && mFilter == filter && mFilteredIdx == filteredIdx && mCatalogGen == catalogGen && mHasReceiveRow == xfer;

    // One row narrower while transfer is on, leaving room for the stacked Send
    // button below the list (mirrors the 3DS layout).
    mHasReceiveRow = xfer;
    mVisibleRows   = xfer && mBaseVisibleRows > 1 ? mBaseVisibleRows - 1 : mBaseVisibleRows;

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

    mNames = mTitle.saves();

    // The recursive directory walk is expensive (many small SD reads), so it runs
    // on BackupSizeCache's worker rather than here — rebuild() is reached from
    // draw() on every title change and must not stall the UI. We only request the
    // compute and read whatever the cache already holds; the labels stay blank
    // until the walk lands, at which point the cache bumps its generation and
    // refreshSelected() re-pulls (see there).
    BackupSizeCache::get().request(mTitle.id(), mTitle.path());

    mSizes.assign(mNames.size(), std::string());
    for (size_t i = 1; i < mNames.size(); i++) { // index 0 is the synthetic "New..." row
        if (auto sz = BackupSizeCache::get().backupSize(mTitle.id(), mTitle.fullPath(i))) {
            mSizes[i] = humanSize(*sz);
        }
    }
    auto total = BackupSizeCache::get().total(mTitle.id());
    mTotalSize = (total && *total > 0) ? humanSize(*total) : std::string();

    clampCursor(); // the list may have shrunk/grown; keep the selection valid
    if (!sizesOnly) {
        snapAnimation(); // different rows underneath: sliding between them means nothing
    }
}

namespace {
    // Account backups are named "YYYYMMDD-HHMMSS username": the timestamp is the
    // row's mono title, the trailing username (if any) becomes the right-hand
    // user chip. Custom-named backups (user typed their own name, possibly with
    // spaces) must not be split — only a "YYYYMMDD-HHMMSS" prefix triggers it.
    void splitBackupName(const std::string& full, std::string& name, std::string& chip)
    {
        size_t sp              = full.find(' ');
        const bool timestamped = sp == 15 && full[8] == '-' &&
                                 std::all_of(full.begin(), full.begin() + 8, [](unsigned char c) { return std::isdigit(c); }) &&
                                 std::all_of(full.begin() + 9, full.begin() + 15, [](unsigned char c) { return std::isdigit(c); });
        if (!timestamped) {
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
    const size_t total = displayCount();
    const int windowH  = (int)mVisibleRows * ROW_PITCH - ROW_GAP;
    // A wrap (last row -> first, or first -> last) travels further than the window
    // is tall; whipping the whole list past the eye reads as a glitch, so those
    // jumps snap instead.
    const float snapDistance = (float)(mVisibleRows * (size_t)ROW_PITCH);
    const int scroll         = (int)lroundf(mScrollAnim.step((float)(mOffset * (size_t)ROW_PITCH), snapDistance));
    // Mid-slide a row hangs off each edge of the window: draw one extra on both
    // sides and let the scissor cut them, so nothing bleeds onto the backups
    // header above or the action buttons below.
    const size_t first = (size_t)(scroll / ROW_PITCH);
    const size_t last  = std::min(total, (size_t)((scroll + windowH) / ROW_PITCH) + 1);
    // Selection highlight rides its own animated position, so it slides between
    // rows even when the window itself does not move.
    const int cursorY = mListY + (int)lroundf(mCursorAnim.step((float)(mIndex * (size_t)ROW_PITCH), snapDistance)) - scroll;

    // Only the vertical edges need clipping; the 2px of horizontal slack keeps the
    // rows' antialiased left/right edges out of the scissor.
    Gfx::SetScissor(mListX - 2, mListY, mListW + 4, windowH);

    // Row backgrounds first, then the highlight on top of them: the highlight is
    // a single sliding quad, not the selected row's own fill, so it can sit
    // between two rows mid-animation.
    for (size_t i = first; i < last; i++) {
        // Rectangular rows (radius 0) to match the 3DS list and the squared buttons.
        Shapes::fillRound(mListX, mListY + (int)i * ROW_PITCH - scroll, mListW, ROW_H, 0, COLOR_FILL1);
    }
    if (focused && total > 0) {
        Shapes::fillRound(mListX, cursorY, mListW, ROW_H, 0, COLOR_ACCENT_TINT);
    }

    for (size_t i = first; i < last; i++) {
        const int ry   = mListY + (int)i * ROW_PITCH - scroll;
        const bool cur = focused && i == mIndex;

        if (i == 0 || isReceiveRow(i)) {
            // Action rows: the synthetic "New..." create-backup row and, when
            // wireless transfer is on, the "Receive" row (both share the accent
            // styling the 3DS action rows use).
            u32 th;
            std::string label = i18n::t(i == 0 ? "main.new_backup" : "transfer.receive");
            Gfx::GetTextDimensions(14, label.c_str(), NULL, &th);
            Gfx::DrawText(14, mListX + 16, ry + (ROW_H - (int)th) / 2, COLOR_ACCENT_LIGHT, (" " + label).c_str());
            continue;
        }

        // The folder name is "YYYYMMDD-HHMMSS[ username]"; show only the
        // timestamp on the left and the folder's on-disk size on the right (the
        // trailing username is dropped — the size is the useful per-row fact).
        // An existing backup: translate the on-screen row to its cell (saves) index.
        const size_t cell = rowToCell(i);
        std::string name, chip;
        splitBackupName(mNames[cell], name, chip);

        u32 nameH;
        Gfx::GetTextDimensions(13, name.c_str(), NULL, &nameH, FontFamily::Mono);
        Gfx::DrawText(13, mListX + 16, ry + (ROW_H - (int)nameH) / 2, cur ? COLOR_TEXT : COLOR_MONO_VAL, name.c_str(), FontFamily::Mono);

        std::string sizeStr = cell < mSizes.size() ? mSizes[cell] : std::string();
        if (!sizeStr.empty()) {
            u32 sw, sh;
            Gfx::GetTextDimensions(12, sizeStr.c_str(), &sw, &sh, FontFamily::Mono);
            Gfx::DrawText(
                12, mListX + mListW - 12 - (int)sw, ry + (ROW_H - (int)sh) / 2, cur ? COLOR_TEXT2 : COLOR_TEXT3, sizeStr.c_str(), FontFamily::Mono);
        }
    }

    Gfx::ResetScissor();

    // The same breathing accent ring the title grid uses on its selection, so the
    // backup list reads as "the thing you're driving" once focused. Drawn outside
    // the scissor because its glow spills past the row, and clamped to the window
    // so a transient animation overshoot cannot park it over the buttons.
    if (focused && total > 0) {
        const int ringY = std::clamp(cursorY, mListY, mListY + windowH - ROW_H);
        Shapes::focusRing(mListX, ringY, mListW, ROW_H, 0, COLOR_ACCENT);
    }

    // Vertical scrollbar in the gutter right of the rows, shown only when the
    // list overflows a page. Track spans the visible rows; the thumb is sized and
    // positioned from the animated scroll offset, so it glides with the rows.
    if (total > mVisibleRows) {
        const int trackX = mListX + mListW + 6;
        const int trackW = 4;
        const int trackY = mListY;
        const int trackH = (int)mVisibleRows * ROW_PITCH - ROW_GAP;
        Shapes::fillRound(trackX, trackY, trackW, trackH, trackW / 2, COLOR_FILL1);

        int thumbH = (int)((long)trackH * mVisibleRows / total);
        if (thumbH < 24)
            thumbH = 24;
        int thumbY = trackY + (int)lroundf((float)(trackH - thumbH) * (float)scroll / (float)((total - mVisibleRows) * (size_t)ROW_PITCH));
        thumbY     = std::min(std::max(thumbY, trackY), trackY + trackH - thumbH);
        Shapes::fillRound(trackX, thumbY, trackW, thumbH, trackW / 2, COLOR_STROKE3);
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
    // Quick backup (or multi-select) skips the keyboard prompt and takes the
    // timestamp directly
    if (MS::multipleSelectionEnabled() || Configuration::getInstance().isQuickBackupEnabled()) {
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

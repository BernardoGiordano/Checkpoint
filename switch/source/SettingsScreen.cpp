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

#include "SettingsScreen.hpp"
#include "FolderBrowserOverlay.hpp"
#include "TitlePickerOverlay.hpp"
#include "colors.hpp"
#include "configuration.hpp"
#include "main.hpp"
#include "shapes.hpp"
#include "sortmode.hpp"
#include "titlecatalog.hpp"
#include "transferjob.hpp"
#include "uikit.hpp"
#include "util.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <unistd.h>
#include <unordered_set>

// Settings layout. Absolute pixels on the fixed 1280x720 canvas, same as
// MainScreen.
namespace {
    constexpr int TOPBAR_H = UiKit::TOPBAR_H;

    // Category rail: x0 w300, 272x54 items from y74 stepping by 60.
    constexpr int CAT_X      = 14;
    constexpr int CAT_W      = 272;
    constexpr int CAT_ITEM_H = 54;
    constexpr int CAT_Y0     = 74;
    constexpr int CAT_PITCH  = 60;
    constexpr int RAIL_W     = 300;

    // Rows pane: x300 w980, padding 24 top / 32 sides -> inner x332 w916.
    constexpr int ROW_X             = 332;
    constexpr int ROW_W             = 916;
    constexpr int ROW_H             = 64;
    constexpr int ROW_GAP           = 10;
    constexpr int ROW_PAD           = 20; // horizontal padding inside a row
    constexpr int ROWS_Y0           = TOPBAR_H + 24;
    constexpr int SECTION_GAP_ABOVE = 12;

    const std::array<const char*, 5> kCategoryLabels = {"General", "Library", "Save folders", "Connectivity", "About"};

    // Vertical space a row's section label occupies above its rect. `atTop`
    // drops the gap the label reserves above itself (the row is first on screen).
    int sectionExtra(const std::string& section, bool atTop)
    {
        if (section.empty())
            return 0;
        u32 sh;
        SDLH_GetTextDimensions(11, "Ag", NULL, &sh);
        return (atTop ? 0 : SECTION_GAP_ABOVE) + (int)sh + 8;
    }

    // The console's LAN address, matching how ftp.c binds gethostid(). Bytes are
    // read in memory order (little-endian ARM), so the low byte is the first
    // octet — same convention inet_ntoa would use on the raw s_addr.
    //
    // gethostid() is a nifm round-trip, not a cheap read: calling it from the
    // FTP row's statusSuffix every frame is what made the Connectivity tab
    // sluggish. Cache the first *valid* address (skip 0.0.0.0 / loopback so a
    // not-yet-up interface retries instead of sticking) and reuse it after.
    const std::string& consoleIp()
    {
        static std::string cached;
        if (cached.empty()) {
            u32 ip   = (u32)gethostid();
            u8 first = ip & 0xFF;
            if (first != 0 && first != 127) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
                cached = buf;
            }
        }
        return cached;
    }
}

SettingsScreen::SettingsScreen(std::shared_ptr<Screen> returnTo) : mReturnTo(std::move(returnTo))
{
    sprintf(mVer, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    rebuildRows();
}

void SettingsScreen::flashSaved(void)
{
    mFlashTimer = 30;
}

void SettingsScreen::rebuildRows(void)
{
    mRows.clear();
    Configuration& cfg = Configuration::getInstance();

    switch (mCategory) {
        case Category::General: {
            Row theme;
            theme.title    = "Theme";
            theme.subtitle = "Applies immediately";
            theme.control  = Control::Segmented;
            theme.section  = "APPEARANCE";
            theme.options  = {"Dark", "Light"};
            theme.getIndex = [&cfg]() { return cfg.theme() == "light" ? 1 : 0; };
            // Theme flips on A only (no onCycle): left/right on the d-pad must
            // not change it, so an accidental horizontal press can't swap themes.
            theme.onActivate = [this, &cfg]() {
                cfg.setTheme(cfg.theme() == "light" ? "dark" : "light");
                Colors::apply(cfg.theme());
                flashSaved();
            };
            mRows.push_back(std::move(theme));

            Row sort;
            sort.title    = "Default sort";
            sort.subtitle = "Order of the title grid at launch";
            sort.control  = Control::Spinner;
            for (const SortMode& m : SortMode::all()) {
                sort.options.push_back(m.label);
            }
            sort.getIndex = []() { return (int)TitleCatalog::get().sortMode(); };
            sort.onCycle  = [this](int delta) {
                int count = (int)SORT_MODES_COUNT;
                int next  = ((int)TitleCatalog::get().sortMode() + delta % count + count) % count;
                TitleCatalog::get().setSortMode((sort_t)next);
                flashSaved();
            };
            mRows.push_back(std::move(sort));
            break;
        }
        case Category::Connectivity: {
            Row ftp;
            ftp.title      = "FTP server";
            ftp.subtitle   = "Browse backups from a computer";
            ftp.control    = Control::Toggle;
            ftp.section    = "CONNECTIVITY";
            ftp.getOn      = [&cfg]() { return cfg.isFTPEnabled(); };
            ftp.onActivate = [this, &cfg]() {
                cfg.setFTPEnabled(!cfg.isFTPEnabled());
                flashSaved();
            };
            ftp.statusSuffix = [&cfg]() -> std::string {
                if (!cfg.isFTPEnabled())
                    return "";
                const std::string& ip = consoleIp();
                return ip.empty() ? "· running" : "· running on " + ip + ":50000";
            };
            mRows.push_back(std::move(ftp));

            Row pksm;
            pksm.title      = "PKSM bridge";
            pksm.subtitle   = "Allow PKSM to send and receive Pokémon saves";
            pksm.control    = Control::Toggle;
            pksm.getOn      = [&cfg]() { return cfg.isPKSMBridgeEnabled(); };
            pksm.onActivate = [this, &cfg]() {
                cfg.setPKSMBridgeEnabled(!cfg.isPKSMBridgeEnabled());
                flashSaved();
            };
            mRows.push_back(std::move(pksm));
            break;
        }
        case Category::Library: {
            auto names  = TitleCatalog::get().getCompleteTitleList();
            auto nameOf = [&names](u64 id) -> std::string {
                auto it = names.find(StringUtils::format("0x%016llX", id));
                return it != names.end() ? it->second : StringUtils::format("%016llX", id);
            };

            // Favorites list + an "+ Add favorite" affordance. (The grid's
            // hold-to-favorite gesture was dropped as an input conflict, so the
            // only way to favorite/hide a title is from here — hence the picker
            // rows, a deliberate addition over the mock's remove-only lists.)
            std::vector<u64> favs = cfg.favoriteIds();
            for (size_t i = 0; i < favs.size(); i++) {
                u64 id = favs[i];
                Row r;
                r.control   = Control::ActionPill;
                r.iconId    = id;
                r.title     = nameOf(id);
                r.subtitle  = StringUtils::format("%016llX", id);
                r.pillLabel = "Unfavorite";
                if (i == 0)
                    r.section = "FAVORITES";
                r.onActivate = [this, id, &cfg]() {
                    cfg.setFavorite(id, false);
                    flashSaved();
                    mNeedsRebuild = true;
                };
                mRows.push_back(std::move(r));
            }
            {
                Row add;
                add.control   = Control::ActionPill;
                add.title     = "Add a favorite";
                add.subtitle  = "Pin a title to the top of the grid";
                add.pillLabel = "+ Add";
                if (favs.empty())
                    add.section = "FAVORITES";
                add.onActivate = [this, names]() {
                    std::vector<std::pair<u64, std::string>> items;
                    std::vector<u64> cur = Configuration::getInstance().favoriteIds();
                    std::unordered_set<u64> ex(cur.begin(), cur.end());
                    for (const auto& kv : names) {
                        u64 id = (u64)strtoull(kv.first.c_str(), nullptr, 16);
                        if (!ex.count(id))
                            items.push_back({id, kv.second});
                    }
                    currentOverlay = std::make_shared<TitlePickerOverlay>(*this, "Add a favorite", std::move(items), [this](u64 id) {
                        Configuration::getInstance().setFavorite(id, true);
                        flashSaved();
                        mNeedsRebuild = true;
                    });
                };
                mRows.push_back(std::move(add));
            }

            std::vector<u64> hidden = cfg.hiddenIds();
            for (size_t i = 0; i < hidden.size(); i++) {
                u64 id = hidden[i];
                Row r;
                r.control   = Control::ActionPill;
                r.iconId    = id;
                r.title     = nameOf(id);
                r.subtitle  = StringUtils::format("%016llX", id);
                r.pillLabel = "Unhide";
                if (i == 0)
                    r.section = "HIDDEN TITLES";
                r.onActivate = [this, id, &cfg]() {
                    cfg.setFilter(id, false);
                    TitleCatalog::get().refreshHiddenFilter();
                    flashSaved();
                    mNeedsRebuild = true;
                };
                mRows.push_back(std::move(r));
            }
            {
                Row add;
                add.control   = Control::ActionPill;
                add.title     = "Hide a title";
                add.subtitle  = "Remove a title from the grid";
                add.pillLabel = "+ Add";
                if (hidden.empty())
                    add.section = "HIDDEN TITLES";
                add.onActivate = [this, names]() {
                    std::vector<std::pair<u64, std::string>> items;
                    std::vector<u64> cur = Configuration::getInstance().hiddenIds();
                    std::unordered_set<u64> ex(cur.begin(), cur.end());
                    for (const auto& kv : names) {
                        u64 id = (u64)strtoull(kv.first.c_str(), nullptr, 16);
                        if (!ex.count(id))
                            items.push_back({id, kv.second});
                    }
                    currentOverlay = std::make_shared<TitlePickerOverlay>(*this, "Hide a title", std::move(items), [this](u64 id) {
                        Configuration::getInstance().setFilter(id, true);
                        TitleCatalog::get().refreshHiddenFilter();
                        flashSaved();
                        mNeedsRebuild = true;
                    });
                };
                mRows.push_back(std::move(add));
            }
            break;
        }
        case Category::SaveFolders: {
            auto names  = TitleCatalog::get().getCompleteTitleList();
            auto nameOf = [&names](u64 id) -> std::string {
                auto it = names.find(StringUtils::format("0x%016llX", id));
                return it != names.end() ? it->second : StringUtils::format("%016llX", id);
            };

            bool first           = true;
            std::vector<u64> ids = cfg.additionalSaveFolderIds();
            for (u64 id : ids) {
                std::vector<std::string> folders = cfg.additionalSaveFolders(id);
                for (const std::string& path : folders) {
                    Row r;
                    r.control   = Control::ActionPill;
                    r.iconId    = id;
                    r.title     = nameOf(id);
                    r.subtitle  = path;
                    r.pillLabel = "Remove";
                    if (first) {
                        r.section = "SAVE FOLDERS";
                        first     = false;
                    }
                    r.onActivate = [this, id, path]() {
                        Configuration::getInstance().removeAdditionalSaveFolder(id, path);
                        flashSaved();
                        mNeedsRebuild = true;
                    };
                    mRows.push_back(std::move(r));
                }
                Row add;
                add.control   = Control::ActionPill;
                add.iconId    = id;
                add.title     = nameOf(id);
                add.subtitle  = "Add another save folder";
                add.pillLabel = "+ Add path";
                if (first) {
                    add.section = "SAVE FOLDERS";
                    first       = false;
                }
                add.onActivate = [this, id]() {
                    currentOverlay = std::make_shared<FolderBrowserOverlay>(*this, "Choose a save folder", [this, id](const std::string& path) {
                        Configuration::getInstance().addAdditionalSaveFolder(id, path);
                        flashSaved();
                        mNeedsRebuild = true;
                    });
                };
                mRows.push_back(std::move(add));
            }
            {
                Row addTitle;
                addTitle.control   = Control::ActionPill;
                addTitle.title     = "Add a title";
                addTitle.subtitle  = "Configure an extra save folder for a title";
                addTitle.pillLabel = "+ Add title";
                if (first)
                    addTitle.section = "SAVE FOLDERS";
                addTitle.onActivate = [this, names]() {
                    std::vector<std::pair<u64, std::string>> items;
                    for (const auto& kv : names) {
                        items.push_back({(u64)strtoull(kv.first.c_str(), nullptr, 16), kv.second});
                    }
                    currentOverlay = std::make_shared<TitlePickerOverlay>(*this, "Choose a title", std::move(items), [this](u64 id) {
                        currentOverlay = std::make_shared<FolderBrowserOverlay>(*this, "Choose a save folder", [this, id](const std::string& path) {
                            Configuration::getInstance().addAdditionalSaveFolder(id, path);
                            flashSaved();
                            mNeedsRebuild = true;
                        });
                    });
                };
                mRows.push_back(std::move(addTitle));
            }
            break;
        }
        case Category::About: {
            Row v;
            v.section  = "ABOUT";
            v.title    = "Version";
            v.subtitle = mVer;
            mRows.push_back(std::move(v));

            Row a;
            a.title    = "Author";
            a.subtitle = "Bernardo Giordano · FlagBrew";
            mRows.push_back(std::move(a));

            Row g;
            g.title    = "Source";
            g.subtitle = "github.com/FlagBrew/Checkpoint";
            mRows.push_back(std::move(g));

            Row f;
            f.section  = "CREDITS";
            f.title    = "Space Mono";
            f.subtitle = "SIL Open Font License 1.1";
            mRows.push_back(std::move(f));
            break;
        }
        default:
            break;
    }

    if (mRows.empty()) {
        mCursor = 0;
    }
    else {
        if (mCursor >= (int)mRows.size())
            mCursor = (int)mRows.size() - 1;
        // Land the cursor on the first focusable row at or after its position.
        for (int i = 0; i < (int)mRows.size(); i++) {
            int idx = (mCursor + i) % (int)mRows.size();
            if (mRows[idx].focusable) {
                mCursor = idx;
                break;
            }
        }
    }
    ensureCursorVisible();
}

void SettingsScreen::switchCategory(int delta)
{
    int count = (int)Category::COUNT;
    int next  = ((int)mCategory + delta % count + count) % count;
    mCategory = (Category)next;
    mCursor   = 0;
    mScroll   = 0;
    rebuildRows();
}

void SettingsScreen::draw(void) const
{
    SDLH_ClearScreen(COLOR_BG);

    // ---- Top bar ----
    UiKit::drawHintCircle(24, (TOPBAR_H - 20) / 2, "B");
    {
        u32 tw, th;
        SDLH_GetTextDimensions(20, "Settings", &tw, &th);
        SDLH_DrawText(20, 24 + 20 + 14, (TOPBAR_H - (int)th) / 2, COLOR_TEXT, "Settings");
    }
    {
        // Live config-path label, flashing to `success` briefly on a write.
        std::string path = "saved to sdmc:" + Configuration::getInstance().BASEPATH;
        SDL_Color c      = mFlashTimer > 0 ? COLOR_SUCCESS : COLOR_TEXT3;
        u32 tw, th;
        SDLH_GetTextDimensions(12, path.c_str(), &tw, &th);
        SDLH_DrawText(12, 1256 - (int)tw, (TOPBAR_H - (int)th) / 2, c, path.c_str());
    }

    // ---- Frame hairlines ----
    SDLH_DrawRect(0, TOPBAR_H, 1280, 1, COLOR_STROKE1);
    SDLH_DrawRect(RAIL_W, TOPBAR_H + 1, 1, 720 - TOPBAR_H - 1 - UiKit::HINTBAR_H, COLOR_STROKE1);

    // ---- Category rail ----
    for (int i = 0; i < (int)Category::COUNT; i++) {
        const int y       = CAT_Y0 + CAT_PITCH * i;
        const bool active = i == (int)mCategory;
        if (active) {
            Shapes::fillRound(CAT_X, y, CAT_W, CAT_ITEM_H, 0, COLOR_ACCENT);
        }
        SDL_Color fg = active ? COLOR_WHITE : COLOR_TEXT2;
        u32 lw, lh;
        SDLH_GetTextDimensions(15, kCategoryLabels[i], &lw, &lh);
        SDLH_DrawText(15, CAT_X + 16, y + (CAT_ITEM_H - (int)lh) / 2, fg, kCategoryLabels[i]);
    }
    // Breathing selector on the rail while it owns the cursor.
    if (mCatFocused) {
        Shapes::focusRing(CAT_X, CAT_Y0 + CAT_PITCH * (int)mCategory, CAT_W, CAT_ITEM_H, 0, COLOR_ACCENT);
    }

    // ---- Rows pane ----
    // Long lists (Library / Save folders) are paged: draw from mScroll and stop
    // before a row would spill past the hint bar, so every row stays reachable.
    const int viewBottom = UiKit::HINTBAR_Y;
    int y                = ROWS_Y0;
    for (int i = mScroll; i < (int)mRows.size(); i++) {
        const Row& row = mRows[i];
        u32 sh         = 0;
        int labelGap   = 0;
        if (!row.section.empty()) {
            SDLH_GetTextDimensions(11, row.section.c_str(), NULL, &sh);
            labelGap = (i == mScroll ? 0 : SECTION_GAP_ABOVE);
        }
        // The row rect starts below any section label; bail before drawing it (or
        // its label) if it no longer fits the viewport.
        const int rectTop = y + labelGap + (sh ? (int)sh + 8 : 0);
        if (i > mScroll && rectTop + ROW_H > viewBottom)
            break;
        if (sh) {
            UiKit::drawSectionLabel(ROW_X + 4, y + labelGap, row.section);
        }
        y = rectTop;

        // Rectangular rows (radius 0): SDL2's hand-rasterised rounded corners
        // look pixelated, so options and their selector are kept square.
        Shapes::cardRound(ROW_X, y, ROW_W, ROW_H, 0, COLOR_SURFACE, COLOR_STROKE1, 1);
        if (i == mCursor && row.focusable && !mCatFocused) {
            Shapes::focusRing(ROW_X, y, ROW_W, ROW_H, 0, COLOR_ACCENT);
        }

        // Optional leading 34px title icon (Library / Save folders).
        int textX = ROW_X + ROW_PAD;
        if (row.iconId != 0) {
            const int ic = 34, iy = y + (ROW_H - ic) / 2;
            Shapes::cardRound(textX, iy, ic, ic, 8, COLOR_TILE, COLOR_STROKE2, 1);
            if (TitleCatalog::get().iconFor(row.iconId) != NULL) {
                SDLH_DrawImageScale(TitleCatalog::get().iconFor(row.iconId), textX, iy, ic, ic);
            }
            textX += ic + 12;
        }

        // Left: title over subtitle (+ optional green status suffix).
        u32 tH, sH;
        SDLH_GetTextDimensions(15, "Ag", NULL, &tH);
        SDLH_GetTextDimensions(12, "Ag", NULL, &sH);
        const int stackH = (int)tH + 4 + (int)sH;
        int ty           = y + (ROW_H - stackH) / 2;
        SDLH_DrawText(15, textX, ty, COLOR_TEXT, row.title.c_str());
        ty += (int)tH + 4;
        u32 subW;
        SDLH_GetTextDimensions(12, row.subtitle.c_str(), &subW, NULL);
        SDLH_DrawText(12, textX, ty, row.iconId != 0 ? COLOR_TEXT3 : COLOR_TEXT2, trimToFit(row.subtitle, ROW_W - 260, 12).c_str());
        if (row.statusSuffix) {
            std::string suffix = row.statusSuffix();
            if (!suffix.empty()) {
                SDLH_DrawText(12, textX + (int)subW + 6, ty, COLOR_SUCCESS, suffix.c_str());
            }
        }

        // Right: the control, right-aligned ending ROW_PAD from the row edge.
        const int rightEdge = ROW_X + ROW_W - ROW_PAD;
        switch (row.control) {
            case Control::ActionPill: {
                u32 pw, ph;
                SDLH_GetTextDimensions(13, row.pillLabel.c_str(), &pw, &ph);
                const int pillH = (int)ph + 14, pillW = (int)pw + 28;
                const int px = rightEdge - pillW, py = y + (ROW_H - pillH) / 2;
                Shapes::fillRound(px, py, pillW, pillH, 0, COLOR_ACCENT_TINT);
                SDLH_DrawText(13, px + 14, py + (pillH - (int)ph) / 2, COLOR_ACCENT_LIGHT, row.pillLabel.c_str());
                break;
            }
            case Control::Segmented: {
                int w = UiKit::segmentedWidth(row.options);
                UiKit::drawSegmented(rightEdge - w, y + (ROW_H - 42) / 2, row.options, row.getIndex ? row.getIndex() : 0);
                break;
            }
            case Control::Spinner: {
                // ‹ label › : arrows text-3, value 14/500 text, as one group.
                // The arrows are Nintendo-Extended-font chevrons (U+E149/E14A);
                // the plain Unicode triangles are absent from the shared font and
                // rendered as tofu boxes.
                const std::string& val = row.options[row.getIndex ? row.getIndex() : 0];
                u32 vw, vh, aw;
                SDLH_GetTextDimensions(14, val.c_str(), &vw, &vh);
                SDLH_GetTextDimensions(14, "", &aw, NULL);
                const int gap   = 12;
                const int group = (int)aw + gap + (int)vw + gap + (int)aw;
                int cx          = rightEdge - group;
                const int cy    = y + (ROW_H - (int)vh) / 2;
                SDLH_DrawText(14, cx, cy, COLOR_TEXT3, "");
                cx += (int)aw + gap;
                SDLH_DrawText(14, cx, cy, COLOR_TEXT, val.c_str());
                cx += (int)vw + gap;
                SDLH_DrawText(14, cx, cy, COLOR_TEXT3, "");
                break;
            }
            case Control::Toggle: {
                UiKit::drawToggle(rightEdge - 46, y + (ROW_H - 27) / 2, row.getOn ? row.getOn() : false);
                break;
            }
            default:
                break;
        }

        y += ROW_H + ROW_GAP;
    }

    // Paged-list affordance: chevrons at the right edge when rows sit off-screen.
    if (!mRows.empty()) {
        // Nintendo-Extended-font up/down chevrons (U+E147/E148); the plain
        // Unicode triangles are missing from the shared font (tofu boxes).
        const int cx = ROW_X + ROW_W + 8;
        if (mScroll > 0) {
            SDLH_DrawText(16, cx, ROWS_Y0, COLOR_TEXT3, "");
        }
        if (lastVisibleRow(mScroll) < (int)mRows.size() - 1) {
            SDLH_DrawText(16, cx, UiKit::HINTBAR_Y - 24, COLOR_TEXT3, "");
        }
    }

    // ---- Hint bar ----
    if (mCatFocused) {
        UiKit::drawHintBar({
            {"A", "Open"},
            {"B", "Back"},
        });
    }
    else {
        UiKit::drawHintBar({
            {"A", "Change"},
            {"B", "Categories"},
        });
    }
}

void SettingsScreen::moveCursor(int dir)
{
    const int n = (int)mRows.size();
    for (int step = 0; step < n; step++) {
        mCursor = (mCursor + dir + n) % n;
        if (mRows[mCursor].focusable)
            return;
    }
}

bool SettingsScreen::hasFocusableRow(void) const
{
    for (const Row& r : mRows) {
        if (r.focusable)
            return true;
    }
    return false;
}

int SettingsScreen::lastVisibleRow(int scroll) const
{
    const int viewBottom = UiKit::HINTBAR_Y;
    int y                = ROWS_Y0;
    int last             = scroll;
    for (int i = scroll; i < (int)mRows.size(); i++) {
        const int rectTop = y + sectionExtra(mRows[i].section, i == scroll);
        // Always keep the first row; past that, stop before one overruns the bar.
        if (i > scroll && rectTop + ROW_H > viewBottom)
            break;
        last = i;
        y    = rectTop + ROW_H + ROW_GAP;
    }
    return last;
}

void SettingsScreen::ensureCursorVisible(void)
{
    if (mRows.empty()) {
        mScroll = 0;
        return;
    }
    mScroll = std::clamp(mScroll, 0, (int)mRows.size() - 1);
    if (mCursor < mScroll) {
        mScroll = mCursor;
        return;
    }
    while (mCursor > lastVisibleRow(mScroll))
        mScroll++;
}

void SettingsScreen::update(const InputState& input)
{
    if (mFlashTimer > 0)
        mFlashTimer--;

    // A structural action last frame (or one from a dismissed overlay) marked
    // the row list stale; rebuild before we touch any Row.
    if (mNeedsRebuild) {
        mNeedsRebuild = false;
        rebuildRows();
    }

    const u64 kdown = input.kDown;

    // L/R jump categories from anywhere (shoulder shortcut).
    if (kdown & HidNpadButton_L) {
        switchCategory(-1);
        return;
    }
    if (kdown & HidNpadButton_R) {
        switchCategory(1);
        return;
    }

    // ---- Category rail focused: Up/Down pick a category, Right/A enters the
    // rows, B leaves for the MainScreen we came from (deferred so we don't
    // destroy ourselves mid-update; main() swaps after doUpdate). ----
    if (mCatFocused) {
        if (kdown & HidNpadButton_B) {
            g_pendingScreen = mReturnTo;
            return;
        }
        if (kdown & HidNpadButton_Up) {
            switchCategory(-1);
        }
        else if (kdown & HidNpadButton_Down) {
            switchCategory(1);
        }
        if ((kdown & (HidNpadButton_Right | HidNpadButton_A)) && hasFocusableRow()) {
            mCatFocused = false; // rebuildRows() already parked mCursor on the first focusable row
        }
        return;
    }

    // ---- A row focused. B or Left steps back out to the category rail. ----
    if ((kdown & HidNpadButton_B) || (kdown & HidNpadButton_Left)) {
        mCatFocused = true;
        return;
    }
    if (mRows.empty()) {
        mCatFocused = true;
        return;
    }

    // Up/down move the row cursor, skipping non-focusable (label/empty) rows.
    if (kdown & HidNpadButton_Up) {
        moveCursor(-1);
        ensureCursorVisible();
    }
    else if (kdown & HidNpadButton_Down) {
        moveCursor(1);
        ensureCursorVisible();
    }

    if (mCursor >= (int)mRows.size() || !mRows[mCursor].focusable)
        return;
    const Row& row = mRows[mCursor];

    // Right cycles a Spinner forward (Left is reserved for "back to rail", so a
    // Spinner cycles forward-only via the d-pad — it wraps — or with A). A
    // operates the focused control. (A structural onActivate may invalidate
    // `row`; it sets mNeedsRebuild and we don't dereference `row` again.)
    if (kdown & HidNpadButton_Right) {
        if (row.onCycle)
            row.onCycle(1);
    }
    if (kdown & HidNpadButton_A) {
        if (row.onActivate)
            row.onActivate();
        else if (row.onCycle)
            row.onCycle(1);
    }
}

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
#include "configuration.hpp"
#include "glyphs.hpp"
#include "gui.hpp"
#include "loader.hpp"
#include "main.hpp"
#include "server.hpp"
#include "textpool.hpp"
#include "title.hpp"
#include "util.hpp"
#include <3ds.h>
#include <sys/statvfs.h>

// Section indices, used across draw/update.
enum SectionId { SEC_GENERAL = 0, SEC_LIBRARY, SEC_FOLDERS, SEC_NETWORK, SEC_ABOUT };

namespace {
    struct Section {
        const char* letter; // section chip glyph (font-safe single character)
        const char* name;
        const char* blurb;
    };

    // Order matches the redesign mockup's section rail.
    const Section SECTIONS[] = {
        {"G", "General",
            "Cartridge scan, system saves, Wi-Fi transfer and restore confirmation. Changes write straight to config.json on your SD card."},
        {"L", "Library", "Favorites and hidden titles - the filter and favorites lists."},
        {"F", "Folders", "Per-title extra save and extdata folders on your SD card."},
        {"N", "Network", "Wi-Fi send / receive of backups between consoles."},
        {"i", "About", "Version, credits and storage usage."},
    };
    constexpr size_t SECTION_COUNT = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

    struct ToggleRow {
        const char* name;
        const char* sub;
    };

    // General section rows, in draw order. Row index maps 1:1 to the mutators
    // used in update(): 0 light theme, 1 scan_cart, 2 nand_saves,
    // 3 transfer_enabled, 4 confirm_restore.
    const ToggleRow GENERAL_ROWS[] = {
        {"Light theme", "Use the light color palette"},
        {"Scan game cartridge", "Detect the inserted cart on launch"},
        {"Show system (NAND) saves", "Include system & DSiWare titles"},
        {"Enable Wi-Fi transfer", "Send / receive backups over network"},
        {"Confirm before restore", "Ask before overwriting a save"},
    };
    constexpr size_t GENERAL_COUNT = sizeof(GENERAL_ROWS) / sizeof(GENERAL_ROWS[0]);

    constexpr int SAVED_FLASH_FRAMES = 90;

    // Resolves a title id to its short description, falling back to the hex id
    // for titles not present in the catalog (e.g. a cart that isn't inserted).
    // Copies out one string, not a full Title.
    std::string titleName(u64 id)
    {
        std::string name;
        if (TitleCatalog::get().nameById(name, id) && !name.empty()) {
            return name;
        }
        return StringUtils::format("%016llX", id);
    }

}

void SettingsScreen::rebuildRows()
{
    Configuration& cfg = Configuration::getInstance();

    mLibraryRows.clear();
    for (u64 id : cfg.favoriteIds()) {
        mLibraryRows.push_back({titleName(id), "Favorite", COLOR_GOLD, RM_FAVORITE, id, 0});
    }
    for (u64 id : cfg.filterIds()) {
        mLibraryRows.push_back({titleName(id), "Hidden", COLOR_FAINT, RM_FILTER, id, 0});
    }

    mFolderRows.clear();
    for (auto& entry : cfg.saveFolders()) {
        for (size_t i = 0; i < entry.second.size(); i++) {
            mFolderRows.push_back({titleName(entry.first), StringUtils::UTF16toUTF8(entry.second[i]), COLOR_TEAL, RM_SAVE_FOLDER, entry.first, i});
        }
    }
    for (auto& entry : cfg.extdataFolders()) {
        for (size_t i = 0; i < entry.second.size(); i++) {
            mFolderRows.push_back({titleName(entry.first), StringUtils::UTF16toUTF8(entry.second[i]), COLOR_BLUE, RM_EXTDATA_FOLDER, entry.first, i});
        }
    }
}

SettingsScreen::SettingsScreen(std::shared_ptr<Screen> parent) : mParent(std::move(parent)), navHid(SECTION_COUNT, 1)
{
    C2D_PlainImageTint(&flagTint, COLOR_TEAL, 1.0f);
    rebuildRows();
}

bool SettingsScreen::sectionInteractive(size_t section) const
{
    return section == SEC_GENERAL || section == SEC_LIBRARY || section == SEC_FOLDERS;
}

size_t SettingsScreen::contentRowCount(size_t section) const
{
    switch (section) {
        case SEC_GENERAL:
            return GENERAL_COUNT;
        case SEC_LIBRARY:
            return mLibraryRows.size();
        case SEC_FOLDERS:
            return mFolderRows.size();
        default:
            return 0;
    }
}

void SettingsScreen::drawHints(int screenW, int y, const std::string& text) const
{
    TextPool::get().drawCentered(text, 0, screenW, y, 0.47f, COLOR_MUTED);
}

void SettingsScreen::drawScrollbar(int totalRows) const
{
    if (totalRows <= VISIBLE_ROWS) {
        return;
    }
    const int trackX = 315, trackY = 30, trackH = VISIBLE_ROWS * 34 - 2;
    C2D_DrawRectSolid(trackX, trackY, 0.5f, 3, trackH, COLOR_LINE);
    const float frac = (float)VISIBLE_ROWS / (float)totalRows;
    int thumbH       = (int)(trackH * frac);
    if (thumbH < 12) {
        thumbH = 12;
    }
    const float posFrac = (float)contentOffset / (float)(totalRows - VISIBLE_ROWS);
    const int thumbY    = trackY + (int)((trackH - thumbH) * posFrac);
    C2D_DrawRectSolid(trackX, thumbY, 0.5f, 3, thumbH, COLOR_ACCENT);
}

void SettingsScreen::drawToggleRow(int y, const char* name, const char* sub, bool on, bool focused) const
{
    const int rowH = 32;
    if (focused) {
        C2D_DrawRectSolid(6, y, 0.5f, 308, rowH, C2D_Color32(122, 66, 196, 40));
        Gui::drawOutline(6, y, 308, rowH, 1, COLOR_ACCENT);
    }
    TextPool::get().draw(name, 14, y + 4, 0.44f, COLOR_TEXT);
    TextPool::get().draw(sub, 14, y + 18, 0.36f, COLOR_FAINT);

    // Rectangular toggle, right-aligned: a track with a sliding square knob.
    const int tw = 34, th = 18, tx = 320 - 14 - tw, ty = y + (rowH - th) / 2;
    C2D_DrawRectSolid(tx, ty, 0.5f, tw, th, on ? COLOR_ACCENT : COLOR_RAISED);
    const int knob = 14, kpad = 2;
    const int knobX = on ? tx + tw - kpad - knob : tx + kpad;
    // Knob contrasts with its track in both themes: white on the accent fill when
    // on, a muted grey on the raised track when off (white-on-light-grey is unreadable).
    C2D_DrawRectSolid(knobX, ty + kpad, 0.6f, knob, th - 2 * kpad, on ? COLOR_WHITE : COLOR_MUTED);
}

void SettingsScreen::drawListRow(int y, const std::string& primary, const std::string& secondary, u32 pipColor, bool focused, bool removable) const
{
    const int rowH = 32;
    if (focused) {
        C2D_DrawRectSolid(6, y, 0.5f, 308, rowH, C2D_Color32(122, 66, 196, 40));
        Gui::drawOutline(6, y, 308, rowH, 1, COLOR_ACCENT);
    }
    C2D_DrawRectSolid(14, y + (rowH - 7) / 2, 0.5f, 7, 7, pipColor);
    TextPool::get().draw(TextPool::get().truncate(primary, 250, 0.44f), 28, y + 4, 0.44f, COLOR_TEXT);
    TextPool::get().draw(TextPool::get().truncate(secondary, 270, 0.36f), 28, y + 18, 0.36f, COLOR_FAINT);
    if (removable && focused) {
        std::string tag = std::string(GLYPH_X);
        float w         = TextPool::get().width(tag, 0.44f);
        TextPool::get().draw(tag, 320 - 14 - w, y + 8, 0.44f, COLOR_DANGER);
    }
}

void SettingsScreen::drawEmptyState(const char* title, const char* body) const
{
    C2D_DrawRectSolid(140, 74, 0.5f, 40, 40, COLOR_CARD);
    Gui::drawOutline(140, 74, 40, 40, 1, COLOR_LINE);
    const float gw = TextPool::get().width(GLYPH_EMPTY, 0.7f);
    const float lf = fontGetInfo(NULL)->lineFeed;
    TextPool::get().draw(GLYPH_EMPTY, 140 + (40 - gw) / 2, 74 + (40 - 0.7f * lf) / 2, 0.7f, COLOR_FAINT);
    float w = TextPool::get().width(title, 0.5f);
    TextPool::get().draw(title, ceilf((320 - w) / 2), 124, 0.5f, COLOR_MUTED);
    TextPool::get().drawWrapped(body, 160, 146, 0.4f, COLOR_FAINT, 240.0f, 0.5f, C2D_AlignCenter);
}

void SettingsScreen::drawTop(void) const
{
    C2D_TargetClear(g_top, COLOR_BASE);
    C2D_TargetClear(g_bottom, COLOR_BASE);
    C2D_SceneBegin(g_top);

    // Header bar.
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 24, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 24, 0.5f, 400, 1, COLOR_LINE);
    C2D_ImageTint brandTint;
    C2D_PlainImageTint(&brandTint, Configuration::getInstance().theme() == "light" ? COLOR_BLUE : COLOR_TEAL, 1.0f);
    C2D_DrawImageAt(flag, 6, 3, 0.5f, &brandTint, 1.0f, 1.0f);
    float nameX = 6 + ceilf(flag.subtex->width * 1.0f) + 6;
    TextPool::get().draw("Settings", nameX, 4, 0.5f, COLOR_TEXT);
    {
        std::string right = "config.json";
        float w           = TextPool::get().width(right, 0.42f);
        TextPool::get().draw(right, 400 - 6 - w, 6, 0.42f, COLOR_FAINT);
    }

    // Section rail (left) + section blurb card (right).
    const size_t sel     = navHid.index();
    const float lineFeed = fontGetInfo(NULL)->lineFeed;
    for (size_t i = 0; i < SECTION_COUNT; i++) {
        const int rowY   = 34 + (int)i * 32;
        const bool isSel = i == sel;
        if (isSel) {
            C2D_DrawRectSolid(8, rowY, 0.5f, 150, 28, C2D_Color32(122, 66, 196, 40));
            Gui::drawOutline(8, rowY, 150, 28, 1, contentFocus ? COLOR_LINE : COLOR_ACCENT);
        }
        const int chipY = rowY + 4, chipSz = 20;
        C2D_DrawRectSolid(14, chipY, 0.5f, chipSz, chipSz, isSel ? COLOR_ACCENT : COLOR_RAISED);
        {
            const float cw = TextPool::get().width(SECTIONS[i].letter, 0.5f);
            const float cy = chipY + (chipSz - 0.5f * lineFeed) / 2;
            TextPool::get().draw(SECTIONS[i].letter, 14 + (chipSz - cw) / 2, cy, 0.5f, isSel ? COLOR_WHITE : COLOR_MUTED);
        }
        // Name is vertically centered against the chip square rather than top-aligned.
        const float ny = chipY + (chipSz - 0.44f * lineFeed) / 2;
        TextPool::get().draw(SECTIONS[i].name, 42, ny, 0.44f, isSel ? COLOR_TEXT : COLOR_MUTED);
    }

    // Blurb card.
    C2D_DrawRectSolid(166, 34, 0.5f, 226, 160, COLOR_CARD);
    Gui::drawOutline(166, 34, 226, 160, 1, COLOR_LINE);
    TextPool::get().draw(SECTIONS[sel].name, 178, 44, 0.5f, COLOR_TEXT);
    TextPool::get().drawWrapped(SECTIONS[sel].blurb, 178, 66, 0.44f, COLOR_MUTED, 202);

    // Footer.
    C2D_DrawRectSolid(0, 220, 0.5f, 400, 20, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 219, 0.5f, 400, 1, COLOR_LINE);
    if (contentFocus) {
        drawHints(400, 223, std::string(GLYPH_B) + " Back to sections");
    }
    else {
        std::string hints = std::string(GLYPH_DPAD) + " Section     ";
        if (sectionInteractive(sel)) {
            hints += std::string(GLYPH_A) + " Edit     ";
        }
        hints += std::string(GLYPH_B) + " Back";
        drawHints(400, 223, hints);
    }
}

void SettingsScreen::drawGeneral(void) const
{
    Configuration& cfg             = Configuration::getInstance();
    const bool vals[GENERAL_COUNT] = {cfg.theme() == "light", cfg.shouldScanCard(), cfg.nandSaves(), cfg.transferEnabled(), cfg.confirmRestore()};
    for (size_t i = 0; i < GENERAL_COUNT; i++) {
        const int rowY = 30 + (int)i * 34;
        drawToggleRow(rowY, GENERAL_ROWS[i].name, GENERAL_ROWS[i].sub, vals[i], contentFocus && contentCursor == (int)i);
    }
    if (contentFocus) {
        drawHints(320, 223, std::string(GLYPH_DPAD) + " Move     " + GLYPH_A + " Toggle     " + GLYPH_B + " Back");
    }
    else {
        drawHints(320, 223, std::string(GLYPH_A) + " Edit");
    }
}

void SettingsScreen::drawFolders(void) const
{
    const std::vector<Row>& rows = mFolderRows;
    if (rows.empty()) {
        drawEmptyState("No extra folders", "Add a per-title save or extdata folder on your SD card.");
    }
    else {
        for (int i = 0; i < VISIBLE_ROWS && contentOffset + i < (int)rows.size(); i++) {
            const Row& r   = rows[contentOffset + i];
            const int rowY = 30 + i * 34;
            drawListRow(rowY, r.primary, r.secondary, r.pip, contentFocus && contentCursor == contentOffset + i, true);
        }
        drawScrollbar((int)rows.size());
    }
    if (!contentFocus) {
        drawHints(320, 223, std::string(GLYPH_A) + " Edit");
    }
    else if (rows.empty()) {
        drawHints(320, 223, std::string(GLYPH_A) + " +Save     " + GLYPH_Y + " +Extdata     " + GLYPH_B + " Back");
    }
    else {
        drawHints(320, 223, std::string(GLYPH_A) + " +Save   " + GLYPH_Y + " +Extdata   " + GLYPH_X + " Remove   " + GLYPH_B + " Back");
    }
}

void SettingsScreen::drawLibrary(void) const
{
    const std::vector<Row>& rows = mLibraryRows;
    if (rows.empty()) {
        drawEmptyState("No favorites or hidden titles", "Favorite a title or hide it from the library grid.");
    }
    else {
        for (int i = 0; i < VISIBLE_ROWS && contentOffset + i < (int)rows.size(); i++) {
            const Row& r   = rows[contentOffset + i];
            const int rowY = 30 + i * 34;
            drawListRow(rowY, r.primary, r.secondary, r.pip, contentFocus && contentCursor == contentOffset + i, true);
        }
        drawScrollbar((int)rows.size());
    }
    if (!contentFocus) {
        drawHints(320, 223, std::string(GLYPH_A) + " Edit");
    }
    else if (rows.empty()) {
        drawHints(320, 223, std::string(GLYPH_A) + " +Favorite     " + GLYPH_Y + " +Hide     " + GLYPH_B + " Back");
    }
    else {
        drawHints(320, 223, std::string(GLYPH_A) + " +Fav   " + GLYPH_Y + " +Hide   " + GLYPH_X + " Remove   " + GLYPH_B + " Back");
    }
}

void SettingsScreen::drawNetwork(void) const
{
    Configuration& cfg = Configuration::getInstance();
    const bool on      = cfg.transferEnabled();
    int y              = 40;
    auto field         = [&](const char* label, const std::string& value, u32 valColor) {
        TextPool::get().draw(label, 20, y, 0.4f, COLOR_FAINT);
        TextPool::get().draw(value, 20, y + 14, 0.46f, valColor);
        y += 44;
    };
    field("Wi-Fi transfer", on ? "Enabled" : "Disabled", on ? COLOR_TEAL : COLOR_MUTED);

    std::string address = Server::getAddress();
    field("This console's address", address.empty() ? "Unavailable" : address, address.empty() ? COLOR_MUTED : COLOR_TEXT);

    TextPool::get().draw("Send / receive", 20, y, 0.4f, COLOR_FAINT);
    TextPool::get().drawWrapped(
        "Use the Transfer button on the main screen to send a backup or wait to receive one.", 20, y + 14, 0.4f, COLOR_MUTED, 280);

    drawHints(320, 223, std::string(GLYPH_B) + " Back");
}

void SettingsScreen::drawAbout(void) const
{
    std::string ver = StringUtils::versionString();

    C2D_ImageTint brandTint;
    C2D_PlainImageTint(&brandTint, Configuration::getInstance().theme() == "light" ? COLOR_BLUE : COLOR_TEAL, 1.0f);
    C2D_DrawImageAt(flag, 20, 36, 0.5f, &brandTint, 1.0f, 1.0f);
    float x = 20 + ceilf(flag.subtex->width * 1.0f) + 8;
    TextPool::get().draw("Checkpoint", x, 34, 0.62f, COLOR_TEXT);
    TextPool::get().draw(ver, x, 54, 0.42f, COLOR_FAINT);

    int y     = 82;
    auto line = [&](const char* label, const std::string& value) {
        TextPool::get().draw(label, 20, y, 0.4f, COLOR_FAINT);
        float lw = TextPool::get().width(value, 0.42f);
        TextPool::get().draw(value, 300 - lw, y, 0.42f, COLOR_TEXT);
        y += 24;
    };
    line("Author", "Bernardo Giordano");
    line("License", "GPLv3");

    struct statvfs st;
    if (statvfs("sdmc:/", &st) == 0 && st.f_blocks > 0) {
        u64 total = (u64)st.f_blocks * st.f_frsize;
        u64 avail = (u64)st.f_bavail * st.f_frsize;
        u64 used  = total > avail ? total - avail : 0;
        line("SD card", StringUtils::humanBytes(used) + " used of " + StringUtils::humanBytes(total));
    }

    drawHints(320, 223, std::string(GLYPH_B) + " Back");
}

void SettingsScreen::drawBottom(void) const
{
    C2D_SceneBegin(g_bottom);

    const size_t sel = navHid.index();

    // Header.
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 24, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 24, 0.5f, 320, 1, COLOR_LINE);
    TextPool::get().draw(SECTIONS[sel].name, 10, 4, 0.5f, COLOR_TEXT);
    if (sectionInteractive(sel) && savedTimer > 0) {
        std::string saved = "\xE2\x97\x8F Saved"; // "● Saved"
        float w           = TextPool::get().width(saved, 0.4f);
        TextPool::get().draw(saved, 320 - 10 - w, 6, 0.4f, COLOR_TEAL);
    }

    // Footer bar (content is drawn by the per-section helpers, which also emit
    // the section-specific footer hints over it).
    C2D_DrawRectSolid(0, 220, 0.5f, 320, 20, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 219, 0.5f, 320, 1, COLOR_LINE);

    switch (sel) {
        case SEC_GENERAL:
            drawGeneral();
            break;
        case SEC_LIBRARY:
            drawLibrary();
            break;
        case SEC_FOLDERS:
            drawFolders();
            break;
        case SEC_NETWORK:
            drawNetwork();
            break;
        case SEC_ABOUT:
            drawAbout();
            break;
    }
}

void SettingsScreen::toggleGeneral(int idx)
{
    Configuration& cfg = Configuration::getInstance();
    switch (idx) {
        case 0:
            cfg.setTheme(cfg.theme() == "light" ? "dark" : "light");
            Colors::apply(cfg.theme());
            break;
        case 1:
            cfg.setScanCard(!cfg.shouldScanCard());
            break;
        case 2:
            cfg.setNandSaves(!cfg.nandSaves());
            break;
        case 3:
            cfg.setTransferEnabled(!cfg.transferEnabled());
            break;
        case 4:
            cfg.setConfirmRestore(!cfg.confirmRestore());
            break;
    }
    // Cart scan and NAND saves change which titles the grid loads.
    if (idx == 1 || idx == 2) {
        g_titlesDirty = true;
    }
    savedTimer = SAVED_FLASH_FRAMES;
}

void SettingsScreen::update(const InputState& input)
{
    u32 kDown        = hidKeysDown();
    const size_t sel = navHid.index();

    // Touch toggles a General row directly, from either focus state.
    if (sel == SEC_GENERAL && (kDown & KEY_TOUCH)) {
        for (int i = 0; i < (int)GENERAL_COUNT; i++) {
            const int rowY = 30 + i * 34;
            if (input.py >= rowY && input.py < rowY + 32 && input.px >= 6 && input.px < 314) {
                contentFocus  = true;
                contentCursor = i;
                toggleGeneral(i);
                break;
            }
        }
        return;
    }

    if (!contentFocus) {
        navHid.update(SECTION_COUNT);
        if (kDown & KEY_A) {
            if (sectionInteractive(navHid.index())) {
                contentFocus  = true;
                contentCursor = 0;
                contentOffset = 0;
                rebuildRows(); // refresh the row cache on entering an interactive section
            }
        }
        else if (kDown & KEY_B) {
            Configuration::getInstance().commit(); // flush any deferred General toggles
            g_pendingScreen = mParent;             // leave Settings, restore the parent screen
            return;
        }
    }
    else {
        const int rows = (int)contentRowCount(sel);

        if ((kDown & KEY_DOWN) && contentCursor + 1 < rows) {
            contentCursor++;
        }
        else if ((kDown & KEY_UP) && contentCursor > 0) {
            contentCursor--;
        }

        if (sel == SEC_GENERAL) {
            if (kDown & KEY_A) {
                toggleGeneral(contentCursor);
            }
            else if (kDown & KEY_B) {
                contentFocus = false;
            }
        }
        else if (sel == SEC_LIBRARY) {
            if (kDown & KEY_A) {
                currentOverlay = std::make_shared<TitlePickerOverlay>(*this, "Add favorite", [this](u64 id) {
                    Configuration::getInstance().addFavorite(id);
                    g_titlesDirty = true;
                    savedTimer    = SAVED_FLASH_FRAMES;
                    rebuildRows();
                });
            }
            else if (kDown & KEY_Y) {
                currentOverlay = std::make_shared<TitlePickerOverlay>(*this, "Hide title", [this](u64 id) {
                    Configuration::getInstance().addFilter(id);
                    g_titlesDirty = true;
                    savedTimer    = SAVED_FLASH_FRAMES;
                    rebuildRows();
                });
            }
            else if ((kDown & KEY_X) && rows > 0) {
                if (contentCursor < (int)mLibraryRows.size()) {
                    const Row& r = mLibraryRows[contentCursor];
                    if (r.removeKind == RM_FAVORITE) {
                        Configuration::getInstance().removeFavorite(r.id);
                    }
                    else {
                        Configuration::getInstance().removeFilter(r.id);
                    }
                    g_titlesDirty = true;
                    savedTimer    = SAVED_FLASH_FRAMES;
                    rebuildRows();
                }
            }
            else if (kDown & KEY_B) {
                contentFocus = false;
            }
        }
        else if (sel == SEC_FOLDERS) {
            if (kDown & KEY_A) {
                currentOverlay = std::make_shared<TitlePickerOverlay>(*this, "Add save folder", [this](u64 id) {
                    currentOverlay = std::make_shared<FolderBrowserOverlay>(*this, "Choose save folder", [this, id](const std::u16string& path) {
                        Configuration::getInstance().addSaveFolder(id, path);
                        g_titlesDirty = true;
                        savedTimer    = SAVED_FLASH_FRAMES;
                        rebuildRows();
                    });
                });
            }
            else if (kDown & KEY_Y) {
                currentOverlay = std::make_shared<TitlePickerOverlay>(*this, "Add extdata folder", [this](u64 id) {
                    currentOverlay = std::make_shared<FolderBrowserOverlay>(*this, "Choose extdata folder", [this, id](const std::u16string& path) {
                        Configuration::getInstance().addExtdataFolder(id, path);
                        g_titlesDirty = true;
                        savedTimer    = SAVED_FLASH_FRAMES;
                        rebuildRows();
                    });
                });
            }
            else if ((kDown & KEY_X) && rows > 0) {
                if (contentCursor < (int)mFolderRows.size()) {
                    const Row& r = mFolderRows[contentCursor];
                    if (r.removeKind == RM_SAVE_FOLDER) {
                        Configuration::getInstance().removeSaveFolder(r.id, r.idx);
                    }
                    else {
                        Configuration::getInstance().removeExtdataFolder(r.id, r.idx);
                    }
                    g_titlesDirty = true;
                    savedTimer    = SAVED_FLASH_FRAMES;
                    rebuildRows();
                }
            }
            else if (kDown & KEY_B) {
                contentFocus = false;
            }
        }

        // Clamp cursor + scroll window against the (possibly shrunk) row count.
        const int now = (int)contentRowCount(sel);
        if (contentCursor >= now) {
            contentCursor = now > 0 ? now - 1 : 0;
        }
        if (contentCursor < contentOffset) {
            contentOffset = contentCursor;
        }
        else if (contentCursor >= contentOffset + VISIBLE_ROWS) {
            contentOffset = contentCursor - VISIBLE_ROWS + 1;
        }
        if (contentOffset > 0 && contentOffset + VISIBLE_ROWS > now) {
            contentOffset = now > VISIBLE_ROWS ? now - VISIBLE_ROWS : 0;
        }
    }

    if (savedTimer > 0) {
        savedTimer--;
    }
}

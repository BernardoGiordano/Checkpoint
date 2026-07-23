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
#include "ftpserver.hpp"
#include "glyphs.hpp"
#include "gui.hpp"
#include "i18n.hpp"
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
        const char* letter;   // section chip glyph (font-safe single character)
        const char* nameKey;  // i18n key, resolved at draw time
        const char* blurbKey; // i18n key, resolved at draw time
    };

    // Order matches the redesign mockup's section rail. Text held as i18n keys,
    // never as localized strings, so a language change is seen live at draw time.
    const Section SECTIONS[] = {
        {"G", "settings.general.title", "settings.general.blurb"},
        {"L", "settings.library.title", "settings.library.blurb"},
        {"F", "settings.folders.title", "settings.folders.blurb"},
        {"N", "settings.network.title", "settings.network.blurb"},
        {"i", "settings.about.title", "settings.about.blurb"},
    };
    constexpr size_t SECTION_COUNT = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

    struct ToggleRow {
        const char* nameKey;
        const char* subKey;
    };

    // General section rows, in draw order. Row index maps 1:1 to the mutators
    // used in update(): 0 light theme, 1 scan_cart, 2 nand_saves,
    // 3 dsiware_saves, 4 transfer_enabled, 5 confirm_restore, 6 quick_backup.
    const ToggleRow GENERAL_ROWS[] = {
        {"settings.general.light_theme", "settings.general.light_theme.sub"},
        {"settings.general.scan_cart", "settings.general.scan_cart.sub"},
        {"settings.general.nand_saves", "settings.general.nand_saves.sub"},
        {"settings.general.dsiware_saves", "settings.general.dsiware_saves.sub"},
        {"settings.general.transfer", "settings.general.transfer.sub"},
        {"settings.general.confirm_restore", "settings.general.confirm_restore.sub"},
        {"settings.general.quick_backup", "settings.general.quick_backup.sub"},
    };
    constexpr size_t GENERAL_COUNT = sizeof(GENERAL_ROWS) / sizeof(GENERAL_ROWS[0]);
    // Extra General row shown first, before the toggles: a language cycler
    // (en -> it -> es -> en). Kept out of the toggle arrays since it has a
    // value, not an on/off state; the toggle rows follow at idx 1..GENERAL_COUNT.
    constexpr size_t GENERAL_ROW_TOTAL = GENERAL_COUNT + 1;
    constexpr int LANGUAGE_ROW         = 0;

    // Supported language codes, in cycle order. Adding a language only means
    // appending here (plus its romfs i18n.json entries and isSupported()).
    const std::string LANGUAGES[]   = {"en", "it", "es", "fr", "de", "pt", "nl", "ja"};
    constexpr size_t LANGUAGE_COUNT = sizeof(LANGUAGES) / sizeof(LANGUAGES[0]);

    // Display name for a language code, in its own language (never localized).
    const char* languageName(const std::string& code)
    {
        if (code == "it")
            return "Italiano";
        if (code == "es")
            return "Español";
        if (code == "fr")
            return "Français";
        if (code == "de")
            return "Deutsch";
        if (code == "pt")
            return "Português";
        if (code == "nl")
            return "Nederlands";
        if (code == "ja")
            return "日本語";
        if (code == "zh")
            return "简体中文";
        return "English";
    }

    size_t languageIndex(const std::string& code)
    {
        for (size_t i = 0; i < LANGUAGE_COUNT; i++) {
            if (LANGUAGES[i] == code)
                return i;
        }
        return 0;
    }

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
    return section == SEC_GENERAL || section == SEC_LIBRARY || section == SEC_FOLDERS || section == SEC_NETWORK;
}

size_t SettingsScreen::contentRowCount(size_t section) const
{
    switch (section) {
        case SEC_GENERAL:
            return GENERAL_ROW_TOTAL;
        case SEC_LIBRARY:
            return mLibraryRows.size();
        case SEC_FOLDERS:
            return mFolderRows.size();
        case SEC_NETWORK:
            return 1; // the single FTP-server toggle
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

void SettingsScreen::drawToggleRow(int y, const std::string& name, const std::string& sub, bool on, bool focused) const
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

void SettingsScreen::drawValueRow(int y, const std::string& name, const std::string& sub, const std::string& value, bool focused) const
{
    const int rowH = 32;
    if (focused) {
        C2D_DrawRectSolid(6, y, 0.5f, 308, rowH, C2D_Color32(122, 66, 196, 40));
        Gui::drawOutline(6, y, 308, rowH, 1, COLOR_ACCENT);
    }
    TextPool::get().draw(name, 14, y + 4, 0.44f, COLOR_TEXT);
    TextPool::get().draw(sub, 14, y + 18, 0.36f, COLOR_FAINT);

    // Right-aligned value in the accent color, matching the toggle's right edge.
    const float vw = TextPool::get().width(value, 0.44f);
    TextPool::get().draw(value, 320 - 14 - vw, y + (rowH - 0.44f * fontGetInfo(NULL)->lineFeed) / 2, 0.44f, COLOR_ACCENT);
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

void SettingsScreen::drawEmptyState(const std::string& title, const std::string& body) const
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
    C2D_PlainImageTint(&brandTint, COLOR_ACCENT, 1.0f);
    C2D_DrawImageAt(flag, 6, 3, 0.5f, &brandTint, 1.0f, 1.0f);
    float nameX = 6 + ceilf(flag.subtex->width * 1.0f) + 6;
    TextPool::get().draw(i18n::t("settings.title"), nameX, 4, 0.5f, COLOR_TEXT);
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
        TextPool::get().draw(i18n::t(SECTIONS[i].nameKey), 42, ny, 0.44f, isSel ? COLOR_TEXT : COLOR_MUTED);
    }

    // Blurb card.
    C2D_DrawRectSolid(166, 34, 0.5f, 226, 160, COLOR_CARD);
    Gui::drawOutline(166, 34, 226, 160, 1, COLOR_LINE);
    TextPool::get().draw(i18n::t(SECTIONS[sel].nameKey), 178, 44, 0.5f, COLOR_TEXT);
    TextPool::get().drawWrapped(i18n::t(SECTIONS[sel].blurbKey), 178, 66, 0.44f, COLOR_MUTED, 202);

    // Footer.
    C2D_DrawRectSolid(0, 220, 0.5f, 400, 20, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 219, 0.5f, 400, 1, COLOR_LINE);
    if (contentFocus) {
        drawHints(400, 223, std::string(GLYPH_B) + " " + i18n::t("settings.hint.back_sections"));
    }
    else {
        std::string hints = std::string(GLYPH_DPAD) + " " + i18n::t("settings.hint.section") + "     ";
        if (sectionInteractive(sel)) {
            hints += std::string(GLYPH_A) + " " + i18n::t("hint.edit") + "     ";
        }
        hints += std::string(GLYPH_B) + " " + i18n::t("hint.back");
        drawHints(400, 223, hints);
    }
}

void SettingsScreen::drawGeneral(void) const
{
    Configuration& cfg             = Configuration::getInstance();
    const bool vals[GENERAL_COUNT] = {cfg.theme() == "light", cfg.shouldScanCard(), cfg.nandSaves(), cfg.dsiwareSaves(), cfg.transferEnabled(),
        cfg.confirmRestore(), cfg.quickBackup()};
    // Windowed like the Library/Folders lists: more rows than fit scroll under a
    // right-edge scrollbar. Stride 34 matches drawScrollbar's track geometry.
    for (int i = 0; i < VISIBLE_ROWS && contentOffset + i < (int)GENERAL_ROW_TOTAL; i++) {
        const int idx  = contentOffset + i;
        const int rowY = 30 + i * 34;
        if (idx == LANGUAGE_ROW) {
            drawValueRow(rowY, i18n::t("settings.general.language"), i18n::t("settings.general.language.sub"), languageName(i18n::language()),
                contentFocus && contentCursor == idx);
        }
        else {
            // Toggles follow the language row, so shift by one into the toggle arrays.
            const int g = idx - 1;
            drawToggleRow(rowY, i18n::t(GENERAL_ROWS[g].nameKey), i18n::t(GENERAL_ROWS[g].subKey), vals[g], contentFocus && contentCursor == idx);
        }
    }
    drawScrollbar((int)GENERAL_ROW_TOTAL);
    if (contentFocus) {
        drawHints(320, 223,
            std::string(GLYPH_DPAD) + " " + i18n::t("hint.move") + "     " + GLYPH_A + " " + i18n::t("hint.toggle") + "     " + GLYPH_B + " " +
                i18n::t("hint.back"));
    }
    else {
        drawHints(320, 223, std::string(GLYPH_A) + " " + i18n::t("hint.edit"));
    }
}

void SettingsScreen::drawFolders(void) const
{
    const std::vector<Row>& rows = mFolderRows;
    if (rows.empty()) {
        drawEmptyState(i18n::t("settings.folders.empty.title"), i18n::t("settings.folders.empty.body"));
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
        drawHints(320, 223, std::string(GLYPH_A) + " " + i18n::t("hint.edit"));
    }
    else if (rows.empty()) {
        drawHints(320, 223,
            std::string(GLYPH_A) + " " + i18n::t("hint.add_save") + "     " + GLYPH_Y + " " + i18n::t("hint.add_extdata") + "     " + GLYPH_B + " " +
                i18n::t("hint.back"));
    }
    else {
        drawHints(320, 223,
            std::string(GLYPH_A) + " " + i18n::t("hint.add_save") + "   " + GLYPH_Y + " " + i18n::t("hint.add_extdata") + "   " + GLYPH_X + " " +
                i18n::t("hint.remove") + "   " + GLYPH_B + " " + i18n::t("hint.back"));
    }
}

void SettingsScreen::drawLibrary(void) const
{
    const std::vector<Row>& rows = mLibraryRows;
    if (rows.empty()) {
        drawEmptyState(i18n::t("settings.library.empty.title"), i18n::t("settings.library.empty.body"));
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
        drawHints(320, 223, std::string(GLYPH_A) + " " + i18n::t("hint.edit"));
    }
    else if (rows.empty()) {
        drawHints(320, 223,
            std::string(GLYPH_A) + " " + i18n::t("hint.add_favorite") + "     " + GLYPH_Y + " " + i18n::t("hint.hide") + "     " + GLYPH_B + " " +
                i18n::t("hint.back"));
    }
    else {
        drawHints(320, 223,
            std::string(GLYPH_A) + " " + i18n::t("hint.add_fav") + "   " + GLYPH_Y + " " + i18n::t("hint.hide") + "   " + GLYPH_X + " " +
                i18n::t("hint.remove") + "   " + GLYPH_B + " " + i18n::t("hint.back"));
    }
}

void SettingsScreen::drawNetwork(void) const
{
    Configuration& cfg = Configuration::getInstance();

    // Row 0: the interactive FTP-server toggle.
    const bool ftpOn = cfg.isFTPEnabled();
    drawToggleRow(26, i18n::t("settings.network.ftp"), i18n::t("settings.network.ftp.sub"), ftpOn, contentFocus && contentCursor == 0);

    int y      = 68;
    auto field = [&](const std::string& label, const std::string& value, u32 valColor) {
        TextPool::get().draw(label, 20, y, 0.4f, COLOR_FAINT);
        TextPool::get().draw(value, 20, y + 14, 0.46f, valColor);
        y += 38;
    };

    std::string ftpAddress = FTPServer::getAddress();
    field(i18n::t("settings.network.ftp_address"),
        ftpOn ? (ftpAddress.empty() ? i18n::t("common.unavailable") : ftpAddress) : i18n::t("common.disabled"),
        ftpOn && !ftpAddress.empty() ? COLOR_TEAL : COLOR_MUTED);

    const bool transferOn = cfg.transferEnabled();
    field(i18n::t("settings.network.transfer"), transferOn ? i18n::t("common.enabled") : i18n::t("common.disabled"),
        transferOn ? COLOR_TEAL : COLOR_MUTED);

    std::string address = Server::getAddress();
    field(i18n::t("settings.network.memory_logs"), address.empty() ? i18n::t("common.unavailable") : address + "/logs/memory",
        address.empty() ? COLOR_MUTED : COLOR_TEXT);
    field(i18n::t("settings.network.file_logs"), address.empty() ? i18n::t("common.unavailable") : address + "/logs/file",
        address.empty() ? COLOR_MUTED : COLOR_TEXT);

    if (contentFocus) {
        drawHints(320, 223, std::string(GLYPH_A) + " " + i18n::t("hint.toggle") + "     " + GLYPH_B + " " + i18n::t("hint.back"));
    }
    else {
        drawHints(320, 223, std::string(GLYPH_A) + " " + i18n::t("hint.edit"));
    }
}

void SettingsScreen::drawAbout(void) const
{
    std::string ver = StringUtils::versionString();

    C2D_ImageTint brandTint;
    C2D_PlainImageTint(&brandTint, COLOR_ACCENT, 1.0f);
    C2D_DrawImageAt(flag, 20, 36, 0.5f, &brandTint, 1.0f, 1.0f);
    float x = 20 + ceilf(flag.subtex->width * 1.0f) + 8;
    TextPool::get().draw("Checkpoint", x, 34, 0.62f, COLOR_TEXT);
    TextPool::get().draw(ver, x, 54, 0.42f, COLOR_FAINT);

    int y     = 82;
    auto line = [&](const std::string& label, const std::string& value) {
        TextPool::get().draw(label, 20, y, 0.4f, COLOR_FAINT);
        float lw = TextPool::get().width(value, 0.42f);
        TextPool::get().draw(value, 300 - lw, y, 0.42f, COLOR_TEXT);
        y += 24;
    };
    line(i18n::t("settings.about.author"), "Bernardo Giordano");
    line(i18n::t("settings.about.license"), "GPLv3");

    struct statvfs st;
    if (statvfs("sdmc:/", &st) == 0 && st.f_blocks > 0) {
        u64 total = (u64)st.f_blocks * st.f_frsize;
        u64 avail = (u64)st.f_bavail * st.f_frsize;
        u64 used  = total > avail ? total - avail : 0;
        line(i18n::t("settings.about.sd_card"), i18n::t("settings.about.sd_usage", {StringUtils::humanBytes(used), StringUtils::humanBytes(total)}));
    }

    drawHints(320, 223, std::string(GLYPH_B) + " " + i18n::t("hint.back"));
}

void SettingsScreen::drawBottom(void) const
{
    C2D_SceneBegin(g_bottom);

    const size_t sel = navHid.index();

    // Header.
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 24, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 24, 0.5f, 320, 1, COLOR_LINE);
    TextPool::get().draw(i18n::t(SECTIONS[sel].nameKey), 10, 4, 0.5f, COLOR_TEXT);
    if (sectionInteractive(sel) && savedTimer > 0) {
        std::string saved = "\xE2\x97\x8F " + i18n::t("common.saved"); // "● Saved"
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
    if (idx == LANGUAGE_ROW) {
        // Cycle en -> it -> es -> en. Persist to config and switch live; every
        // t() call from the next frame resolves against the new language.
        const std::string next = LANGUAGES[(languageIndex(i18n::language()) + 1) % LANGUAGE_COUNT];
        cfg.setLanguage(next);
        i18n::setLanguage(next);
        // The content-keyed TextPool cache needs no flush: new-language strings
        // are cache misses that re-parse lazily; stale entries age out on rebuild.
        savedTimer = SAVED_FLASH_FRAMES;
        return;
    }
    // Toggles follow the language row, so shift by one into the toggle arrays.
    const int g = idx - 1;
    switch (g) {
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
            cfg.setDSiWareSaves(!cfg.dsiwareSaves());
            break;
        case 4:
            cfg.setTransferEnabled(!cfg.transferEnabled());
            break;
        case 5:
            cfg.setConfirmRestore(!cfg.confirmRestore());
            break;
        case 6:
            cfg.setQuickBackup(!cfg.quickBackup());
            break;
    }
    // Cart scan, NAND saves and DSiWare saves change which titles the grid loads.
    if (g == 1 || g == 2 || g == 3) {
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
        for (int i = 0; i < VISIBLE_ROWS && contentOffset + i < (int)GENERAL_ROW_TOTAL; i++) {
            const int idx  = contentOffset + i;
            const int rowY = 30 + i * 34;
            if (input.py >= rowY && input.py < rowY + 32 && input.px >= 6 && input.px < 314) {
                contentFocus  = true;
                contentCursor = idx;
                toggleGeneral(idx);
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
        else if (sel == SEC_NETWORK) {
            if (kDown & KEY_A) {
                // Live toggle: the FTP loop thread reads isFTPEnabled() each
                // iteration, so this takes effect without a restart. The SD
                // write is deferred to commit() on leaving Settings.
                Configuration::getInstance().setFTPEnabled(!Configuration::getInstance().isFTPEnabled());
                savedTimer = SAVED_FLASH_FRAMES;
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

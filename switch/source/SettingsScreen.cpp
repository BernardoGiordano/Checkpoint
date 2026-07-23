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
#include "gfxutils.hpp"
#include "i18n.hpp"
#include "logging.hpp"
#include "main.hpp"
#include "server.hpp"
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

    // i18n keys, resolved at draw time so a live language change is seen without
    // rebuilding (the rail draws every frame).
    const std::array<const char*, 6> kCategoryKeys = {"settings.cat.general", "settings.cat.library", "settings.cat.save_folders",
        "settings.cat.connectivity", "settings.cat.logs", "settings.cat.about"};

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

    // Log viewer: monospace body text, tight line spacing so a useful number of
    // lines fit the pane.
    constexpr int LOG_FONT     = 11;
    constexpr int LOG_LINE_GAP = 3;
    constexpr int LOG_PAD      = 16;

    // Bytes consumed by the UTF-8 lead byte `c` (1 if not a lead byte). Local so
    // the log wrapper never splits a multi-byte glyph without pulling in the
    // font-cache unicode helpers.
    int utf8Len(unsigned char c)
    {
        if (c < 0x80)
            return 1;
        if ((c >> 5) == 0x6)
            return 2;
        if ((c >> 4) == 0xE)
            return 3;
        if ((c >> 3) == 0x1E)
            return 4;
        return 1;
    }

    // Height of one section-label slot (label + 8px breathing room below it).
    int labelSlotH(void)
    {
        u32 sh;
        Gfx::GetTextDimensions(11, "Ag", NULL, &sh);
        return (int)sh + 8;
    }

    // Vertical space the section label of a NON-first row reserves above its
    // rect. The very first drawn row always reserves labelSlotH() unconditionally
    // (a sticky header slot), so its space never depends on whether it happens to
    // carry a section — that constant top slot is what keeps scrolling from
    // jumping when a group's header row scrolls off.
    int inlineSectionExtra(const std::string& section, const std::string& prevSection)
    {
        if (section.empty() || section == prevSection)
            return 0;
        return SECTION_GAP_ABOVE + labelSlotH();
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
    snprintf(mVer, sizeof(mVer), "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
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
            theme.title    = i18n::t("settings.general.theme");
            theme.subtitle = i18n::t("settings.general.theme.sub");
            theme.control  = Control::Segmented;
            theme.section  = i18n::t("settings.section.appearance");
            theme.options  = {i18n::t("settings.theme.dark"), i18n::t("settings.theme.light")};
            theme.getIndex = [&cfg]() { return cfg.theme() == "light" ? 1 : 0; };
            // Theme flips on A only (no onCycle): left/right on the d-pad must
            // not change it, so an accidental horizontal press can't swap themes.
            theme.onActivate = [this, &cfg]() {
                cfg.setTheme(cfg.theme() == "light" ? "dark" : "light");
                Colors::apply(cfg.theme());
                flashSaved();
            };
            mRows.push_back(std::move(theme));

            // Language, in the Appearance section next to Theme. Spinner, cycles
            // left/right like Default sort (more than two options reads better
            // as a cycler than as a segmented control). The option names stay
            // in their own language. A change defers a rebuild so every row
            // picks up the new language.
            Row language;
            language.title    = i18n::t("settings.general.language");
            language.subtitle = i18n::t("settings.general.language.sub");
            language.control  = Control::Spinner;
            language.section  = i18n::t("settings.section.appearance");
            for (const std::string& code : LANGUAGES) {
                language.options.push_back(languageName(code));
            }
            language.getIndex = []() { return (int)languageIndex(i18n::language()); };
            language.onCycle  = [this, &cfg](int delta) {
                int count               = (int)LANGUAGE_COUNT;
                int next                = ((int)languageIndex(i18n::language()) + delta % count + count) % count;
                const std::string& code = LANGUAGES[next];
                cfg.setLanguage(code);
                i18n::setLanguage(code);
                flashSaved();
                mNeedsRebuild = true;
            };
            mRows.push_back(std::move(language));

            Row sort;
            sort.title    = i18n::t("settings.general.sort");
            sort.subtitle = i18n::t("settings.general.sort.sub");
            sort.control  = Control::Spinner;
            for (const SortMode& m : SortMode::all()) {
                sort.options.push_back(i18n::t(m.label));
            }
            sort.getIndex = []() { return (int)TitleCatalog::get().sortMode(); };
            sort.onCycle  = [this](int delta) {
                int count = (int)SORT_MODES_COUNT;
                int next  = ((int)TitleCatalog::get().sortMode() + delta % count + count) % count;
                TitleCatalog::get().setSortMode((sort_t)next);
                flashSaved();
            };
            mRows.push_back(std::move(sort));

            Row confirmRestore;
            confirmRestore.title      = i18n::t("settings.general.confirm_restore");
            confirmRestore.subtitle   = i18n::t("settings.general.confirm_restore.sub");
            confirmRestore.control    = Control::Toggle;
            confirmRestore.section    = i18n::t("settings.section.safety");
            confirmRestore.getOn      = [&cfg]() { return cfg.isConfirmRestoreEnabled(); };
            confirmRestore.onActivate = [this, &cfg]() {
                cfg.setConfirmRestoreEnabled(!cfg.isConfirmRestoreEnabled());
                flashSaved();
            };
            mRows.push_back(std::move(confirmRestore));

            Row quickBackup;
            quickBackup.title      = i18n::t("settings.general.quick_backup");
            quickBackup.subtitle   = i18n::t("settings.general.quick_backup.sub");
            quickBackup.control    = Control::Toggle;
            quickBackup.section    = i18n::t("settings.section.safety");
            quickBackup.getOn      = [&cfg]() { return cfg.isQuickBackupEnabled(); };
            quickBackup.onActivate = [this, &cfg]() {
                cfg.setQuickBackupEnabled(!cfg.isQuickBackupEnabled());
                flashSaved();
            };
            mRows.push_back(std::move(quickBackup));

            Row verifyRestore;
            verifyRestore.title      = i18n::t("settings.general.verify_restore");
            verifyRestore.subtitle   = i18n::t("settings.general.verify_restore.sub");
            verifyRestore.control    = Control::Toggle;
            verifyRestore.section    = i18n::t("settings.section.safety");
            verifyRestore.getOn      = [&cfg]() { return cfg.isVerifyRestoreEnabled(); };
            verifyRestore.onActivate = [this, &cfg]() {
                cfg.setVerifyRestoreEnabled(!cfg.isVerifyRestoreEnabled());
                flashSaved();
            };
            mRows.push_back(std::move(verifyRestore));
            break;
        }
        case Category::Connectivity: {
            // Log server rows are read-only status mirrors: info rows, no
            // controls, not focusable (nothing to operate here).
            auto info = [](std::string title, std::string subtitle, std::string section = "") {
                Row r;
                r.title     = std::move(title);
                r.subtitle  = std::move(subtitle);
                r.section   = std::move(section);
                r.focusable = false;
                return r;
            };

            Row ftp;
            ftp.title      = i18n::t("settings.conn.ftp");
            ftp.subtitle   = i18n::t("settings.conn.ftp.sub");
            ftp.control    = Control::Toggle;
            ftp.section    = i18n::t("settings.section.connectivity");
            ftp.getOn      = [&cfg]() { return cfg.isFTPEnabled(); };
            ftp.onActivate = [this, &cfg]() {
                cfg.setFTPEnabled(!cfg.isFTPEnabled());
                flashSaved();
            };
            ftp.statusSuffix = [&cfg]() -> std::string {
                if (!cfg.isFTPEnabled())
                    return "";
                const std::string& ip = consoleIp();
                return ip.empty() ? i18n::t("settings.conn.running") : i18n::t("settings.conn.running_on", {ip});
            };
            mRows.push_back(std::move(ftp));

            // Master switch for the wireless save-transfer feature; when off the
            // Send/Receive buttons stay hidden and the /transfer handlers unregistered.
            Row transfer;
            transfer.title      = i18n::t("settings.conn.transfer");
            transfer.subtitle   = i18n::t("settings.conn.transfer.sub");
            transfer.control    = Control::Toggle;
            transfer.section    = i18n::t("settings.section.connectivity");
            transfer.getOn      = [&cfg]() { return cfg.isTransferEnabled(); };
            transfer.onActivate = [this, &cfg]() {
                cfg.setTransferEnabled(!cfg.isTransferEnabled());
                flashSaved();
            };
            mRows.push_back(std::move(transfer));

            // Address of the built-in HTTP log server (parity with the 3DS build).
            // Open it from any browser on the same network to read the logs.
            const std::string addr = Server::getAddress();
            mRows.push_back(info(i18n::t("settings.conn.log_server"), addr.empty() ? i18n::t("common.unavailable") : addr + "/logs/memory",
                i18n::t("settings.section.logs")));
            if (!addr.empty()) {
                mRows.push_back(info(i18n::t("settings.conn.full_log"), addr + "/logs/file"));
            }
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
                r.pillLabel = i18n::t("settings.library.unfavorite");
                if (i == 0)
                    r.section = i18n::t("settings.section.favorites");
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
                add.title     = i18n::t("settings.library.add_favorite");
                add.subtitle  = i18n::t("settings.library.add_favorite.sub");
                add.pillLabel = i18n::t("settings.pill.add");
                if (favs.empty())
                    add.section = i18n::t("settings.section.favorites");
                add.onActivate = [this, names]() {
                    std::vector<std::pair<u64, std::string>> items;
                    std::vector<u64> cur = Configuration::getInstance().favoriteIds();
                    std::unordered_set<u64> ex(cur.begin(), cur.end());
                    for (const auto& kv : names) {
                        u64 id = (u64)strtoull(kv.first.c_str(), nullptr, 16);
                        if (!ex.count(id))
                            items.push_back({id, kv.second});
                    }
                    currentOverlay =
                        std::make_shared<TitlePickerOverlay>(*this, i18n::t("settings.library.add_favorite"), std::move(items), [this](u64 id) {
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
                r.pillLabel = i18n::t("settings.library.unhide");
                if (i == 0)
                    r.section = i18n::t("settings.section.hidden");
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
                add.title     = i18n::t("settings.library.hide_title");
                add.subtitle  = i18n::t("settings.library.hide_title.sub");
                add.pillLabel = i18n::t("settings.pill.add");
                if (hidden.empty())
                    add.section = i18n::t("settings.section.hidden");
                add.onActivate = [this, names]() {
                    std::vector<std::pair<u64, std::string>> items;
                    std::vector<u64> cur = Configuration::getInstance().hiddenIds();
                    std::unordered_set<u64> ex(cur.begin(), cur.end());
                    for (const auto& kv : names) {
                        u64 id = (u64)strtoull(kv.first.c_str(), nullptr, 16);
                        if (!ex.count(id))
                            items.push_back({id, kv.second});
                    }
                    currentOverlay =
                        std::make_shared<TitlePickerOverlay>(*this, i18n::t("settings.library.hide_title"), std::move(items), [this](u64 id) {
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

            // One block per save kind, mirroring the Library tab's shape: the
            // configured (title, folder) rows with a Remove pill, then a single
            // "+ Add" row whose flow picks a title of that kind first and the
            // folder second. The pickers list only titles owning that save
            // kind, so system/BCAT titles never show up here.
            struct Kind {
                std::string section;
                std::string addTitle;
                std::string addSubtitle;
                u8 saveDataType;
                std::vector<u64> (Configuration::*ids)(void);
                std::vector<std::string> (Configuration::*folders)(u64);
                void (Configuration::*add)(u64, const std::string&);
                void (Configuration::*remove)(u64, const std::string&);
            };
            const Kind kinds[] = {
                {i18n::t("settings.section.user_folders"), i18n::t("settings.folders.add_save"), i18n::t("settings.folders.add_save.sub"),
                    FsSaveDataType_Account, &Configuration::additionalSaveFolderIds, &Configuration::additionalSaveFolders,
                    &Configuration::addAdditionalSaveFolder, &Configuration::removeAdditionalSaveFolder},
                {i18n::t("settings.section.device_folders"), i18n::t("settings.folders.add_device"), i18n::t("settings.folders.add_device.sub"),
                    FsSaveDataType_Device, &Configuration::additionalDeviceSaveFolderIds, &Configuration::additionalDeviceSaveFolders,
                    &Configuration::addAdditionalDeviceSaveFolder, &Configuration::removeAdditionalDeviceSaveFolder},
            };

            for (const Kind& kind : kinds) {
                // Copy what the lambdas need out of `kind`: the Kind array is a
                // local, so capturing a reference to it would dangle by the time
                // a row's onActivate runs.
                const auto addFn    = kind.add;
                const auto removeFn = kind.remove;
                const u8 type       = kind.saveDataType;

                bool first = true;
                for (u64 id : (cfg.*kind.ids)()) {
                    for (const std::string& path : (cfg.*kind.folders)(id)) {
                        Row r;
                        r.control   = Control::ActionPill;
                        r.iconId    = id;
                        r.title     = nameOf(id);
                        r.subtitle  = path;
                        r.pillLabel = i18n::t("settings.pill.remove");
                        if (first) {
                            r.section = kind.section;
                            first     = false;
                        }
                        r.onActivate = [this, id, path, removeFn]() {
                            (Configuration::getInstance().*removeFn)(id, path);
                            TitleCatalog::get().refreshDirectories(id);
                            flashSaved();
                            mNeedsRebuild = true;
                        };
                        mRows.push_back(std::move(r));
                    }
                }

                Row add;
                add.control   = Control::ActionPill;
                add.title     = kind.addTitle;
                add.subtitle  = kind.addSubtitle;
                add.pillLabel = i18n::t("settings.pill.add");
                if (first)
                    add.section = kind.section;
                add.onActivate = [this, addFn, type]() {
                    auto items     = TitleCatalog::get().titleListForSaveType(type);
                    currentOverlay = std::make_shared<TitlePickerOverlay>(
                        *this, i18n::t("settings.folders.choose_title"), std::move(items), [this, addFn](u64 id) {
                            currentOverlay = std::make_shared<FolderBrowserOverlay>(
                                *this, i18n::t("settings.folders.choose_folder"), [this, addFn, id](const std::string& path) {
                                    (Configuration::getInstance().*addFn)(id, path);
                                    TitleCatalog::get().refreshDirectories(id);
                                    flashSaved();
                                    mNeedsRebuild = true;
                                });
                        });
                };
                mRows.push_back(std::move(add));
            }
            break;
        }
        case Category::Logs: {
            // No interactive rows: the log pane is drawn directly (drawLogs) and
            // scrolled by mLogScroll while focused.
            rebuildLogLines();
            break;
        }
        case Category::About: {
            Row v;
            v.section  = i18n::t("settings.section.about");
            v.title    = i18n::t("settings.about.version");
            v.subtitle = mVer;
            mRows.push_back(std::move(v));

            Row a;
            a.title    = i18n::t("settings.about.author");
            a.subtitle = "Bernardo Giordano";
            mRows.push_back(std::move(a));

            Row g;
            g.title    = i18n::t("settings.about.source");
            g.subtitle = "github.com/BernardoGiordano/Checkpoint";
            mRows.push_back(std::move(g));

            Row f;
            f.section  = i18n::t("settings.section.credits");
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

int SettingsScreen::logLinesPerPage(void) const
{
    u32 lh;
    Gfx::GetTextDimensions(LOG_FONT, "Ag", NULL, &lh, FontFamily::Mono);
    const int lineH = (int)lh + LOG_LINE_GAP;
    const int paneH = UiKit::HINTBAR_Y - ROWS_Y0 - 2 * LOG_PAD;
    return std::max(1, paneH / lineH);
}

void SettingsScreen::rebuildLogLines(void)
{
    mLogLines.clear();

    const std::string logs = Logging::getApplicationLogs();
    const int paneW        = ROW_W - 2 * LOG_PAD;

    // Greedy wrap of one logical line to the pane width. Measurement is on the
    // mono font; only runs when the Logs category is (re)built, not per frame.
    auto wrap = [&](const std::string& s) {
        if (s.empty()) {
            mLogLines.emplace_back();
            return;
        }
        std::string cur;
        const char* src = s.c_str();
        while (*src != '\0') {
            const int cs      = utf8Len((unsigned char)*src);
            std::string glyph = std::string(src, cs);
            std::string cand  = cur + glyph;
            u32 w;
            Gfx::GetTextDimensions(LOG_FONT, cand.c_str(), &w, NULL, FontFamily::Mono);
            if (w > (u32)paneW && !cur.empty()) {
                mLogLines.push_back(cur);
                cur = glyph;
            }
            else {
                cur = std::move(cand);
            }
            src += cs;
        }
        mLogLines.push_back(cur);
    };

    size_t start = 0;
    while (start <= logs.size()) {
        size_t nl        = logs.find('\n', start);
        size_t len       = (nl == std::string::npos) ? logs.size() - start : nl - start;
        std::string line = logs.substr(start, len);
        // Drop a trailing '\r' if the log ever carried CRLF.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        wrap(line);
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }

    // Start at the newest lines (bottom), which are the ones a user came to see.
    mLogScroll    = std::max(0, (int)mLogLines.size() - logLinesPerPage());
    mLogHeldTimer = 0;
}

void SettingsScreen::drawLogs(void) const
{
    const int paneY = ROWS_Y0;
    const int paneH = UiKit::HINTBAR_Y - ROWS_Y0 - ROW_GAP;
    Shapes::cardRound(ROW_X, paneY, ROW_W, paneH, 0, COLOR_SURFACE, COLOR_STROKE1, 1);

    if (mLogLines.empty()) {
        const std::string msg = i18n::t("settings.logs.empty");
        u32 tw, th;
        Gfx::GetTextDimensions(13, msg.c_str(), &tw, &th);
        Gfx::DrawText(13, ROW_X + (ROW_W - (int)tw) / 2, paneY + (paneH - (int)th) / 2, COLOR_TEXT3, msg.c_str());
        return;
    }

    u32 lh;
    Gfx::GetTextDimensions(LOG_FONT, "Ag", NULL, &lh, FontFamily::Mono);
    const int lineH = (int)lh + LOG_LINE_GAP;
    const int per   = logLinesPerPage();

    int y             = paneY + LOG_PAD;
    const int lastRow = std::min((int)mLogLines.size(), mLogScroll + per);
    for (int i = mLogScroll; i < lastRow; i++) {
        Gfx::DrawText(LOG_FONT, ROW_X + LOG_PAD, y, COLOR_MONO_VAL, mLogLines[i].c_str(), FontFamily::Mono);
        y += lineH;
    }

    // Off-screen affordance chevrons (Nintendo-Extended up/down glyphs).
    const int cx = ROW_X + ROW_W + 8;
    if (mLogScroll > 0) {
        Gfx::DrawText(16, cx, paneY, COLOR_TEXT3, "");
    }
    if (mLogScroll + per < (int)mLogLines.size()) {
        Gfx::DrawText(16, cx, UiKit::HINTBAR_Y - 24, COLOR_TEXT3, "");
    }
}

void SettingsScreen::draw(void) const
{
    Gfx::ClearScreen(COLOR_BG);

    // ---- Top bar ----
    UiKit::drawHintCircle(24, (TOPBAR_H - 20) / 2, "-");
    {
        const std::string title = i18n::t("settings.title");
        u32 tw, th;
        Gfx::GetTextDimensions(20, title.c_str(), &tw, &th);
        Gfx::DrawText(20, 24 + 20 + 14, (TOPBAR_H - (int)th) / 2, COLOR_TEXT, title.c_str());
    }
    {
        // Live config-path label, flashing to `success` briefly on a write.
        std::string path = i18n::t("settings.saved_to") + " sdmc:" + Configuration::getInstance().BASEPATH;
        Color c          = mFlashTimer > 0 ? COLOR_SUCCESS : COLOR_TEXT3;
        u32 tw, th;
        Gfx::GetTextDimensions(12, path.c_str(), &tw, &th);
        Gfx::DrawText(12, 1256 - (int)tw, (TOPBAR_H - (int)th) / 2, c, path.c_str());
    }

    // ---- Frame hairlines ----
    Gfx::DrawRect(0, TOPBAR_H, 1280, 1, COLOR_STROKE1);
    Gfx::DrawRect(RAIL_W, TOPBAR_H + 1, 1, 720 - TOPBAR_H - 1 - UiKit::HINTBAR_H, COLOR_STROKE1);

    // ---- Category rail ----
    for (int i = 0; i < (int)Category::COUNT; i++) {
        const int y       = CAT_Y0 + CAT_PITCH * i;
        const bool active = i == (int)mCategory;
        if (active) {
            Shapes::fillRound(CAT_X, y, CAT_W, CAT_ITEM_H, 0, COLOR_ACCENT);
        }
        Color fg                = active ? COLOR_WHITE : COLOR_TEXT2;
        const std::string label = i18n::t(kCategoryKeys[i]);
        u32 lw, lh;
        Gfx::GetTextDimensions(15, label.c_str(), &lw, &lh);
        Gfx::DrawText(15, CAT_X + 16, y + (CAT_ITEM_H - (int)lh) / 2, fg, label.c_str());
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
        // The first drawn row always reserves a sticky header slot, showing the
        // section it belongs to (inherited when the group's header row itself has
        // scrolled above the fold). Later rows only reserve when they start a new
        // group. The row rect starts below whatever slot was reserved; bail before
        // drawing it (or its label) if it no longer fits the viewport.
        const bool atTop  = (i == mScroll);
        const int extra   = atTop ? labelSlotH() : inlineSectionExtra(row.section, mRows[i - 1].section);
        const int rectTop = y + extra;
        if (i > mScroll && rectTop + ROW_H > viewBottom)
            break;
        if (atTop) {
            const std::string& sec = sectionAt(i);
            if (!sec.empty())
                UiKit::drawSectionLabel(ROW_X + 4, y, sec);
        }
        else if (row.section != mRows[i - 1].section && !row.section.empty()) {
            UiKit::drawSectionLabel(ROW_X + 4, y + SECTION_GAP_ABOVE, row.section);
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
                Gfx::DrawImageScale(TitleCatalog::get().iconFor(row.iconId), textX, iy, ic, ic);
            }
            textX += ic + 12;
        }

        // Left: title over subtitle (+ optional green status suffix).
        u32 tH, sH;
        Gfx::GetTextDimensions(15, "Ag", NULL, &tH);
        Gfx::GetTextDimensions(12, "Ag", NULL, &sH);
        const int stackH = (int)tH + 4 + (int)sH;
        int ty           = y + (ROW_H - stackH) / 2;
        Gfx::DrawText(15, textX, ty, COLOR_TEXT, row.title.c_str());
        ty += (int)tH + 4;
        u32 subW;
        Gfx::GetTextDimensions(12, row.subtitle.c_str(), &subW, NULL);
        Gfx::DrawText(12, textX, ty, row.iconId != 0 ? COLOR_TEXT3 : COLOR_TEXT2, trimToFit(row.subtitle, ROW_W - 260, 12).c_str());
        if (row.statusSuffix) {
            std::string suffix = row.statusSuffix();
            if (!suffix.empty()) {
                Gfx::DrawText(12, textX + (int)subW + 6, ty, COLOR_SUCCESS, suffix.c_str());
            }
        }

        // Right: the control, right-aligned ending ROW_PAD from the row edge.
        const int rightEdge = ROW_X + ROW_W - ROW_PAD;
        switch (row.control) {
            case Control::ActionPill: {
                u32 pw, ph;
                Gfx::GetTextDimensions(13, row.pillLabel.c_str(), &pw, &ph);
                const int pillH = (int)ph + 14, pillW = (int)pw + 28;
                const int px = rightEdge - pillW, py = y + (ROW_H - pillH) / 2;
                Shapes::fillRound(px, py, pillW, pillH, 0, COLOR_ACCENT_TINT);
                Gfx::DrawText(13, px + 14, py + (pillH - (int)ph) / 2, COLOR_ACCENT_LIGHT, row.pillLabel.c_str());
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
                Gfx::GetTextDimensions(14, val.c_str(), &vw, &vh);
                Gfx::GetTextDimensions(14, "", &aw, NULL);
                const int gap   = 12;
                const int group = (int)aw + gap + (int)vw + gap + (int)aw;
                int cx          = rightEdge - group;
                const int cy    = y + (ROW_H - (int)vh) / 2;
                Gfx::DrawText(14, cx, cy, COLOR_TEXT3, "");
                cx += (int)aw + gap;
                Gfx::DrawText(14, cx, cy, COLOR_TEXT, val.c_str());
                cx += (int)vw + gap;
                Gfx::DrawText(14, cx, cy, COLOR_TEXT3, "");
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

    // The Logs category has no interactive rows; it renders a scrollable pane.
    if (mCategory == Category::Logs) {
        drawLogs();
    }

    // Paged-list affordance: chevrons at the right edge when rows sit off-screen.
    if (!mRows.empty()) {
        // Nintendo-Extended-font up/down chevrons (U+E147/E148); the plain
        // Unicode triangles are missing from the shared font (tofu boxes).
        const int cx = ROW_X + ROW_W + 8;
        if (mScroll > 0) {
            Gfx::DrawText(16, cx, ROWS_Y0, COLOR_TEXT3, "");
        }
        if (lastVisibleRow(mScroll) < (int)mRows.size() - 1) {
            Gfx::DrawText(16, cx, UiKit::HINTBAR_Y - 24, COLOR_TEXT3, "");
        }
    }

    // ---- Hint bar ----
    if (mCatFocused) {
        UiKit::drawHintBar({
            {"A", i18n::t("hint.open")},
            {"B", i18n::t("hint.back")},
        });
    }
    else if (mCategory == Category::Logs) {
        UiKit::drawHintBar({
            {"", i18n::t("hint.scroll")},
            {"B", i18n::t("hint.categories")},
        });
    }
    else {
        UiKit::drawHintBar({
            {"A", i18n::t("hint.change")},
            {"B", i18n::t("hint.categories")},
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

const std::string& SettingsScreen::sectionAt(int i) const
{
    static const std::string empty;
    for (; i >= 0; i--) {
        if (!mRows[i].section.empty())
            return mRows[i].section;
    }
    return empty;
}

int SettingsScreen::lastVisibleRow(int scroll) const
{
    const int viewBottom = UiKit::HINTBAR_Y;
    int y                = ROWS_Y0;
    int last             = scroll;
    for (int i = scroll; i < (int)mRows.size(); i++) {
        const int rectTop = y + (i == scroll ? labelSlotH() : inlineSectionExtra(mRows[i].section, mRows[i - 1].section));
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
        const bool canEnter = hasFocusableRow() || (mCategory == Category::Logs && !mLogLines.empty());
        if ((kdown & (HidNpadButton_Right | HidNpadButton_A)) && canEnter) {
            mCatFocused = false; // rebuildRows() already parked mCursor on the first focusable row
        }
        return;
    }

    // ---- Logs pane focused: Up/Down scroll the log lines (d-pad auto-repeat),
    // B/Left returns to the category rail. ----
    if (mCategory == Category::Logs) {
        if ((kdown & HidNpadButton_B) || (kdown & HidNpadButton_Left)) {
            mCatFocused = true;
            return;
        }
        const int maxScroll = std::max(0, (int)mLogLines.size() - logLinesPerPage());
        const u64 kheld     = input.kHeld;
        auto up             = [&]() { mLogScroll = std::max(0, mLogScroll - 1); };
        auto down           = [&]() { mLogScroll = std::min(maxScroll, mLogScroll + 1); };
        if (kdown & HidNpadButton_AnyUp) {
            up();
            mLogHeldTimer = 0;
        }
        else if (kdown & HidNpadButton_AnyDown) {
            down();
            mLogHeldTimer = 0;
        }
        else if (kheld & (HidNpadButton_AnyUp | HidNpadButton_AnyDown)) {
            // Ramp: an initial hold delay, then step every other frame.
            if (++mLogHeldTimer > 8 && (mLogHeldTimer % 2) == 0) {
                (kheld & HidNpadButton_AnyUp) ? up() : down();
            }
        }
        else {
            mLogHeldTimer = 0;
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

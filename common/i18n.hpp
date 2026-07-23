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

#ifndef I18N_HPP
#define I18N_HPP

#include <initializer_list>
#include <string>

// Runtime string localization. One flat JSON per project (romfs:/i18n.json)
// maps key -> { lang -> text }. Lookups happen live at every call so any
// string fetched per frame updates instantly on setLanguage. See HANDOFF-i18n.md.
namespace i18n {
    // Parse the romfs json once at boot. Safe to call before setLanguage. A
    // parse failure leaves the table empty and every t() falls back to the key.
    bool init(const std::string& path);

    // Only "en", "it", "es", "fr", "de", "pt", "nl", "ja" and "zh" are accepted; anything else falls back to "en".
    void setLanguage(const std::string& code);
    const std::string& language(void);

    // Lookup with fallback chain: current lang -> "en" -> the key itself.
    // A visible "settings.general.title" on screen flags a missing entry.
    std::string t(const std::string& key);

    // Placeholder substitution: replaces "{0}", "{1}", ... with args. Args are
    // pre-stringified by the caller (this does no printf). Indexed placeholders
    // let a translation reorder arguments relative to English.
    std::string t(const std::string& key, std::initializer_list<std::string> args);
}

#endif

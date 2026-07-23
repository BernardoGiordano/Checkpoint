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

#include "i18n.hpp"
#include "json.hpp"
#include <cstdio>
#include <unordered_map>

namespace {
    // key -> (lang -> text). Flattened at init so lookups never traverse json.
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sTable;
    std::string sLanguage = "en";

    bool isSupported(const std::string& code)
    {
        return code == "en" || code == "it" || code == "es" || code == "fr" || code == "de" || code == "pt" || code == "nl" || code == "ja" || code == "zh";
    }
}

bool i18n::init(const std::string& path)
{
    sTable.clear();

    FILE* in = fopen(path.c_str(), "rt");
    if (in == nullptr) {
        return false;
    }
    // Never crash on a malformed i18n.json: parse non-throwing, fall back to
    // key-echo (empty table) on any error, mirroring configuration.cpp.
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    fclose(in);

    if (!j.is_object()) {
        return false;
    }

    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_object()) {
            continue;
        }
        auto& langs = sTable[it.key()];
        for (auto lit = it.value().begin(); lit != it.value().end(); ++lit) {
            if (lit.value().is_string()) {
                langs.emplace(lit.key(), lit.value().get<std::string>());
            }
        }
    }
    return true;
}

void i18n::setLanguage(const std::string& code)
{
    sLanguage = isSupported(code) ? code : "en";
}

const std::string& i18n::language(void)
{
    return sLanguage;
}

std::string i18n::t(const std::string& key)
{
    auto entry = sTable.find(key);
    if (entry != sTable.end()) {
        auto& langs = entry->second;
        auto hit    = langs.find(sLanguage);
        if (hit != langs.end()) {
            return hit->second;
        }
        auto en = langs.find("en");
        if (en != langs.end()) {
            return en->second;
        }
    }
    // Missing entry: echo the key so it is visible on screen as a flag.
    return key;
}

std::string i18n::t(const std::string& key, std::initializer_list<std::string> args)
{
    std::string result = t(key);

    // Replace each "{i}" token with args[i]. Single left-to-right pass per index
    // so a substituted value that itself contains braces is never re-expanded.
    size_t index = 0;
    for (const auto& arg : args) {
        const std::string token = "{" + std::to_string(index) + "}";
        size_t pos              = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), arg);
            pos += arg.size();
        }
        ++index;
    }
    return result;
}

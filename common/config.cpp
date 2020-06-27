/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#include "config.hpp"
#include "io.hpp"
#include "fileptr.hpp"
#include "platform.hpp"

#include "json.hpp"

namespace {
    nlohmann::json loadJson(const char* path)
    {
        FilePtr in = openFile(path, "rt");
        if (in)
            return nlohmann::json::parse(in.get(), nullptr, false);
        else
            return {};
    }

    void storeJson(const nlohmann::json& j, const char* path)
    {
        std::string writeData = j.dump(2);

        FilePtr out = openFile(path, "wt");
        if (out) {
            fwrite(writeData.c_str(), 1, writeData.size(), out.get());
        }
    }

    nlohmann::json storeAndGetDefault()
    {
        nlohmann::json src = loadJson(Platform::Files::DefaultConfig);
        storeJson(src, Platform::Files::Config);
        return src;
    }

    nlohmann::json g_configJson;
    ConfigDataHolder g_data = ConfigDataHolder(nullptr);
}

void Configuration::load()
{
    bool isDefault = false;
    IODataHolder configPath;
    configPath.srcPath = Platform::Files::Config;
    if (!io::fileExists(configPath)) {
        g_configJson = storeAndGetDefault();
        isDefault = true;
    }
    else {
        g_configJson = loadJson(Platform::Files::Config);
    }

    if (g_configJson.find("version") == g_configJson.end()) {
        // if config is present but is < 3.4.2, override it
        g_configJson = storeAndGetDefault();
        isDefault = true;
    }
    g_data = ConfigDataHolder(&g_configJson, isDefault);
}

void Configuration::save()
{
    if(g_data.anyChange) {
        storeJson(g_configJson, Platform::Files::Config);
    }
}

ConfigDataHolder& Configuration::get()
{
    return g_data;
}

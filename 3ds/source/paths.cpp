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

#include "paths.hpp"
#include "util.hpp"
#include <cstdio>

const char* Paths::checkpointRoot(void)
{
    return "/3ds/Checkpoint";
}

const char* Paths::scriptsRoot(void)
{
    return "/3ds/Checkpoint/scripts";
}

std::string Paths::universalScriptsDir(void)
{
    return std::string(scriptsRoot()) + "/universal";
}

std::string Paths::scriptsDirFor(u64 id)
{
    char idStr[17] = {0};
    snprintf(idStr, sizeof(idStr), "%016llX", id);
    return std::string(scriptsRoot()) + "/" + idStr;
}

const char* Paths::bundledScriptsRoot(void)
{
    return "romfs:/scripts";
}

std::string Paths::bundledUniversalScriptsDir(void)
{
    return std::string(bundledScriptsRoot()) + "/universal";
}

std::string Paths::bundledScriptsDirFor(u64 id)
{
    char idStr[17] = {0};
    snprintf(idStr, sizeof(idStr), "%016llX", id);
    return std::string(bundledScriptsRoot()) + "/" + idStr;
}

std::u16string Paths::savesRoot(void)
{
    return StringUtils::UTF8toUTF16("/3ds/Checkpoint/saves/");
}

std::u16string Paths::extdataRoot(void)
{
    return StringUtils::UTF8toUTF16("/3ds/Checkpoint/extdata/");
}

std::u16string Paths::rootFor(bool extdata)
{
    return extdata ? extdataRoot() : savesRoot();
}

std::u16string Paths::ctrFolderName(u64 id, const std::u16string& sanitizedDescription)
{
    const u32 unique   = (u32)id >> 8;
    char uniqueStr[12] = {0};
    snprintf(uniqueStr, sizeof(uniqueStr), "0x%05X ", (unsigned int)unique);
    return StringUtils::UTF8toUTF16(uniqueStr) + sanitizedDescription;
}

std::u16string Paths::ctrSavePath(u64 id, const std::u16string& sanitizedDescription)
{
    return savesRoot() + ctrFolderName(id, sanitizedDescription);
}

std::u16string Paths::ctrExtdataPath(u64 id, const std::u16string& sanitizedDescription)
{
    return extdataRoot() + ctrFolderName(id, sanitizedDescription);
}

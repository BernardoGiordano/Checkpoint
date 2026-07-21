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

#include "paths.hpp"
#include "util.hpp"

const char* Paths::scriptsRoot(void)
{
    return "sdmc:/switch/Checkpoint/scripts";
}

std::string Paths::universalScriptsDir(void)
{
    return std::string(scriptsRoot()) + "/universal";
}

std::string Paths::scriptsDirFor(u64 id)
{
    return std::string(scriptsRoot()) + "/" + StringUtils::format("%016llX", (unsigned long long)id);
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
    return std::string(bundledScriptsRoot()) + "/" + StringUtils::format("%016llX", (unsigned long long)id);
}

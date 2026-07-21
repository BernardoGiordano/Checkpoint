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

#ifndef PATHS_HPP
#define PATHS_HPP

#include <string>
#include <switch.h>

// Owner of the script folder layout (the backup roots stay in SaveDataSource,
// which predates this header). Mirrors the 3DS Paths namespace so common code
// and docs can talk about one rule.
namespace Paths {
    // "sdmc:/switch/Checkpoint/scripts" — the drop-in point for PicoC scripts.
    // Its "universal" subfolder holds title-independent scripts; a subfolder
    // named after a 16-hex-uppercase title id holds that title's own.
    const char* scriptsRoot(void);
    std::string universalScriptsDir(void);
    std::string scriptsDirFor(u64 id);

    // "romfs:/scripts" — scripts bundled inside the app (e.g. sharkive.c). Same
    // layout as scriptsRoot; an SD file of the same name shadows the bundled one.
    const char* bundledScriptsRoot(void);
    std::string bundledUniversalScriptsDir(void);
    std::string bundledScriptsDirFor(u64 id);
}

#endif

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

#ifndef PATHS_HPP
#define PATHS_HPP

#include <3ds.h>
#include <string>

// Sole owner of "where a backup lives and what its folder is called".
//
// The backup roots and the CTR folder-name rule used to be re-spelled in
// TitleProbe, the wireless receiver, and the boot-time mkdir bootstrap. When
// the receiver named an unknown title's folder differently from TitleProbe, a
// received backup landed in a folder that never reconciled with the title's
// real one once installed. Everyone routes through here now so the rule can
// only exist once.
namespace Paths {
    // "/3ds/Checkpoint" — the app root under which every backup tree lives.
    const char* checkpointRoot(void);

    // "/3ds/Checkpoint/scripts" — the drop-in point for PicoC scripts. Its
    // "universal" subfolder holds title-independent scripts; a subfolder named
    // after a 16-hex-uppercase title id holds that title's own.
    const char* scriptsRoot(void);
    std::string universalScriptsDir(void);
    std::string scriptsDirFor(u64 id);

    // "romfs:/scripts" — scripts bundled inside the app (e.g. sharkive.c). Same
    // layout as scriptsRoot; an SD file of the same name shadows the bundled one.
    const char* bundledScriptsRoot(void);
    std::string bundledUniversalScriptsDir(void);
    std::string bundledScriptsDirFor(u64 id);

    // Backup roots, trailing slash included so a folder name concatenates
    // directly: savesRoot() + folder.
    std::u16string savesRoot(void);
    std::u16string extdataRoot(void);
    std::u16string rootFor(bool extdata);

    // The canonical CTR backup folder name: "0x%05X " + the (already sanitized)
    // short description, keyed on the title's unique id. TitleProbe and the
    // receiver must produce byte-identical names for the same title, so both
    // call this instead of formatting the prefix themselves.
    std::u16string ctrFolderName(u64 id, const std::u16string& sanitizedDescription);

    // Full CTR backup directory paths (root + canonical folder name).
    std::u16string ctrSavePath(u64 id, const std::u16string& sanitizedDescription);
    std::u16string ctrExtdataPath(u64 id, const std::u16string& sanitizedDescription);
}

#endif

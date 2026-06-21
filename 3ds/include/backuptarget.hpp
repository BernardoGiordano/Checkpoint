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

#ifndef BACKUPTARGET_HPP
#define BACKUPTARGET_HPP

#include "archive.hpp"
#include <string>
#include <vector>

class Title;

// One backup facet (Save or Extdata) of a Title. The single place that answers,
// for a given BackupKind: what is the root path, the list of existing backups,
// the full path of backup N, the human-readable name, and how to open its
// archive.
//
// Holds a reference to the Title it was vended from; intended to be used as a
// short-lived local that does not outlive that Title.
class BackupTarget {
public:
    BackupTarget(Title& title, BackupKind kind) : mTitle(title), mKind(kind) {}

    BackupKind kind(void) const { return mKind; }

    std::u16string rootPath(void) const;             // savePath() | extdataPath()
    std::u16string fullPath(size_t cellIndex) const; // fullSavePath() | fullExtdataPath()
    std::vector<std::u16string> backups(void) const; // saves() | extdata()
    const char* dataTypeName(void) const;            // "save" | "extdata"

    // Opens the underlying archive. `res` receives the FS result. The returned
    // handle closes itself correctly (FSUSER/FSPXI) on scope exit; on failure the
    // handle is invalid (evaluates to false). GBA VC raw saves are handled here,
    // invisibly to the caller.
    ArchiveHandle open(Result& res) const;

private:
    // The save-data source backing this facet: a raw GBA VC save when the Title
    // is GBA VC, otherwise a regular CTR save; extdata for the Extdata kind.
    SaveDataSource source(void) const;

    Title& mTitle;
    BackupKind mKind;
};

#endif // BACKUPTARGET_HPP

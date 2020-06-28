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

#include "cheatmanager.hpp"
#include "logger.hpp"
#include "stringutils.hpp"
#include "fileptr.hpp"
#include "platform.hpp"

void CheatManager::save(const std::string& key, const std::vector<std::string>& s)
{
    std::string cheatFile;
    for (const std::string_view& cellName : s) {
        if (cellName.compare(0, SELECTED_MAGIC.size(), SELECTED_MAGIC) == 0) { // compare from the beginning to the end of the magic
            const auto fixedNameView = cellName.substr(SELECTED_MAGIC.size()); // from the end of the magic to the end
            const std::string fixedName(fixedNameView.begin(), fixedNameView.end());
            cheatFile += '[';
            cheatFile += fixedName;
            cheatFile += ']';
            cheatFile += '\n';
            for (auto& it : (*mCheats)[key][fixedName]) {
                cheatFile += it.get<std::string>();
                cheatFile += '\n';
            }
            cheatFile += '\n';
        }
    }

    const std::string outPath = StringUtils::format(Platform::Formats::CheatPath, key.c_str());
    FilePtr f = openFile(outPath.c_str(), "wt");
    if (f) {
        fwrite(cheatFile.c_str(), 1, cheatFile.length(), f.get());
    }
    else {
        Logger::log(Logger::ERROR, "Failed to write " + outPath + " with errno %d.", errno);
    }
}

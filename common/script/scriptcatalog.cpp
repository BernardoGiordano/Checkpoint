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

#include "scriptcatalog.hpp"
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

namespace {
    using ScriptCatalog::Entry;
    using ScriptCatalog::Root;
    using ScriptCatalog::Source;

    // Merges one section (universal or a title's specific scripts) across its
    // roots. Roots come base-first (bundled, then SD): a later root's file with
    // the same name replaces the earlier one and is flagged `overridden`. The
    // merged section is appended to `out`, sorted alphabetically.
    void scanSection(const std::vector<Root>& roots, bool universal, std::vector<Entry>& out)
    {
        const size_t first = out.size();
        for (const Root& root : roots) {
            DIR* d = opendir(root.dir.c_str());
            if (!d) {
                continue;
            }
            while (struct dirent* ent = readdir(d)) {
                const std::string name = ent->d_name;
                if (name.size() <= 2 || name.compare(name.size() - 2, 2, ".c") != 0) {
                    continue;
                }
                // Only files directly in the directory count; subfolders are assets.
                const std::string path = root.dir + "/" + name;
                struct stat st;
                if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                    continue;
                }
                const std::string display = name.substr(0, name.size() - 2);
                auto it                   = std::find_if(out.begin() + first, out.end(), [&](const Entry& e) { return e.name == display; });
                if (it != out.end()) {
                    // A later root shadows an earlier one: the SD file wins and
                    // is marked as an override of the bundled script.
                    it->path       = path;
                    it->source     = root.source;
                    it->overridden = true;
                }
                else {
                    out.push_back({display, path, universal, root.source, false});
                }
            }
            closedir(d);
        }

        std::sort(out.begin() + first, out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    }
}

std::vector<ScriptCatalog::Entry> ScriptCatalog::scan(const std::vector<Root>& universalRoots, const std::vector<Root>& specificRoots)
{
    std::vector<Entry> entries;
    scanSection(universalRoots, true, entries);
    if (!specificRoots.empty()) {
        scanSection(specificRoots, false, entries);
    }
    return entries;
}

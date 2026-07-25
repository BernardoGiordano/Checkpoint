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

#include "scriptbar.hpp"
#include <algorithm>
#include <cstdio>

namespace {
    std::string scaled(double n, const char* unit)
    {
        char buf[32];
        // One decimal below 10 of a unit, none above: "1.4 MB" is worth the
        // digit, "947.3 MB" is noise in a figure that moves every frame.
        if (n < 10.0) {
            snprintf(buf, sizeof buf, "%.1f %s", n, unit);
        }
        else {
            snprintf(buf, sizeof buf, "%d %s", (int)(n + 0.5), unit);
        }
        return buf;
    }
}

namespace ScriptBar {
    std::string bytes(long long n)
    {
        if (n < 0) {
            n = 0;
        }
        if (n < 1024) {
            return std::to_string(n) + " B";
        }
        if (n < 1024 * 1024) {
            return scaled((double)n / 1024.0, "KB");
        }
        if (n < 1024LL * 1024 * 1024) {
            return scaled((double)n / (1024.0 * 1024.0), "MB");
        }
        return scaled((double)n / (1024.0 * 1024.0 * 1024.0), "GB");
    }

    float fraction(const ScriptConsole::Layer& layer)
    {
        if (layer.total <= 0) {
            return 0.0f;
        }
        return std::clamp((float)layer.done / (float)layer.total, 0.0f, 1.0f);
    }

    std::string rightText(const ScriptConsole::Layer& layer)
    {
        if (layer.bytes) {
            std::string out;
            if (layer.total > 0) {
                out = std::to_string((int)(fraction(layer) * 100.0f)) + "%";
            }
            else if (layer.done > 0) {
                // No known total (a stream): the amount moved so far is the
                // only real figure.
                out = bytes(layer.done);
            }
            if (layer.rate > 0.0) {
                if (!out.empty()) {
                    out += "  ";
                }
                out += bytes((long long)layer.rate) + "/s";
            }
            return out;
        }

        // An item bar: the count the script is keeping, verbatim. Shown for an
        // idle bar too — it finished at that count, and blanking it would make
        // a finished row indistinguishable from one that never ran.
        if (layer.total > 0) {
            return std::to_string(layer.done) + "/" + std::to_string(layer.total);
        }
        if (layer.done > 0 || layer.active) {
            return std::to_string(layer.done);
        }
        return std::string();
    }
}

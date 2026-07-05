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

// Backend-agnostic drawing helpers: everything here goes through the Gfx::*
// API only (see HANDOFF-deko3d.md).

#include "DekoHelper.hpp"
#include "main.hpp"
#include <cmath>
#include <unordered_map>

// Byte length of the UTF-8 sequence starting at `character`, 0 if invalid.
// (Same semantics as the retired SDL_FontCache's U8_charsize.)
static int utf8CharSize(const char* character)
{
    if (character == NULL || character[0] == '\0') {
        return 0;
    }
    if ((character[0] & 0x80) == 0) {
        return 1;
    }
    if ((character[0] & 0xE0) == 0xC0) {
        return 2;
    }
    if ((character[0] & 0xF0) == 0xE0) {
        return 3;
    }
    if ((character[0] & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, Color color)
{
    Gfx::DrawRect(x - size, y - size, w + 2 * size, size, color); // top
    Gfx::DrawRect(x - size, y, size, h, color);                   // left
    Gfx::DrawRect(x + w, y, size, h, color);                      // right
    Gfx::DrawRect(x - size, y + h, w + 2 * size, size, color);    // bottom
}

void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, Color color)
{
    float highlight_multiplier = fmax(0.0, fabs(fmod(g_currentTime, 1.0) - 0.5) / 0.5);
    color                      = makeColor(color.r + (255 - color.r) * highlight_multiplier, color.g + (255 - color.g) * highlight_multiplier,
                             color.b + (255 - color.b) * highlight_multiplier, 255);
    drawOutline(x, y, w, h, size, color);
}

std::string trimToFit(const std::string& text, u32 maxsize, size_t textsize, FontFamily family)
{
    // Each call is O(n) text measurements, and this runs every frame on the
    // top-bar title, the panel subtitles and overlay rows. Memoize by (size,
    // width, family, text): the inputs are a small, stable set (title/backup
    // names), and all drawing is on the one UI thread, so no lock is needed.
    static std::unordered_map<std::string, std::string> cache;
    std::string key = std::to_string(textsize) + "|" + std::to_string(maxsize) + "|" + std::to_string((int)family) + "|" + text;
    auto cached     = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    if (cache.size() > 512) {
        cache.clear(); // defensive bound; the distinct-string set is normally tiny
    }

    u32 width;
    std::string newtext = "";
    const char* src     = text.c_str();
    while (*src != '\0') {
        int charsize = utf8CharSize(src);
        if (charsize < 1)
            break;
        std::string candidate = newtext + std::string(src, charsize);
        Gfx::GetTextDimensions(textsize, candidate.c_str(), &width, NULL, family);
        if (width >= maxsize) {
            newtext += "...";
            break;
        }
        newtext = candidate;
        src += charsize;
    }
    cache.emplace(std::move(key), newtext);
    return newtext;
}

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
 *         or requiring that modified versions of such material are marked in
 *         reasonable ways as different from the original version.
 */

#include "textpool.hpp"
#include "util.hpp"
#include <vector>

namespace {
    // Glyph capacity of the persistent buffer. When a parse would not fit, the
    // whole cache is rebuilt (C2D_TextBuf cannot free individual entries).
    constexpr size_t GLYPH_CAPACITY = 8192;
    // frameTick rebuilds when distinct cached strings exceed this, so scrolling
    // through long backup/title lists cannot grow the map without bound.
    constexpr size_t MAX_ENTRIES = 512;
    // How often frameTick checks the entry bound.
    constexpr u32 TICK_INTERVAL = 300;

    // Byte length of the UTF-8 sequence starting with lead byte `b`.
    size_t utf8SeqLen(unsigned char b)
    {
        if ((b & 0x80) == 0x00) {
            return 1;
        }
        if ((b & 0xE0) == 0xC0) {
            return 2;
        }
        if ((b & 0xF0) == 0xE0) {
            return 3;
        }
        return 4;
    }
}

TextPool& TextPool::get(void)
{
    static TextPool instance;
    return instance;
}

TextPool::TextPool(void)
{
    mBuf = C2D_TextBufNew(GLYPH_CAPACITY);
}

TextPool::~TextPool(void)
{
    C2D_TextBufDelete(mBuf);
}

void TextPool::rebuild(void)
{
    C2D_TextBufClear(mBuf); // invalidates every cached C2D_Text with it
    mCache.clear();
}

void TextPool::frameTick(void)
{
    if (++mFrame % TICK_INTERVAL == 0 && mCache.size() > MAX_ENTRIES) {
        rebuild();
    }
}

const C2D_Text* TextPool::obtain(const std::string& s)
{
    auto it = mCache.find(s);
    if (it != mCache.end()) {
        return &it->second;
    }
    // s.size() over-estimates the glyph count for multi-byte UTF-8, which only
    // makes the rebuild trigger early — never a truncated parse.
    if (C2D_TextBufGetNumGlyphs(mBuf) + s.size() > GLYPH_CAPACITY) {
        rebuild();
    }
    C2D_Text t;
    C2D_TextParse(&t, mBuf, s.c_str());
    C2D_TextOptimize(&t);
    return &mCache.emplace(s, t).first->second;
}

float TextPool::draw(const std::string& s, float x, float y, float scale, u32 color, float depth)
{
    const C2D_Text* t = obtain(s);
    C2D_DrawText(t, C2D_WithColor, x, y, depth, scale, scale, color);
    return StringUtils::textWidth(*t, scale);
}

void TextPool::drawCentered(const std::string& s, float x, float w, float y, float scale, u32 color, float depth)
{
    const C2D_Text* t = obtain(s);
    C2D_DrawText(t, C2D_WithColor, x + ceilf((w - StringUtils::textWidth(*t, scale)) / 2), y, depth, scale, scale, color);
}

void TextPool::drawWrapped(const std::string& s, float x, float y, float scale, u32 color, float maxWidth, float depth, u32 alignFlags)
{
    const C2D_Text* t = obtain(s);
    C2D_DrawText(t, C2D_WithColor | C2D_WordWrap | alignFlags, x, y, depth, scale, scale, color, maxWidth);
}

float TextPool::width(const std::string& s, float scale) const
{
    return StringUtils::textWidth(s, scale);
}

std::string TextPool::truncate(const std::string& s, float maxWidth, float scale) const
{
    if (StringUtils::textWidth(s, scale) <= maxWidth) {
        return s;
    }
    const float ellipsisWidth = StringUtils::textWidth("...", scale);

    // Codepoint start offsets, so the cut never splits a UTF-8 sequence.
    std::vector<size_t> starts;
    for (size_t i = 0; i < s.size(); i += utf8SeqLen((unsigned char)s[i])) {
        starts.push_back(i);
    }

    // Largest codepoint count whose prefix still fits alongside the ellipsis.
    size_t lo = 0, hi = starts.size();
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        const size_t end = mid < starts.size() ? starts[mid] : s.size();
        if (StringUtils::textWidth(s.substr(0, end), scale) + ellipsisWidth <= maxWidth) {
            lo = mid;
        }
        else {
            hi = mid - 1;
        }
    }
    const size_t end = lo < starts.size() ? starts[lo] : s.size();
    return s.substr(0, end) + "...";
}

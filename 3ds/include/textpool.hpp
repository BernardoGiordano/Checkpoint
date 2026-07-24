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

#ifndef TEXTPOOL_HPP
#define TEXTPOOL_HPP

#include <citro2d.h>
#include <string>
#include <unordered_map>

// The single owner of C2D text glyph storage for the whole UI. Screens and
// overlays draw strings through it and never see a C2D_TextBuf; parsed
// C2D_Texts are cached by string in one persistent buffer, so a label that
// does not change is parsed once, not once per frame.
//
// Measurement (width / truncate) goes through the glyph-width cache in
// StringUtils::textWidth and never parses at all.
//
// UI-thread only, like every other draw call. Call frameTick() once per
// frame from the main loop; it bounds the cache between frames.
class TextPool {
public:
    static TextPool& get(void);

    // Housekeeping between frames: rebuilds the cache when it has grown past
    // its bounds (a rebuild just clears; entries re-parse lazily on next use).
    void frameTick(void);

    // Draws `s` at (x, y) and returns its advance width. `depth` is the C2D z
    // the caller's layer draws at (screens use 0.5, modal overlays 0.6+).
    float draw(const std::string& s, float x, float y, float scale, u32 color, float depth = 0.5f);

    // Draws `s` horizontally centered inside [x, x + w). Text wider than `w`
    // is ellipsis-truncated so it never escapes the box it centers in.
    void drawCentered(const std::string& s, float x, float w, float y, float scale, u32 color, float depth = 0.5f);

    // Draws a word-wrapped paragraph limited to maxWidth. `alignFlags` may add
    // C2D alignment flags (e.g. C2D_AlignCenter, which makes `x` the center).
    void drawWrapped(const std::string& s, float x, float y, float scale, u32 color, float maxWidth, float depth = 0.5f, u32 alignFlags = 0);

    // Measures without parsing (multi-line strings measure their widest line).
    float width(const std::string& s, float scale) const;

    // Largest scale <= `scale` at which `s` fits maxWidth, floored at
    // minScale. Width is linear in scale so this is a single measure, not a
    // search. If even minScale overflows, the caller truncates at minScale.
    float fitScale(const std::string& s, float maxWidth, float scale, float minScale) const;

    // Shortens `s` with a trailing ellipsis until it fits maxWidth. Respects
    // UTF-8 codepoint boundaries and never parses.
    std::string truncate(const std::string& s, float maxWidth, float scale) const;

    // ---- monospace ---------------------------------------------------------
    // The script log pane's font: a bundled Space Mono built into a .bcfnt by
    // the root Makefile. The shared system font has no monospace face, and the
    // pane needs a fixed advance so one stored log line is exactly one row.
    // Loaded lazily on first use (romfs is mounted well before then).

    // In all three calls below `scale` is relative to the atlas: 1.0 draws the
    // glyphs at the point size the .bcfnt was rasterised at, one texel per
    // pixel, which is the only scale that stays crisp.

    // False when the font could not be loaded; callers fall back to draw().
    bool monoReady(void);

    // Width of one character at `scale`. Fixed, so a full line is `n` times it.
    float monoAdvance(float scale);

    // Height of one line at `scale`, for stacking log rows.
    float monoLineHeight(float scale);

    // Draws `s` in the monospace font. Same pen-snapping as draw().
    void drawMono(const std::string& s, float x, float y, float scale, u32 color, float depth = 0.5f);

private:
    TextPool(void);
    ~TextPool(void);
    TextPool(const TextPool&)            = delete;
    TextPool& operator=(const TextPool&) = delete;

    // Returns the cached parse of `s`, parsing (and, if the buffer is nearly
    // full, rebuilding) as needed. The pointer is valid until the next rebuild.
    // `font` selects the face; nullptr is the system font.
    const C2D_Text* obtain(const std::string& s, C2D_Font font);
    void rebuild(void);

    C2D_TextBuf mBuf;
    // One cache per face: the same string parsed in two fonts is two different
    // glyph runs, so they cannot share a key.
    std::unordered_map<std::string, C2D_Text> mCache, mMonoCache;
    C2D_Font mMono     = nullptr;
    bool mMonoTried    = false;
    float mMonoAdvance = 0.0f; // both measured once, in atlas pixels
    float mMonoHeight  = 0.0f;
    // citro2d normalises every font to the system font's line height, so its
    // scale 1.0 blows a small atlas up to ~30px rows. This factor divides that
    // back out: the mono* calls take scale 1.0 to mean one atlas pixel.
    float mMonoScale = 1.0f;
    u32 mFrame       = 0;
};

#endif

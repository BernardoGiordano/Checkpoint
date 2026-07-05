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

#ifndef GFX_HPP
#define GFX_HPP

#include "colors.hpp"
#include "gfxtypes.hpp"
#include <switch.h>

// Sans is the shared system font (matches the mock's "system font
// stack"). Mono is a bundled Space Mono (OFL) used for backup names, title
// IDs, and paths — the shared font has no monospace variant.
enum class FontFamily { Sans, Mono };

namespace Gfx {
    bool Init(void);
    void Exit(void);

    // Seconds since app start; anchors the pulsing-outline animation (see
    // Gfx::Render). Not wall-clock time.
    float animationTime(void);

    void ClearScreen(Color color);
    void DrawRect(int x, int y, int w, int h, Color color);
    void DrawText(int size, int x, int y, Color color, const char* text, FontFamily family = FontFamily::Sans);
    void LoadImage(Texture** texture, const char* path);
    void LoadImage(Texture** texture, u8* buff, size_t size);
    void DrawImageScale(Texture* texture, int x, int y, int w, int h);
    // Disable alpha blending for `texture` (used for NS title icons, which are
    // opaque JPEGs; Gfx::LoadImage's black colorkey must not punch holes in them).
    void SetTextureOpaque(Texture* texture);
    void DestroyTexture(Texture* texture);
    void GetTextDimensions(int size, const char* text, u32* w, u32* h, FontFamily family = FontFamily::Sans);
    void DrawTextBox(int size, int x, int y, Color color, int max, const char* text, FontFamily family = FontFamily::Sans);
    void Render(void);
    void CreateColorTexture(Texture** texture, int w, int h, Color color);

    // GPU-antialiased rounded rectangles (signed-distance-field). radius is
    // clamped to min(w,h)/2 by the caller conventions in Shapes. FillRoundRect
    // fills the shape; StrokeRoundRect draws a hollow ring of `thickness` inset
    // from the outer edge (inner corner radius = radius - thickness).
    void FillRoundRect(int x, int y, int w, int h, float radius, Color color);
    void StrokeRoundRect(int x, int y, int w, int h, float radius, float thickness, Color color);
} // namespace Gfx

#endif
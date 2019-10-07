/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#include "gui.hpp"

C2D_Image Gui::noIcon(void)
{
    return C2D_SpriteSheetGetImage(spritesheet, sprites_noicon_idx);
}

void Gui::init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    g_top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    spritesheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    flag        = C2D_SpriteSheetGetImage(spritesheet, sprites_checkpoint_idx);
    C2D_SpriteFromSheet(&checkbox, spritesheet, sprites_checkbox_idx);
    C2D_SpriteSetDepth(&checkbox, 0.5f);
    C2D_SpriteFromSheet(&star, spritesheet, sprites_star_idx);
    C2D_SpriteSetDepth(&star, 0.5f);
}

void Gui::exit(void)
{
    C2D_SpriteSheetFree(spritesheet);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

void Gui::frameEnd(void)
{
    C3D_FrameEnd(0);
    g_timer += 0.025f;
}

void Gui::drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color)
{
    u8 r                       = color & 0xFF;
    u8 g                       = (color >> 8) & 0xFF;
    u8 b                       = (color >> 16) & 0xFF;
    float highlight_multiplier = fmax(0.0, fabs(fmod(g_timer, 1.0) - 0.5) / 0.5);
    color = C2D_Color32(r + (255 - r) * highlight_multiplier, g + (255 - g) * highlight_multiplier, b + (255 - b) * highlight_multiplier, 255);
    drawOutline(x, y, w, h, size, color);
}

void Gui::drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color)
{
    C2D_DrawRectSolid(x - size, y - size, 0.5f, w + 2 * size, size, color); // top
    C2D_DrawRectSolid(x - size, y, 0.5f, size, h, color);                   // left
    C2D_DrawRectSolid(x + w, y, 0.5f, size, h, color);                      // right
    C2D_DrawRectSolid(x - size, y + h, 0.5f, w + 2 * size, size, color);    // bottom
}
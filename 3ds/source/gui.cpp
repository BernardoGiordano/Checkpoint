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

C2D_Image Gui::TWLIcon(void)
{
    return C2D_SpriteSheetGetImage(spritesheet, sprites_twlcart_idx);
}

C2D_Image Gui::noIcon(void)
{
    return C2D_SpriteSheetGetImage(spritesheet, sprites_noicon_idx);
}

bool Gui::askForConfirmation(const std::string& message)
{
    bool ret             = false;
    Clickable* buttonYes = new Clickable(42, 162, 116, 36, COLOR_GREY_DARK, COLOR_WHITE, "\uE000 Yes", true);
    Clickable* buttonNo  = new Clickable(162, 162, 116, 36, COLOR_GREY_DARK, COLOR_WHITE, "\uE001 No", true);
    HidHorizontal* mhid  = new HidHorizontal(2, 2);
    C2D_Text text;
    C2D_TextParse(&text, g_dynamicBuf, message.c_str());
    C2D_TextOptimize(&text);

    while (aptMainLoop()) {
        hidScanInput();
        mhid->update(2);

        if (buttonYes->released() || ((hidKeysDown() & KEY_A) && mhid->index() == 0)) {
            ret = true;
            break;
        }
        else if (buttonNo->released() || (hidKeysDown() & KEY_B) || ((hidKeysDown() & KEY_A) && mhid->index() == 1)) {
            break;
        }

        mhid->index(buttonYes->held() ? 0 : buttonNo->held() ? 1 : mhid->index());
        buttonYes->selected(mhid->index() == 0);
        buttonNo->selected(mhid->index() == 1);

        C2D_TextBufClear(g_dynamicBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_SceneBegin(g_bottom);
        C2D_DrawRectSolid(40, 40, 0.5f, 240, 160, COLOR_GREY_DARK);
        C2D_DrawText(&text, C2D_WithColor, ceilf(320 - text.width * 0.6) / 2, 40 + ceilf(120 - 0.6f * fontGetInfo(NULL)->lineFeed) / 2, 0.5f, 0.6f,
            0.6f, COLOR_WHITE);
        C2D_DrawRectSolid(40, 160, 0.5f, 240, 40, COLOR_GREY_LIGHT);

        buttonYes->draw(0.7, 0);
        buttonNo->draw(0.7, 0);

        if (mhid->index() == 0) {
            drawPulsingOutline(42, 162, 116, 36, 2, COLOR_BLUE);
        }
        else {
            drawPulsingOutline(162, 162, 116, 36, 2, COLOR_BLUE);
        }

        frameEnd();
    }

    delete mhid;
    delete buttonYes;
    delete buttonNo;
    return ret;
}

void Gui::showInfo(const std::string& message)
{
    const float size  = 0.6f;
    Clickable* button = new Clickable(42, 162, 236, 36, COLOR_GREY_DARK, COLOR_WHITE, "OK", true);
    button->selected(true);
    std::string t = StringUtils::wrap(message, size, 220);
    C2D_Text text;
    C2D_TextParse(&text, g_dynamicBuf, t.c_str());
    C2D_TextOptimize(&text);
    u32 w = StringUtils::textWidth(text, size);
    u32 h = StringUtils::textHeight(t, size);

    while (aptMainLoop()) {
        hidScanInput();

        if (button->released() || (hidKeysDown() & KEY_A) || (hidKeysDown() & KEY_B)) {
            break;
        }

        C2D_TextBufClear(g_dynamicBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_SceneBegin(g_bottom);
        C2D_DrawRectSolid(40, 40, 0.5f, 240, 160, COLOR_GREY_DARK);
        C2D_DrawText(&text, C2D_WithColor, ceilf(320 - w) / 2, 40 + ceilf(120 - h) / 2, 0.5f, size, size, COLOR_WHITE);
        button->draw(0.7f, COLOR_BLUE);
        drawPulsingOutline(42, 162, 236, 36, 2, COLOR_BLUE);
        frameEnd();
    }

    delete button;
}

void Gui::showError(Result res, const std::string& message)
{
    const float size  = 0.6f;
    Clickable* button = new Clickable(42, 162, 236, 36, COLOR_GREY_DARKER, COLOR_WHITE, "OK", true);
    button->selected(true);
    std::string t = StringUtils::wrap(message, size, 220);
    std::string e = StringUtils::format("Error: 0x%08lX", res);
    C2D_Text text, error;
    C2D_TextParse(&text, g_dynamicBuf, t.c_str());
    C2D_TextParse(&error, g_dynamicBuf, e.c_str());
    C2D_TextOptimize(&text);
    C2D_TextOptimize(&error);
    u32 w = StringUtils::textWidth(text, size);
    u32 h = StringUtils::textHeight(t, size);

    while (aptMainLoop()) {
        hidScanInput();

        if (button->released() || (hidKeysDown() & KEY_A) || (hidKeysDown() & KEY_B)) {
            break;
        }

        C2D_TextBufClear(g_dynamicBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_SceneBegin(g_bottom);
        C2D_DrawRectSolid(40, 40, 0.5f, 240, 160, COLOR_GREY_DARK);
        C2D_DrawText(&error, C2D_WithColor, 44, 44, 0.5f, 0.5f, 0.5f, COLOR_RED);
        C2D_DrawText(&text, C2D_WithColor, ceilf(320 - w) / 2, 40 + ceilf(120 - h) / 2, 0.5f, size, size, COLOR_WHITE);
        button->draw(0.7f, COLOR_RED);
        drawPulsingOutline(42, 162, 236, 36, 2, COLOR_RED);
        frameEnd();
    }

    delete button;
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

    g_staticBuf  = C2D_TextBufNew(256);
    g_dynamicBuf = C2D_TextBufNew(256);
}

void Gui::exit(void)
{
    C2D_TextBufDelete(g_dynamicBuf);
    C2D_TextBufDelete(g_staticBuf);
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
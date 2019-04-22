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

// c2d/c3d vars
C3D_RenderTarget* g_top;
C3D_RenderTarget* g_bottom;

static C2D_SpriteSheet spritesheet;
static C2D_Image flag;
static C2D_Sprite checkbox, star;
static C2D_ImageTint checkboxTint;

C2D_TextBuf g_staticBuf, g_dynamicBuf;
static C2D_Text ins1, ins2, ins3, ins4, c2dId, c2dMediatype;
static C2D_Text checkpoint, version;
// instructions text
static C2D_Text top_move, top_a, top_y, top_my, top_b, bot_ts, bot_b, bot_r, bot_t;

// gui elements
static Clickable* buttonBackup;
static Clickable* buttonRestore;
static Clickable* buttonCheats;
static Scrollable* directoryList;
bool g_bottomScrollEnabled;

static const size_t rowlen = 4;
static const size_t collen = 8;
static HidHorizontal* hid;

static void drawSelector(void);
static int selectorX(size_t i);
static int selectorY(size_t i);
float g_timer = 0;

C2D_Image Gui::TWLIcon(void)
{
    return C2D_SpriteSheetGetImage(spritesheet, sprites_twlcart_idx);
}

C2D_Image Gui::noIcon(void)
{
    return C2D_SpriteSheetGetImage(spritesheet, sprites_noicon_idx);
}

std::string Gui::nameFromCell(size_t index)
{
    return directoryList->cellName(index);
}

void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color)
{
    u8 r = color & 0xFF;
    u8 g = (color >> 8) & 0xFF;
    u8 b = (color >> 16) & 0xFF;
    float highlight_multiplier = fmax(0.0, fabs(fmod(g_timer, 1.0) - 0.5) / 0.5);
    color = C2D_Color32(r + (255 - r) * highlight_multiplier, g + (255 - g) * highlight_multiplier, b + (255 - b) * highlight_multiplier, 255);
    C2D_DrawRectSolid(x - size, y - size, 0.5f, w + 2*size, size, color); // g_top
    C2D_DrawRectSolid(x - size, y, 0.5f, size, h, color); // left
    C2D_DrawRectSolid(x + w, y, 0.5f, size, h, color); // right
    C2D_DrawRectSolid(x - size, y + h, 0.5f, w + 2*size, size, color); // g_bottom
}

void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color)
{
    C2D_DrawRectSolid(x - size, y - size, 0.5f, w + 2*size, size, color); // g_top
    C2D_DrawRectSolid(x - size, y, 0.5f, size, h, color); // left
    C2D_DrawRectSolid(x + w, y, 0.5f, size, h, color); // right
    C2D_DrawRectSolid(x - size, y + h, 0.5f, w + 2*size, size, color); // g_bottom
}

void Gui::drawCopy(const std::u16string& src, u32 offset, u32 size)
{
    C2D_Text copyText, srcText;
    std::string sizeString = StringUtils::sizeString(offset) + " of " + StringUtils::sizeString(size);
    C2D_TextParse(&srcText, g_dynamicBuf, StringUtils::UTF16toUTF8(src).c_str());
    C2D_TextParse(&copyText, g_dynamicBuf, sizeString.c_str());
    C2D_TextOptimize(&srcText);
    C2D_TextOptimize(&copyText);
    const float scale = 0.6f;
    const u32 size_h = scale * fontGetInfo()->lineFeed;
    const u32 src_w = StringUtils::textWidth(srcText, scale);
    const u32 size_w = StringUtils::textWidth(copyText, scale);

    C2D_TextBufClear(g_dynamicBuf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_SceneBegin(g_bottom);
    C2D_DrawRectSolid(40, 40, 0.5f, 240, 160, COLOR_GREY_DARK);
    C2D_DrawRectSolid(40, 160, 0.5f, (float)offset / (float)size * 240, 40, COLOR_BLUE);
    C2D_DrawText(&srcText, C2D_WithColor, 40 + (240 - src_w) / 2, 40 + ceilf((120 - size_h) / 2), 0.5f, scale, scale, COLOR_WHITE);
    C2D_DrawText(&copyText, C2D_WithColor, 40 + (240 - size_w) / 2, 160 + ceilf((40 - size_h) / 2), 0.5f, scale, scale, COLOR_WHITE);
    frameEnd();
}

bool Gui::askForConfirmation(const std::string& message)
{
    bool ret = false;
    Clickable* buttonYes = new Clickable(42, 162, 116, 36, COLOR_GREY_DARK, COLOR_WHITE, "\uE000 Yes", true);
    Clickable* buttonNo = new Clickable(162, 162, 116, 36, COLOR_GREY_DARK, COLOR_WHITE, "\uE001 No", true);
    HidHorizontal* hid = new HidHorizontal(2, 2);
    C2D_Text text;
    C2D_TextParse(&text, g_dynamicBuf, message.c_str());
    C2D_TextOptimize(&text);

    while(aptMainLoop())
    {
        hidScanInput();
        hid->update(2);

        if (buttonYes->released() ||
            ((hidKeysDown() & KEY_A) && hid->index() == 0))
        {
            ret = true;
            break;
        }
        else if (buttonNo->released() ||
            (hidKeysDown() & KEY_B) ||
            ((hidKeysDown() & KEY_A) && hid->index() == 1))
        {
            break;
        }

        hid->index(buttonYes->held() ? 0 : buttonNo->held() ? 1 : hid->index());
        buttonYes->selected(hid->index() == 0);
        buttonNo->selected(hid->index() == 1);

        C2D_TextBufClear(g_dynamicBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_SceneBegin(g_bottom);
        C2D_DrawRectSolid(40, 40, 0.5f, 240, 160, COLOR_GREY_DARK);
        C2D_DrawText(&text, C2D_WithColor, ceilf(320 - text.width * 0.6) / 2, 40 + ceilf(120 - 0.6f * fontGetInfo()->lineFeed) / 2, 0.5f, 0.6f, 0.6f, COLOR_WHITE);
        C2D_DrawRectSolid(40, 160, 0.5f, 240, 40, COLOR_GREY_LIGHT);
        
        buttonYes->draw(0.7, 0);
        buttonNo->draw(0.7, 0);

        if (hid->index() == 0)
        {
            drawPulsingOutline(42, 162, 116, 36, 2, COLOR_BLUE);
        }
        else
        {
            drawPulsingOutline(162, 162, 116, 36, 2, COLOR_BLUE);
        }

        frameEnd();
    }

    delete hid;
    delete buttonYes;
    delete buttonNo;
    return ret;
}

void Gui::showInfo(const std::string& message)
{
    const float size = 0.6f;
    Clickable* button = new Clickable(42, 162, 236, 36, COLOR_GREY_DARK, COLOR_WHITE, "OK", true);
    button->selected(true);
    std::string t = StringUtils::wrap(message, size, 220);
    C2D_Text text;
    C2D_TextParse(&text, g_dynamicBuf, t.c_str());
    C2D_TextOptimize(&text);
    u32 w = StringUtils::textWidth(text, size);
    u32 h = StringUtils::textHeight(t, size);

    while(aptMainLoop())
    {
        hidScanInput();

        if (button->released() ||
            (hidKeysDown() & KEY_A) ||
            (hidKeysDown() & KEY_B))
        {
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
    const float size = 0.6f;
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

    while(aptMainLoop())
    {
        hidScanInput();

        if (button->released() ||
            (hidKeysDown() & KEY_A) ||
            (hidKeysDown() & KEY_B))
        {
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

void Gui::resetScrollableIndex(void)
{
    directoryList->resetIndex();
}

size_t Gui::scrollableIndex(void)
{
    return directoryList->index();
}

void Gui::scrollableIndex(size_t idx)
{
    directoryList->setIndex(idx);
}

void Gui::init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    g_bottomScrollEnabled = false;
    hid = new HidHorizontal(rowlen * collen, collen);

    buttonBackup = new Clickable(204, 102, 110, 35, COLOR_GREY_DARKER, COLOR_WHITE, "Backup \uE004", true);
    buttonRestore = new Clickable(204, 139, 110, 35, COLOR_GREY_DARKER, COLOR_WHITE, "Restore \uE005", true);
    buttonCheats = new Clickable(204, 176, 110, 36, COLOR_GREY_DARKER, COLOR_WHITE, "Cheats", true);
    directoryList = new Scrollable(6, 102, 196, 110, 5);
    buttonBackup->canInvertColor(true);
    buttonRestore->canInvertColor(true);
    buttonCheats->canInvertColor(true);

    spritesheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    flag = C2D_SpriteSheetGetImage(spritesheet, sprites_checkpoint_idx);
    C2D_SpriteFromSheet(&checkbox, spritesheet, sprites_checkbox_idx);
    C2D_SpriteSetDepth(&checkbox, 0.5f);
    C2D_PlainImageTint(&checkboxTint, C2D_Color32(88, 88, 88, 255), 1.0f);
    C2D_SpriteFromSheet(&star, spritesheet, sprites_star_idx);
    C2D_SpriteSetDepth(&star, 0.5f);

    char ver[10];
    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

    g_staticBuf = C2D_TextBufNew(256);
    g_dynamicBuf = C2D_TextBufNew(256);
    C2D_TextParse(&ins1, g_staticBuf, "Hold SELECT to see commands. Press \uE002 for ");
    C2D_TextParse(&ins2, g_staticBuf, "extdata");
    C2D_TextParse(&ins3, g_staticBuf, ".");
    C2D_TextParse(&ins4, g_staticBuf, "Press \uE073 or START to exit.");
    C2D_TextParse(&version, g_staticBuf, ver);
    C2D_TextParse(&checkpoint, g_staticBuf, "checkpoint");
    C2D_TextParse(&c2dId, g_staticBuf, "ID:");
    C2D_TextParse(&c2dMediatype, g_staticBuf, "Mediatype:");

    C2D_TextParse(&top_move, g_staticBuf, "\uE006 to move between titles");
    C2D_TextParse(&top_a, g_staticBuf, "\uE000 to enter target");
    C2D_TextParse(&top_y, g_staticBuf, "\uE003 to multiselect a title");
    C2D_TextParse(&top_my, g_staticBuf, "\uE003 hold to multiselect all titles");
    C2D_TextParse(&top_b, g_staticBuf, "\uE001 to exit target or deselect all titles");
    C2D_TextParse(&bot_ts, g_staticBuf, "\uE01D \uE006 to move\nbetween backups");
    C2D_TextParse(&bot_b, g_staticBuf, "\uE01D \uE004");
    C2D_TextParse(&bot_r, g_staticBuf, "\uE01D \uE005");
    C2D_TextParse(&bot_t, g_staticBuf, "\uE01D");

    C2D_TextOptimize(&ins1);
    C2D_TextOptimize(&ins2);
    C2D_TextOptimize(&ins3);
    C2D_TextOptimize(&ins4);
    C2D_TextOptimize(&version);
    C2D_TextOptimize(&checkpoint);
    C2D_TextOptimize(&c2dId);
    C2D_TextOptimize(&c2dMediatype);

    C2D_TextOptimize(&top_move);
    C2D_TextOptimize(&top_a);
    C2D_TextOptimize(&top_y);
    C2D_TextOptimize(&top_my);
    C2D_TextOptimize(&top_b);
    C2D_TextOptimize(&bot_ts);
    C2D_TextOptimize(&bot_b);
    C2D_TextOptimize(&bot_r);
    C2D_TextOptimize(&bot_t);
}

void Gui::exit(void)
{
    C2D_TextBufDelete(g_dynamicBuf);
    C2D_TextBufDelete(g_staticBuf);
    C2D_SpriteSheetFree(spritesheet);
    delete hid;
    delete directoryList;
    delete buttonCheats;
    delete buttonRestore;
    delete buttonBackup;
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

size_t Gui::index(void)
{
    return hid->fullIndex();
}

void Gui::updateButtons(void)
{
    if (MS::multipleSelectionEnabled())
    {
        buttonRestore->canInvertColor(true);
        buttonRestore->canInvertColor(false);
        buttonCheats->canInvertColor(false);
        buttonBackup->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonRestore->setColors(COLOR_GREY_DARKER, COLOR_GREY_LIGHT);
        buttonCheats->setColors(COLOR_GREY_DARKER, COLOR_GREY_LIGHT);
    }
    else if (g_bottomScrollEnabled)
    {
        buttonBackup->canInvertColor(true);
        buttonRestore->canInvertColor(true);
        buttonCheats->canInvertColor(true);
        buttonBackup->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonRestore->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonCheats->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
    }
    else
    {
        buttonBackup->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonRestore->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
        buttonCheats->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
    }
}

void Gui::updateSelector(void)
{
    if (!g_bottomScrollEnabled)
    {
        if (getTitleCount() > 0)
        {
            hid->update(getTitleCount());
            directoryList->resetIndex();
        }
    }
    else
    {
        directoryList->updateSelection();
    }
}

static void drawSelector(void)
{
    static const int w = 2;
    const int x = selectorX(hid->index());
    const int y = selectorY(hid->index());
    float highlight_multiplier = fmax(0.0, fabs(fmod(g_timer, 1.0) - 0.5) / 0.5);
    u8 r = COLOR_SELECTOR & 0xFF;
    u8 g = (COLOR_SELECTOR >> 8) & 0xFF;
    u8 b = (COLOR_SELECTOR >> 16) & 0xFF;
    u32 color = C2D_Color32(r + (255 - r) * highlight_multiplier, g + (255 - g) * highlight_multiplier, b + (255 - b) * highlight_multiplier, 255);

    C2D_DrawRectSolid(         x,          y, 0.5f, 50,       50, C2D_Color32(255, 255, 255, 100));
    C2D_DrawRectSolid(         x,          y, 0.5f, 50,        w, color); // g_top
    C2D_DrawRectSolid(         x,      y + w, 0.5f,  w, 50 - 2*w, color); // left
    C2D_DrawRectSolid(x + 50 - w,      y + w, 0.5f,  w, 50 - 2*w, color); // right
    C2D_DrawRectSolid(         x, y + 50 - w, 0.5f, 50,        w, color); // g_bottom
}

static int selectorX(size_t i)
{
    return 50*((i % (rowlen*collen)) % collen);
}

static int selectorY(size_t i)
{
    return 20 + 50*((i % (rowlen*collen)) / collen);
}

void Gui::draw(void)
{
    const float scaleInst = 0.7f;
    auto selEnt = MS::selectedEntries();
    const size_t entries = hid->maxVisibleEntries();
    const size_t max = hid->maxEntries(getTitleCount()) + 1;
    const Mode_t mode = Archive::mode();

    C2D_TextBufClear(g_dynamicBuf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(g_top, COLOR_BG);
    C2D_TargetClear(g_bottom, COLOR_BG);

    C2D_SceneBegin(g_top);
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 19, COLOR_GREY_DARK);
    C2D_DrawRectSolid(0, 221, 0.5f, 400, 19, COLOR_GREY_DARK);

    C2D_Text timeText;
    C2D_TextParse(&timeText, g_dynamicBuf, DateTime::timeStr().c_str());
    C2D_TextOptimize(&timeText);
    C2D_DrawText(&timeText, C2D_WithColor, 4.0f, 3.0f, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);
    
    for (size_t k = hid->page()*entries; k < hid->page()*entries + max; k++)
    {
        C2D_DrawImageAt(icon(k), selectorX(k) + 1, selectorY(k) + 1, 0.5f, NULL, 1.0f, 1.0f);
    }

    if (getTitleCount() > 0)
    {
        drawSelector();
    }

    for (size_t k = hid->page()*entries; k < hid->page()*entries + max; k++)
    {
        if (!selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end())
        {
            C2D_DrawRectSolid(selectorX(k) + 31, selectorY(k) + 31, 0.5f, 16, 16, COLOR_WHITE);
            C2D_SpriteSetPos(&checkbox, selectorX(k) + 27, selectorY(k) + 27);
            C2D_DrawSpriteTinted(&checkbox, &checkboxTint);
        }

        if (favorite(k))
        {
            C2D_DrawRectSolid(selectorX(k) + 31, selectorY(k) + 3, 0.5f, 16, 16, COLOR_GOLD);
            C2D_SpriteSetPos(&star, selectorX(k) + 27, selectorY(k) - 1);
            C2D_DrawSpriteTinted(&star, &checkboxTint);
        }
    }

    static const float border = ceilf((400 - (ins1.width + ins2.width + ins3.width) * 0.47f) / 2);
    C2D_DrawText(&ins1, C2D_WithColor, border, 223, 0.5f, 0.47f, 0.47f, COLOR_WHITE);
    C2D_DrawText(&ins2, C2D_WithColor, border + ceilf(ins1.width*0.47f), 223, 0.5f, 0.47f, 0.47f, Archive::mode() == MODE_SAVE ? COLOR_WHITE : COLOR_RED);
    C2D_DrawText(&ins3, C2D_WithColor, border + ceilf((ins1.width + ins2.width)*0.47f), 223, 0.5f, 0.47f, 0.47f, COLOR_WHITE);

    if (hidKeysHeld() & KEY_SELECT)
    {
        const u32 inst_lh = scaleInst * fontGetInfo()->lineFeed;
        const u32 inst_h = ceilf((240 - scaleInst * inst_lh * 6) / 2);
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C2D_Color32(0, 0, 0, 190));
        C2D_DrawText(&top_move, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_move, scaleInst)) / 2), inst_h, 0.9f, scaleInst, scaleInst, COLOR_WHITE);
        C2D_DrawText(&top_a, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_a, scaleInst)) / 2), inst_h + inst_lh * 1, 0.9f, scaleInst, scaleInst, COLOR_WHITE);
        C2D_DrawText(&top_b, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_b, scaleInst)) / 2), inst_h + inst_lh * 2, 0.9f, scaleInst, scaleInst, COLOR_WHITE);
        C2D_DrawText(&top_y, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_y, scaleInst)) / 2), inst_h + inst_lh * 3, 0.9f, scaleInst, scaleInst, COLOR_WHITE);
        C2D_DrawText(&top_my, C2D_WithColor, ceilf((400 - StringUtils::textWidth(top_my, scaleInst)) / 2), inst_h + inst_lh * 4, 0.9f, scaleInst, scaleInst, COLOR_WHITE);
    }

    C2D_DrawText(&version, C2D_WithColor, 400 - 4 - ceilf(0.45f*version.width), 3.0f, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);
    C2D_DrawImageAt(flag, 400 - 24 - ceilf(version.width*0.45f), 0.0f, 0.5f, NULL, 1.0f, 1.0f);
    C2D_DrawText(&checkpoint, C2D_WithColor, 400 - 6 - 0.45f*version.width - 0.5f*checkpoint.width - 19, 2.0f, 0.5f, 0.5f, 0.5f, COLOR_WHITE);

    C2D_SceneBegin(g_bottom);
    C2D_DrawRectSolid(0, 0, 0.5f, 320, 19, COLOR_GREY_DARK);
    C2D_DrawRectSolid(0, 221, 0.5f, 320, 19, COLOR_GREY_DARK);
    if (getTitleCount() > 0)
    {
        Title title;
        getTitle(title, hid->fullIndex());

        directoryList->flush();
        std::vector<std::u16string> dirs = mode == MODE_SAVE ? title.saves() : title.extdata();
        static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> convert;

        for (size_t i = 0; i < dirs.size(); i++)
        {
            directoryList->push_back(COLOR_GREY_DARKER, COLOR_WHITE, convert.to_bytes(dirs.at(i)), i == directoryList->index());
        }

        C2D_Text shortDesc, longDesc, id, prodCode, media;

        char lowid[18];
        snprintf(lowid, 9, "%08X", (int)title.lowId());

        C2D_TextParse(&shortDesc, g_dynamicBuf, title.shortDescription().c_str());
        C2D_TextParse(&longDesc, g_dynamicBuf, title.longDescription().c_str());
        C2D_TextParse(&id, g_dynamicBuf, lowid);
        C2D_TextParse(&media, g_dynamicBuf, title.mediaTypeString().c_str());

        C2D_TextOptimize(&shortDesc);
        C2D_TextOptimize(&longDesc);
        C2D_TextOptimize(&id);
        C2D_TextOptimize(&media);

        float longDescHeight, lowidWidth;
        C2D_TextGetDimensions(&longDesc, 0.55f, 0.55f, NULL, &longDescHeight);
        C2D_TextGetDimensions(&id, 0.5f, 0.5f, &lowidWidth, NULL);

        C2D_DrawText(&shortDesc, C2D_WithColor, 4, 1, 0.5f, 0.6f, 0.6f, COLOR_WHITE);
        C2D_DrawText(&longDesc, C2D_WithColor, 4, 27, 0.5f, 0.55f, 0.55f, COLOR_GREY_LIGHT);

        C2D_DrawText(&c2dId, C2D_WithColor, 4, 31 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
        C2D_DrawText(&id, C2D_WithColor, 25, 31 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_WHITE);

        snprintf(lowid, 18, "(%s)", title.productCode);
        C2D_TextParse(&prodCode, g_dynamicBuf, lowid);
        C2D_TextOptimize(&prodCode);
        C2D_DrawText(&prodCode, C2D_WithColor, 30 + lowidWidth, 32 + longDescHeight, 0.5f, 0.42f, 0.42f, COLOR_GREY_LIGHT);
        C2D_DrawText(&c2dMediatype, C2D_WithColor, 4, 47 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
        C2D_DrawText(&media, C2D_WithColor, 75, 47 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_WHITE);

        C2D_DrawRectSolid(260, 27, 0.5f, 52, 52, COLOR_BLACK);
        C2D_DrawImageAt(title.icon(), 262, 29, 0.5f, NULL, 1.0f, 1.0f);

        C2D_DrawRectSolid(4, 100, 0.5f, 312, 114, COLOR_GREY_DARK);
        directoryList->draw(g_bottomScrollEnabled);
        buttonBackup->draw(0.7, 0);
        buttonRestore->draw(0.7, 0);
        buttonCheats->draw(0.7, 0);
    }

    C2D_DrawText(&ins4, C2D_WithColor, ceilf((320 - ins4.width*0.47f) / 2), 223, 0.5f, 0.47f, 0.47f, COLOR_WHITE);
    
    if (hidKeysHeld() & KEY_SELECT)
    {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, C2D_Color32(0, 0, 0, 190));
        C2D_DrawText(&bot_ts, C2D_WithColor, 32, 134, 0.5f, scaleInst, scaleInst, COLOR_WHITE);
        C2D_DrawText(&bot_b, C2D_WithColor, 238, 107, 0.5f, 0.8f, 0.8f, COLOR_WHITE);
        C2D_DrawText(&bot_r, C2D_WithColor, 238, 145, 0.5f, 0.8f, 0.8f, COLOR_WHITE);
        C2D_DrawText(&bot_t, C2D_WithColor, 252, 183, 0.5f, 0.8f, 0.8f, COLOR_WHITE);
    }

    frameEnd();
}

bool Gui::isBackupReleased(void)
{
    return buttonBackup->released();
}

bool Gui::isRestoreReleased(void)
{
    return buttonRestore->released();
}

bool Gui::isCheatReleased(void)
{
    return buttonCheats->released();
}

void Gui::resetIndex(void)
{
    hid->reset();
}

void Gui::frameEnd(void)
{
    C3D_FrameEnd(0);
    g_timer += 0.025f;
}
/*
*   This file is part of Checkpoint
*   Copyright (C) 2017-2018 Bernardo Giordano
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
static C3D_RenderTarget* top;
static C3D_RenderTarget* bottom;

static C2D_SpriteSheet spritesheet;
static C2D_Image flag;
static C2D_Sprite checkbox;
static C2D_ImageTint checkboxTint;

static C2D_TextBuf staticBuf, dynamicBuf;
static C2D_Text copyText;
static C2D_Text ins1, ins2, ins3, ins4, c2dId, c2dMediatype;
static C2D_Text checkpoint, version;

// gui elements
static Info* info;
static Clickable* buttonBackup;
static Clickable* buttonRestore;
static MessageBox* messageBox;
static MessageBox* copyList;
static Scrollable* directoryList;

static const size_t rowlen = 4;
static const size_t collen = 8;
static bool bottomScrollEnabled;
static Hid* hid;

static void drawBackground(gfxScreen_t screen);
static void drawSelector(void);
static int selectorX(size_t i);
static int selectorY(size_t i);

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

/// Multi selection

static std::vector<size_t> selEnt;

std::vector<size_t> Gui::selectedEntries(void)
{
    return selEnt;
}

bool Gui::multipleSelectionEnabled(void)
{
    return !selEnt.empty();
}

void Gui::clearSelectedEntries(void)
{
    selEnt.clear();
}

void Gui::addSelectedEntry(size_t idx)
{
    int existing = -1;
    for (size_t i = 0, sz = selEnt.size(); i < sz && existing == -1; i++)
    {
        if (selEnt.at(i) == idx)
        {
            existing = (int)i;
        }
    }
    
    if (existing == -1)
    {
        selEnt.push_back(idx);
    }
    else
    {
        selEnt.erase(selEnt.begin() + existing);
    }
}

/// Gui implementation

static void drawBackground(gfxScreen_t screen)
{
    if (screen == GFX_TOP)
    {
        C2D_DrawRectSolid(0, 0, 0.5f, 400, 19, COLOR_GREY_DARK);
        C2D_DrawRectSolid(0, 221, 0.5f, 400, 19, COLOR_GREY_DARK);

        C2D_Text timeText;
        C2D_TextBufClear(dynamicBuf);
        C2D_TextParse(&timeText, dynamicBuf, DateTime::timeStr().c_str());
        C2D_TextOptimize(&timeText);

        C2D_DrawText(&timeText, C2D_WithColor, 4.0f, 3.0f, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);
        C2D_DrawText(&version, C2D_WithColor, 400 - 4 - ceilf(0.45f*version.width), 3.0f, 0.5f, 0.45f, 0.45f, COLOR_GREY_LIGHT);
        C2D_DrawImageAt(flag, 400 - 24 - ceilf(version.width*0.45f), 0.0f, 0.5f, NULL, 1.0f, 1.0f);
        C2D_DrawText(&checkpoint, C2D_WithColor, 400 - 6 - 0.45f*version.width - 0.5f*checkpoint.width - 19, 2.0f, 0.5f, 0.5f, 0.5f, COLOR_WHITE);	
    }
    else
    {
        C2D_DrawRectSolid(0, 0, 0.5f, 320, 19, COLOR_GREY_DARK);
        C2D_DrawRectSolid(0, 221, 0.5f, 320, 19, COLOR_GREY_DARK);		
    }
}

void Gui::drawCopy(const std::u16string& src, u32 offset, u32 size)
{
    copyList->clear();
    copyList->push_message("Copying " + StringUtils::UTF16toUTF8(src));

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top, COLOR_BG);
    C2D_TargetClear(bottom, COLOR_BG);
    
    C2D_SceneBegin(top);
    drawBackground(GFX_TOP);
    copyList->draw();

    C2D_SceneBegin(bottom);
    drawBackground(GFX_BOTTOM);
    
    static const int barHeight = 19;
    static const int progressBarHeight = 50;
    static const int spacingFromSides = 20;
    static const int spacingFromBars = (240 - barHeight * 2 - progressBarHeight) / 2;
    static const int width = 320 - spacingFromSides * 2;
    
    C2D_DrawRectSolid(spacingFromSides - 2, barHeight + spacingFromBars - 2, 0.5f, width + 4, progressBarHeight + 4, COLOR_GREY_LIGHT);
    C2D_DrawRectSolid(spacingFromSides, barHeight + spacingFromBars, 0.5f, width, progressBarHeight, COLOR_WHITE);
    C2D_DrawRectSolid(spacingFromSides, barHeight + spacingFromBars, 0.5f, (float)offset / (float)size * width, progressBarHeight, C2D_Color32(116, 222, 126, 255));
    
    std::string sizeString = StringUtils::sizeString(offset) + " of " + StringUtils::sizeString(size);
    C2D_TextBufClear(dynamicBuf);
    C2D_TextParse(&copyText, dynamicBuf, sizeString.c_str());
    C2D_TextOptimize(&copyText);
    C2D_DrawText(&copyText, 0, (320 - ceilf(copyText.width*0.5f))/2, 112, 0.5f, 0.5f, 0.5f);
    C3D_FrameEnd(0);
}

bool Gui::askForConfirmation(const std::string& text)
{
    bool ret = false;
    Clickable* buttonYes = new Clickable(40, 90, 100, 60, COLOR_WHITE, COLOR_BLACK, "\uE000 Yes", true);
    Clickable* buttonNo = new Clickable(180, 90, 100, 60, COLOR_WHITE, COLOR_BLACK, "\uE001 No", true);
    MessageBox* message = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE, GFX_TOP);
    message->push_message(text);
    
    while(aptMainLoop() && !(buttonNo->released() || hidKeysDown() & KEY_B))
    {
        hidScanInput();
        if (buttonYes->released() || hidKeysDown() & KEY_A)
        {
            ret = true;
            break;
        }
        
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, COLOR_BG);
        C2D_TargetClear(bottom, COLOR_BG);
        
        C2D_SceneBegin(top);    
        drawBackground(GFX_TOP);
        message->draw();

        C2D_SceneBegin(bottom);
        drawBackground(GFX_BOTTOM);
        C2D_DrawRectSolid(38, 88, 0.5f, 104, 64, COLOR_GREY_LIGHT);
        C2D_DrawRectSolid(178, 88, 0.5f, 104, 64, COLOR_GREY_LIGHT);
        buttonYes->draw();
        buttonNo->draw();
        C3D_FrameEnd(0);
    }
    
    delete message;
    delete buttonYes;
    delete buttonNo;
    return ret;
}

void Gui::createInfo(const std::string& title, const std::string& message)
{
    info->init(title, message, 500, TYPE_INFO);
}

void Gui::createError(Result res, const std::string& message)
{
    info->init(res, message, 500, TYPE_ERROR);
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
    directoryList->index(idx);
}

void Gui::init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    bottomScrollEnabled = false;
    hid = new Hid(rowlen * collen, collen);

    info = new Info();
    info->init("", "", 0, TYPE_INFO);
    buttonBackup = new Clickable(204, 102, 110, 54, COLOR_WHITE, bottomScrollEnabled ? COLOR_BLACK : COLOR_GREY_LIGHT, "Backup \uE004", true);
    buttonRestore = new Clickable(204, 158, 110, 54, COLOR_WHITE, bottomScrollEnabled ? COLOR_BLACK : COLOR_GREY_LIGHT, "Restore \uE005", true);
    copyList = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE, GFX_TOP);
    directoryList = new Scrollable(6, 102, 196, 110, 5);
    messageBox = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE, GFX_TOP);        
    messageBox->push_message("Press \uE000 to enter target.");
    messageBox->push_message("Press \uE004 to backup target.");
    messageBox->push_message("Press \uE005 to restore target.");
    messageBox->push_message("Press \uE001 to exit target or deselect all titles.");
    messageBox->push_message("Press \uE002 to delete a backup.");
    messageBox->push_message("Press \uE003 to multiselect a title.");
    messageBox->push_message("Hold \uE003 to multiselect all titles.");
    messageBox->push_message("Press \uE006 to move between titles.");
    messageBox->push_message("Press \uE054\uE055 to switch page.");
    messageBox->push_message("Hold \uE001 to refresh titles.");

    spritesheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    flag = C2D_SpriteSheetGetImage(spritesheet, sprites_checkpoint_idx);
    C2D_SpriteFromSheet(&checkbox, spritesheet, sprites_checkbox_idx);
    C2D_SpriteSetDepth(&checkbox, 0.6f);
    C2D_PlainImageTint(&checkboxTint, C2D_Color32(88, 88, 88, 255), 1.0f);

    char ver[10];
    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    
    staticBuf = C2D_TextBufNew(128);
    dynamicBuf = C2D_TextBufNew(256);
    C2D_TextParse(&ins1, staticBuf, "Hold SELECT to see commands. Press \uE002 for ");
    C2D_TextParse(&ins2, staticBuf, "extdata");
    C2D_TextParse(&ins3, staticBuf, ".");
    C2D_TextParse(&ins4, staticBuf, "Press \uE073 or START to exit.");
    C2D_TextParse(&version, staticBuf, ver);
    C2D_TextParse(&checkpoint, staticBuf, "checkpoint");
    C2D_TextParse(&c2dId, staticBuf, "ID:");
    C2D_TextParse(&c2dMediatype, staticBuf, "Mediatype:");
    C2D_TextOptimize(&ins1);
    C2D_TextOptimize(&ins2);
    C2D_TextOptimize(&ins3);
    C2D_TextOptimize(&ins4);
    C2D_TextOptimize(&version);
    C2D_TextOptimize(&checkpoint);
    C2D_TextOptimize(&c2dId);
    C2D_TextOptimize(&c2dMediatype);
}

void Gui::exit(void)
{
    C2D_TextBufDelete(dynamicBuf);
    C2D_TextBufDelete(staticBuf);
    C2D_SpriteSheetFree(spritesheet);
    delete hid;
    delete messageBox;
    delete directoryList;
    delete copyList;
    delete buttonRestore;
    delete buttonBackup;
    delete info;
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

bool Gui::bottomScroll(void)
{
    return bottomScrollEnabled;
}

size_t Gui::index(void)
{
    return hid->fullIndex();
}

void Gui::bottomScroll(bool enable)
{
    bottomScrollEnabled = enable;
}

void Gui::updateButtonsColor(void)
{
    if (bottomScrollEnabled)
    {
        buttonBackup->setColors(COLOR_WHITE, COLOR_BLACK);
        buttonRestore->setColors(COLOR_WHITE, COLOR_BLACK);
    }
    else
    {
        buttonBackup->setColors(COLOR_WHITE, COLOR_GREY_LIGHT);
        buttonRestore->setColors(COLOR_WHITE, COLOR_GREY_LIGHT);		
    }
}

void Gui::updateSelector(void)
{
    if (!bottomScrollEnabled)
    {
        hid->update(getTitleCount());
        directoryList->resetIndex();
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
    C2D_DrawRectSolid(         x,          y, 0.5f, 50,       50, C2D_Color32(255, 255, 255, 200)); 
    C2D_DrawRectSolid(         x,          y, 0.5f, 50,        w, COLOR_RED); // top
    C2D_DrawRectSolid(         x,      y + w, 0.5f,  w, 50 - 2*w, COLOR_RED); // left
    C2D_DrawRectSolid(x + 50 - w,      y + w, 0.5f,  w, 50 - 2*w, COLOR_RED); // right
    C2D_DrawRectSolid(         x, y + 50 - w, 0.5f, 50,        w, COLOR_RED); // bottom
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
    const size_t entries = hid->maxVisibleEntries();
    const size_t max = hid->maxEntries(getTitleCount()) + 1;
    const Mode_t mode = Archive::mode();
 
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top, COLOR_BG);
    C2D_TargetClear(bottom, COLOR_BG);
    
    C2D_SceneBegin(top);
    drawBackground(GFX_TOP);

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
    }

    static const float border = ceilf((400 - (ins1.width + ins2.width + ins3.width) * 0.47f) / 2);
    C2D_DrawText(&ins1, C2D_WithColor, border, 224, 0.5f, 0.47f, 0.47f, COLOR_WHITE);
    C2D_DrawText(&ins2, C2D_WithColor, border + ceilf(ins1.width*0.47f), 224, 0.5f, 0.47f, 0.47f, Archive::mode() == MODE_SAVE ? COLOR_WHITE : COLOR_RED);
    C2D_DrawText(&ins3, C2D_WithColor, border + ceilf((ins1.width + ins2.width)*0.47f), 224, 0.5f, 0.47f, 0.47f, COLOR_WHITE);
    
    info->draw();
    
    if (hidKeysHeld() & KEY_SELECT)
    {
        messageBox->draw();
    }
        
    C2D_SceneBegin(bottom);
    drawBackground(GFX_BOTTOM);  
    if (getTitleCount() > 0)
    {
        Title title;
        getTitle(title, hid->fullIndex());
        
        directoryList->flush();
        std::vector<std::u16string> dirs = mode == MODE_SAVE ? title.saves() : title.extdata();
        static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> convert;
        
        for (size_t i = 0; i < dirs.size(); i++)
        {
            directoryList->push_back(COLOR_WHITE, bottomScrollEnabled ? COLOR_BLUE : COLOR_GREY_LIGHT, convert.to_bytes(dirs.at(i)));
            if (i == directoryList->index())
            {
                directoryList->invertCellColors(i);
            }
        }

        C2D_TextBufClear(dynamicBuf);
        C2D_Text shortDesc, longDesc, id, prodCode, media;

        char lowid[18];
        snprintf(lowid, 9, "%08X", (int)title.lowId());
        
        C2D_TextParse(&shortDesc, dynamicBuf, title.shortDescription().c_str());
        C2D_TextParse(&longDesc, dynamicBuf, title.longDescription().c_str());
        C2D_TextParse(&id, dynamicBuf, lowid);
        C2D_TextParse(&media, dynamicBuf, title.mediaTypeString().c_str());

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
        C2D_TextParse(&prodCode, dynamicBuf, lowid);
        C2D_TextOptimize(&prodCode);
        C2D_DrawText(&prodCode, C2D_WithColor, 30 + lowidWidth, 32 + longDescHeight, 0.5f, 0.42f, 0.42f, COLOR_GREY_LIGHT);
        C2D_DrawText(&c2dMediatype, C2D_WithColor, 4, 47 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_GREY_LIGHT);
        C2D_DrawText(&media, C2D_WithColor, 75, 47 + longDescHeight, 0.5f, 0.5f, 0.5f, COLOR_WHITE);
        
        C2D_DrawRectSolid(260, 27, 0.5f, 52, 52, COLOR_BLACK);
        C2D_DrawImageAt(title.icon(), 262, 29, 0.5f, NULL, 1.0f, 1.0f);
        
        C2D_DrawRectSolid(4, 100, 0.5f, 312, 114, COLOR_GREY_LIGHT);
        C2D_DrawRectSolid(6, 102, 0.5f, 308, 110, COLOR_GREY_DARK);

        directoryList->draw();
        buttonBackup->draw();
        buttonRestore->draw();
        
        C2D_DrawRectSolid(202, 102, 0.5f, 2, 110, COLOR_GREY_LIGHT);
        C2D_DrawRectSolid(204, 156, 0.5f, 110, 2, COLOR_GREY_LIGHT);
    }
    
    C2D_DrawText(&ins4, C2D_WithColor, ceilf((320 - ins4.width*0.47f) / 2), 224, 0.5f, 0.47f, 0.47f, COLOR_WHITE);
    C3D_FrameEnd(0);
}

bool Gui::isBackupReleased(void)
{
    return buttonBackup->released() && bottomScrollEnabled;
}

bool Gui::isRestoreReleased(void)
{
    return buttonRestore->released() && bottomScrollEnabled;
}

void Gui::resetIndex(void)
{
    hid->reset();
}

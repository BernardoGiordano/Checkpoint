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

u8* framebuf;
u32 framebuf_width;

static Info* info;
static Clickable* buttonBackup;
static Clickable* buttonRestore;
static MessageBox* messageBox;
static MessageBox* copyList;
static Scrollable* titleList;
static Scrollable* backupList;

static const size_t cols = 14;
static bool backupScrollEnabled;

static char ver[8];

void Gui::createInfo(const std::string& title, const std::string& message)
{
    info->init(title, message, 300, TYPE_INFO);
}

void Gui::createError(Result res, const std::string& message)
{
    info->init(res, message, 300, TYPE_ERROR);
}

size_t Gui::count(entryType_t type)
{
    return type == TITLES ? titleList->size() : backupList->size();
}

std::string Gui::nameFromCell(entryType_t type, size_t index)
{
    return type == TITLES ? titleList->cellName(index) : backupList->cellName(index);
}

void Gui::resetIndex(entryType_t type)
{
    if (type == TITLES)
    {
        titleList->resetIndex();
    }
    else
    {
        backupList->resetIndex();
    }
}

size_t Gui::index(entryType_t type)
{
    return type == TITLES ? titleList->index() : backupList->index();
}

void Gui::index(entryType_t type, size_t i)
{
    if (type == TITLES)
    {
        titleList->index(i);
    }
    else
    {
        backupList->index(i);
    }
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

static void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, color_t color)
{
    rectangle(x - size, y - size, w + 2*size, size, color); // top
    rectangle(x - size, y, size, h, color); // left
    rectangle(x + w, y, size, h, color); // right
    rectangle(x - size, y + h, w + 2*size, size, color); // bottom
}

static void drawBackground(void)
{
    framebuf = gfxGetFramebuffer(&framebuf_width, NULL);
    memset(framebuf, 51, gfxGetFramebufferSize());

    rectangle(0, 0, 1280, 40, COLOR_GREY_DARK);
    rectangle(0, 680, 1280, 40, COLOR_GREY_DARK);

    u32 ver_w, checkpoint_w; 
    GetTextDimensions(font20, ver, &ver_w, NULL);
    GetTextDimensions(font24, "checkpoint", &checkpoint_w, NULL);
    DrawText(font20, 10, 7, COLOR_GREY_LIGHT, DateTime::timeStr().c_str());
    DrawText(font24, 1280 - 10 - ver_w - 40 - 12 - checkpoint_w, 0, COLOR_WHITE, "checkpoint");
    DrawText(font20, 1280 - 10 - ver_w, 7, COLOR_GREY_LIGHT, ver);
    DrawImage(1280 - 10 - ver_w - 40 - 6, 0, 40, 40, flag_bin, IMAGE_MODE_RGBA32); 
}

void Gui::drawCopy(const std::string& src, u64 offset, u64 size)
{
    copyList->clear();
    copyList->push_message("Copying " + src);

    drawBackground();
    copyList->draw();
    
    static const int barHeight = 40;
    static const int progressBarHeight = 50;
    static const int spacingFromSides = 200;
    static const int spacingFromBars = 220 + (720 - barHeight * 2 - progressBarHeight) / 2;
    static const int width = 1280 - spacingFromSides * 2;
    
    rectangle(spacingFromSides - 2, barHeight + spacingFromBars - 2, width + 4, progressBarHeight + 4, COLOR_GREY_LIGHT);
    rectangle(spacingFromSides, barHeight + spacingFromBars, width, progressBarHeight, COLOR_WHITE);
    rectangle(spacingFromSides, barHeight + spacingFromBars, (float)offset / (float)size * width, progressBarHeight, MakeColor(116, 222, 126, 255));
    
    std::string sizeString = StringUtils::sizeString(offset) + " of " + StringUtils::sizeString(size);
    
    u32 textw;
    GetTextDimensions(font20, sizeString.c_str(), &textw, NULL);
    DrawText(font20, ceilf((1280 - textw)/2), spacingFromBars + 48, COLOR_BLACK, sizeString.c_str());

    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
}

bool Gui::askForConfirmation(const std::string& text)
{
    bool ret = false;
    Clickable* buttonYes = new Clickable(293, 540, 200, 80, COLOR_WHITE, COLOR_BLACK, "Yes", true);
    Clickable* buttonNo = new Clickable(786, 540, 200, 80, COLOR_WHITE, COLOR_BLACK, "No", true);
    MessageBox* message = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE);
    message->push_message(text);
    
    while(appletMainLoop() && !(buttonNo->released() || hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B))
    {
        hidScanInput();
        if (buttonYes->released() || hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A)
        {
            ret = true;
            break;
        }
        
        drawBackground();
        message->draw();

        drawOutline(293, 540, 200, 80, 4, COLOR_GREY_LIGHT);
        drawOutline(786, 540, 200, 80, 4, COLOR_GREY_LIGHT);
        buttonYes->draw();
        buttonNo->draw();

        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }
    
    delete message;
    delete buttonYes;
    delete buttonNo;
    return ret;
}

void Gui::init(void)
{
    gfxInitDefault();
    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    backupScrollEnabled = false;
    info = new Info();
    info->init("", "", 0, TYPE_INFO);
    titleList = new Scrollable(40, 80, 398, 560, cols);
    backupList = new Scrollable(444, 80, 398, 560, cols);
    buttonBackup = new Clickable(984, 476, 256, 80, COLOR_WHITE, COLOR_GREY_LIGHT, "Backup", true);
    buttonRestore = new Clickable(984, 560, 256, 80, COLOR_WHITE, COLOR_GREY_LIGHT, "Restore", true);
    copyList = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE);
    messageBox = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE);        
    messageBox->push_message("Press A to enter target.");
    messageBox->push_message("Press B to exit target or deselect all titles.");
    messageBox->push_message("Press X to delete a backup.");
    messageBox->push_message("Press Y to multiselect a title.");
    messageBox->push_message("Hold Y to multiselect all titles.");
    messageBox->push_message("Press the arrows to move between titles.");
    messageBox->push_message("Press L/R to switch page.");
}

void Gui::exit(void)
{
    delete info;
    delete titleList;
    delete backupList;
    delete copyList;
    delete buttonBackup;
    delete buttonRestore;
    delete messageBox;
    gfxExit();
}

void Gui::draw(void)
{
    titleList->flush();
    backupList->flush();

    // draw
    drawBackground();

    drawOutline(984, 476, 256, 164, 4, COLOR_GREY_LIGHT);
    rectangle(984, 556, 256, 4, COLOR_GREY_LIGHT);
    drawOutline(40, 80, 804, 560, 4, COLOR_GREY_LIGHT);
    rectangle(440, 80, 4, 560, COLOR_GREY_LIGHT);
    // TODO: optimize
    rectangle(40, 80, 398, 560, COLOR_GREY_DARK);
    rectangle(444, 80, 398, 560, COLOR_GREY_DARK);

    for (size_t i = 0, sz = getTitleCount(); i < sz; i++)
    {
        Title title;
        getTitle(title, i);
        titleList->push_back(COLOR_WHITE, !backupScrollEnabled ? COLOR_BLUE : COLOR_GREY_LIGHT, title.name());
    
        if (i == Gui::index(TITLES))
        {
            std::vector<std::string> dirs = title.saves();
            for (size_t j = 0, amount = dirs.size(); j < amount; j++)
            {
                backupList->push_back(COLOR_WHITE, backupScrollEnabled ? COLOR_BLUE : COLOR_GREY_LIGHT, dirs.at(j));
            }

            // descriptions
            u32 userw;
            DrawText(font14, 980, 354, COLOR_GREY_LIGHT, "User: ");
            GetTextDimensions(font14, "User:  ", &userw, NULL);
            DrawText(font14, 980 + userw, 354, COLOR_WHITE, Account::username(title.userId()).c_str());

            // icon
            drawOutline(984, 80, 256, 256, 4, COLOR_BLACK);
            if (title.icon() != NULL)
            {
                DrawImage(984, 80, 256, 256, title.icon(), IMAGE_MODE_RGB24);
            }
            else
            {
                rectangle(984, 80, 256, 256, COLOR_WHITE);
            }
        }
    }

    titleList->invertCellColors(titleList->index());
    if (backupScrollEnabled)
    {
        backupList->invertCellColors(backupList->index());
    }

    titleList->draw();
    backupList->draw();
    buttonBackup->draw();
    buttonRestore->draw();

    for (size_t k = titleList->page()*14; k < titleList->page()*14 + titleList->maxVisibleEntries(); k++)
    {
        if (!selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end())
        {
            DrawImage(398, 80+40*(k%14), 40, 40, k == titleList->index() ? checkbox_white_bin : checkbox_grey_bin, IMAGE_MODE_RGBA32); 
        }
    }

    info->draw();
    
    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_MINUS)
    {
        messageBox->draw();
    }

    u32 ins_w, ins_h;
    const char* instructions = "Hold - to see commands. Press + to exit.";
    GetTextDimensions(font20, instructions, &ins_w, &ins_h);
    DrawText(font20, ceil((1280 - ins_w) / 2), 680 + 5, COLOR_WHITE, instructions);

    gfxFlushBuffers();
    gfxSwapBuffers();
    gfxWaitForVsync();
}

bool Gui::isBackupReleased(void)
{
    return buttonBackup->released() && backupScrollEnabled;
}

bool Gui::isRestoreReleased(void)
{
    return buttonRestore->released() && backupScrollEnabled;
}

bool Gui::backupScroll(void)
{
    return backupScrollEnabled;
}

void Gui::backupScroll(bool enable)
{
    backupScrollEnabled = enable;
}

void Gui::updateButtonsColor(void)
{
    if (backupScrollEnabled)
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
    if (!backupScrollEnabled)
    {
        titleList->updateSelection();
        backupList->resetIndex();
    }
    else
    {
        backupList->updateSelection();
    }
}
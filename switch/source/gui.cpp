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

static Info* info;
static Clickable* buttonBackup;
static Clickable* buttonRestore;
static MessageBox* messageBox;
static MessageBox* copyList;
static Scrollable* backupList;

static const size_t rowlen = 5;
static const size_t collen = 4;
static const size_t cols = 6;
static bool backupScrollEnabled;
static Hid* hid;
static entryType_t type;

static float currentTime = 0;
static int selectorX(size_t i);
static int selectorY(size_t i);

static char ver[8];

void Gui::entryType(entryType_t type_)
{
    type = type_;
}

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
    return type == TITLES ? getTitleCount(g_currentUId) : backupList->size();
}

std::string Gui::nameFromCell(size_t index)
{
    return backupList->cellName(index);
}

void Gui::resetIndex(entryType_t type)
{
    if (type == TITLES)
    {
        hid->reset();
    }
    else
    {
        backupList->resetIndex();
    }
}

size_t Gui::index(entryType_t type)
{
    return type == TITLES ? hid->fullIndex() : backupList->index();
}

void Gui::index(entryType_t type, size_t i)
{
    if (type == TITLES)
    {
        hid->page(i / hid->maxVisibleEntries());
        hid->index(i - hid->page() * hid->maxVisibleEntries());
    }
    else
    {
        backupList->setIndex(i);
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

static void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color)
{
    SDLH_DrawRect(x - size, y - size, w + 2*size, size, color); // top
    SDLH_DrawRect(x - size, y, size, h, color); // left
    SDLH_DrawRect(x + w, y, size, h, color); // right
    SDLH_DrawRect(x - size, y + h, w + 2*size, size, color); // bottom
}

static void drawBackground(void)
{
    SDLH_ClearScreen(COLOR_GREY_BG);

    const u8 bar_height = 28;
    const u8 image_dim = 32;
    SDLH_DrawRect(0, 0, 1280, bar_height, COLOR_GREY_DARK);
    SDLH_DrawRect(0, 720 - bar_height, 1280, bar_height, COLOR_GREY_DARK);

    u32 ver_w, ver_h, checkpoint_w; 
    SDLH_GetTextDimensions(23, ver, &ver_w, &ver_h);
    SDLH_GetTextDimensions(30, "checkpoint", &checkpoint_w, NULL);
    u32 h = (bar_height - ver_h) / 2;
    SDLH_DrawText(23, 8, h + 3, COLOR_GREY_LIGHT, DateTime::timeStr().c_str());
    SDLH_DrawText(30, 1280 - 10 - ver_w - image_dim - 12 - checkpoint_w, h - 2, COLOR_WHITE, "checkpoint");
    SDLH_DrawText(23, 1280 - 10 - ver_w, h + 3, COLOR_GREY_LIGHT, ver);
    SDLH_DrawIcon("flag", 1280 - 10 - ver_w - image_dim - 6, -2); 
    // shadow
    SDLH_DrawRect(0, bar_height, 1280, 2, COLOR_GREY_DARKER);
    // patch the p
    SDLH_DrawRect(1080, bar_height + 2, 8, 8, COLOR_GREY_BG);
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
    
    SDLH_DrawRect(spacingFromSides - 2, barHeight + spacingFromBars - 2, width + 4, progressBarHeight + 4, COLOR_GREY_LIGHT);
    SDLH_DrawRect(spacingFromSides, barHeight + spacingFromBars, width, progressBarHeight, COLOR_WHITE);
    SDLH_DrawRect(spacingFromSides, barHeight + spacingFromBars, (float)offset / (float)size * width, progressBarHeight, FC_MakeColor(116, 222, 126, 255));
    
    std::string sizeString = StringUtils::sizeString(offset) + " of " + StringUtils::sizeString(size);
    
    u32 textw, texth;
    SDLH_GetTextDimensions(30, sizeString.c_str(), &textw, &texth);
    SDLH_DrawText(30, ceilf((1280 - textw)/2), spacingFromBars + barHeight + (progressBarHeight - texth) / 2, COLOR_BLACK, sizeString.c_str());
    SDLH_Render();
}

bool Gui::askForConfirmation(const std::string& text)
{
    bool ret = false;
    Clickable* buttonYes = new Clickable(293, 540, 200, 80, COLOR_WHITE, COLOR_BLACK, "Yes \ue000", true);
    Clickable* buttonNo = new Clickable(786, 540, 200, 80, COLOR_WHITE, COLOR_BLACK, "No \ue001", true);
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

        SDLH_Render();
    }
    
    delete message;
    delete buttonYes;
    delete buttonNo;
    return ret;
}

bool Gui::init(void)
{
    if (!SDLH_Init())
    {
        return false;
    }

    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    backupScrollEnabled = false;
    info = new Info();
    info->init("", "", 0, TYPE_INFO);
    hid = new Hid(rowlen * collen, collen);
    backupList = new Scrollable(540, 462, 506, 222, cols);
    buttonBackup = new Clickable(1050, 462, 220, 109, COLOR_WHITE, COLOR_GREY_LIGHT, "Backup \ue004", true);
    buttonRestore = new Clickable(1050, 575, 220, 109, COLOR_WHITE, COLOR_GREY_LIGHT, "Restore \ue005", true);
    copyList = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE);
    messageBox = new MessageBox(COLOR_GREY_DARK, COLOR_WHITE);        
    messageBox->push_message("Press \ue000 to enter target");
    messageBox->push_message("Press \ue001 to exit target or deselect all titles");
    messageBox->push_message("Press \ue004 to backup target");
    messageBox->push_message("Press \ue005 to restore target");
    messageBox->push_message("Press \ue002 to delete a backup");
    messageBox->push_message("Press \ue003 to multiselect a title");
    messageBox->push_message("Hold \ue003 to multiselect all titles");
    messageBox->push_message("Press \ue041 to move between titles");
    messageBox->push_message("Press \ue085/\ue086 to switch user");
    
    return true;
}

void Gui::exit(void)
{
    delete info;
    delete hid;
    delete backupList;
    delete copyList;
    delete buttonBackup;
    delete buttonRestore;
    delete messageBox;
    SDLH_Exit();
}

void Gui::draw(u128 uid)
{
    const u8 bar_height = 28;
    const u8 spacing = 4;
    const size_t entries = hid->maxVisibleEntries();
    const size_t max = hid->maxEntries(getTitleCount(g_currentUId)) + 1;

    // draw
    drawBackground();
    SDLH_DrawRect(0, bar_height + 2, 532, 720 - 2*bar_height - 2, FC_MakeColor(55, 55, 55, 255));

    // user icons
    std::vector<u128> userIds = Account::ids();
    for (size_t i = 0; i < userIds.size(); i++)
    {
        const u32 x = 1280 - (spacing + USER_ICON_SIZE) * (i + 1);
        const u32 y = bar_height + spacing;
        if (i == g_currentUserIndex)
        {
            float highlight_multiplier = fmax(0.0, fabs(fmod(currentTime, 1.0) - 0.5) / 0.5);
            SDL_Color color = COLOR_GREEN;
            color = FC_MakeColor(color.r + (255 - color.r) * highlight_multiplier, color.g + (255 - color.g) * highlight_multiplier, color.b + (255 - color.b) * highlight_multiplier, 255);    
            drawOutline(x, y, USER_ICON_SIZE, USER_ICON_SIZE, 4, color);
        }

        if (Account::icon(userIds.at(i)) != NULL)
        {
            SDLH_DrawImageScale(Account::icon(userIds.at(i)), x, y, USER_ICON_SIZE, USER_ICON_SIZE);
        }
        else
        {
            SDLH_DrawRect(x, y, USER_ICON_SIZE, USER_ICON_SIZE, COLOR_BLACK);
        }
    }

    // title icons
    for (size_t k = hid->page()*entries; k < hid->page()*entries + max; k++)
    {
        int selectorx = selectorX(k);
        int selectory = selectorY(k);
        if (smallIcon(g_currentUId, k) != NULL)
        {
            SDLH_DrawImageScale(smallIcon(g_currentUId, k), selectorx, selectory, 128, 128);
        }
        else
        {
            SDLH_DrawRect(selectorx, selectory, 128, 128, COLOR_BLACK);
        }

        if (!selEnt.empty() && std::find(selEnt.begin(), selEnt.end(), k) != selEnt.end())
        {
            SDLH_DrawIcon("checkbox", selectorx + 86, selectory + 86);
        }

        if (favorite(g_currentUId, k))
        {
            SDLH_DrawRect(selectorx + 94, selectory + 8, 24, 24, COLOR_GOLD);
            SDLH_DrawIcon("star", selectorx + 86, selectory);
        }
    }

    // title selector
    if (getTitleCount(g_currentUId) > 0)
    {
        const int x = selectorX(hid->index()) + spacing/2;
        const int y = selectorY(hid->index()) + spacing/2;
        float highlight_multiplier = fmax(0.0, fabs(fmod(currentTime, 1.0) - 0.5) / 0.5);
        SDL_Color color = COLOR_BLUE;
        color = FC_MakeColor(color.r + (255 - color.r) * highlight_multiplier, color.g + (255 - color.g) * highlight_multiplier, color.b + (255 - color.b) * highlight_multiplier, 255);
        drawOutline(x, y, 124, 124, spacing, color);
        SDLH_DrawRect(x, y, 124, 124, FC_MakeColor(255, 255, 255, 80));
    }

    if (getTitleCount(g_currentUId) > 0)
    {
        Title title;
        getTitle(title, g_currentUId, hid->fullIndex());
        
        backupList->flush();
        std::vector<std::string> dirs = title.saves();
        
        for (size_t i = 0; i < dirs.size(); i++)
        {
            backupList->push_back(COLOR_WHITE, backupScrollEnabled ? COLOR_BLUE : COLOR_GREY_LIGHT, dirs.at(i));
            if (i == backupList->index())
            {
                backupList->invertCellColors(i);
            }
        }

        if (title.icon() != NULL)
        {
            drawOutline(1016, 157, 256, 256, 4, COLOR_BLACK);
            SDLH_DrawImage(title.icon(), 1016, 157);
        }

        // draw infos
        u32 info_w, info_h, title_w, h, titleid_w, producer_w, user_w;
        SDLH_GetTextDimensions(36, "Title Information", &info_w, &info_h);
        SDLH_GetTextDimensions(23, "Title: ", &title_w, &h);
        SDLH_GetTextDimensions(23, "Title ID: ", &titleid_w, NULL);
        SDLH_GetTextDimensions(23, "Author: ", &producer_w, NULL);
        SDLH_GetTextDimensions(23, "User: ", &user_w, NULL);

        h += 12;
        u32 offset = 159 + 16 + info_h + h/2;
        
        SDLH_DrawRect(536, 159, 468, 16 + info_h, COLOR_GREY_DARK);
        SDLH_DrawRect(536, offset - h/2, 468, h*4 + h/2, COLOR_GREY_DARKER);
        
        SDLH_DrawText(36, 540 - 12 + 468 - info_w, 169, COLOR_GREY_LIGHT, "Title Information");
        SDLH_DrawText(23, 540, offset, COLOR_GREY_LIGHT, "Title: ");
        SDLH_DrawText(23, 540, offset + h, COLOR_GREY_LIGHT, "Title ID: ");
        SDLH_DrawText(23, 540, offset + h*2, COLOR_GREY_LIGHT, "Author: ");
        SDLH_DrawText(23, 540, offset + h*3, COLOR_GREY_LIGHT, "User: ");

        SDLH_DrawTextBox(23, 540 + title_w, offset, COLOR_WHITE, 1012 - 540 - title_w - 4*2, title.name().c_str());
        SDLH_DrawTextBox(23, 540 + titleid_w, offset + h, COLOR_WHITE, 1012 - 540 - titleid_w - 4*2, StringUtils::format("0x%016llX", title.id()).c_str());
        SDLH_DrawTextBox(23, 540 + producer_w, offset + h*2, COLOR_WHITE, 1012 - 540 - producer_w - 4*2, title.author().c_str());
        SDLH_DrawTextBox(23, 540 + user_w, offset + h*3, COLOR_WHITE, 1012 - 540 - user_w - 4*2, title.userName().c_str());

        drawOutline(540, 462, 730, 222, 4, COLOR_GREY_LIGHT);
        SDLH_DrawRect(1046, 462, 4, 222, COLOR_GREY_LIGHT);
        SDLH_DrawRect(1048, 571, 222, 4, COLOR_GREY_LIGHT);
        backupList->draw();
        buttonBackup->draw();
        buttonRestore->draw();
    }

    info->draw();
    
    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_MINUS)
    {
        messageBox->draw();
    }

    u32 ins_w, ins_h;
    const char* instructions = "Hold \ue046 to see commands. Press \ue045 to exit.";
    SDLH_GetTextDimensions(23, instructions, &ins_w, &ins_h);
    SDLH_DrawText(23, ceil((1280 - ins_w) / 2), 720 - bar_height + (bar_height - ins_h) / 2 + 2, COLOR_WHITE, instructions);

    // increase currentTime
    currentTime = SDL_GetTicks() / 1000.f;
    
    SDLH_Render();
}

bool Gui::isBackupReleased(void)
{
    return buttonBackup->released();
}

bool Gui::isRestoreReleased(void)
{
    return buttonRestore->released();
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
    if (Gui::multipleSelectionEnabled())
    {
        buttonBackup->setColors(COLOR_WHITE, COLOR_BLACK);
        buttonRestore->setColors(COLOR_WHITE, COLOR_GREY_LIGHT);
    }
    else if (backupScrollEnabled)
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
        hid->update(getTitleCount(g_currentUId));
        
        // loop through every rendered title
        touchPosition touch;
        hidTouchRead(&touch, 0);
        for (u8 row = 0; row < rowlen; row++)
        {
            for (u8 col = 0; col < collen; col++)
            {
                u8 index = row * collen + col;
                if (index > hid->maxEntries(getTitleCount(g_currentUId)))
                    break;

                u32 x = selectorX(index);
                u32 y = selectorY(index);
                if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_TOUCH &&
                    touch.px >= x && touch.px <= x + 128 &&
                    touch.py >= y && touch.py <= y + 128)
                {
                    hid->index(index);
                }
            }
        }

        backupList->resetIndex();
    }
    else
    {
        backupList->updateSelection();
    }
}

static int selectorX(size_t i)
{
    return 128*((i % (rowlen*collen)) % collen) + 4 * (((i % (rowlen*collen)) % collen) + 1);
}

static int selectorY(size_t i)
{
    return 28 + 128*((i % (rowlen*collen)) / collen) + 4 * (((i % (rowlen*collen)) / collen) + 1);
}
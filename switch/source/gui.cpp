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

static Clickable* buttonBackup;
static Clickable* buttonRestore;
static Clickable* buttonCheats;
static Scrollable* backupList;

static const size_t rowlen = 5;
static const size_t collen = 4;
static const size_t rows = 10;
static const size_t SIDEBAR_w = 96;
static HidHorizontal* hid;
static entryType_t type;

float g_currentTime = 0;
static int selectorX(size_t i);
static int selectorY(size_t i);

static char ver[8];

static bool PKSMBridge = false;

void Gui::entryType(entryType_t type_)
{
    type = type_;
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

static void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color)
{
    SDLH_DrawRect(x - size, y - size, w + 2*size, size, color); // top
    SDLH_DrawRect(x - size, y, size, h, color); // left
    SDLH_DrawRect(x + w, y, size, h, color); // right
    SDLH_DrawRect(x - size, y + h, w + 2*size, size, color); // bottom
}

void Gui::drawCopy(const std::string& src, u64 offset, u64 size)
{
    std::string sizeString = StringUtils::sizeString(offset) + " of " + StringUtils::sizeString(size);
    u32 srcw, srch, textw, texth;
    SDLH_GetTextDimensions(28, src.c_str(), &srcw, &srch);
    SDLH_GetTextDimensions(26, sizeString.c_str(), &textw, &texth);
    SDLH_DrawRect(320, 200, 640, 240, theme().c3);
    SDLH_DrawRect(320, 440, 640, 80, theme().c2);
    SDLH_DrawRect(320, 440, (float)offset / (float)size * 640, 80, COLOR_BLUE);
    SDLH_DrawText(28, ceilf((1280 - srcw) / 2), 200 + ceilf((220 - srch) / 2), theme().c6, src.c_str());
    SDLH_DrawText(26, ceilf((1280 - textw) / 2), 440 + ceilf((80 - texth) / 2), theme().c6, sizeString.c_str());
    SDLH_Render();
}

bool Gui::askForConfirmation(const std::string& text)
{
    bool ret = false;
    Clickable* buttonYes = new Clickable(322, 462, 316, 56, theme().c3, theme().c6, "Yes", true);
    Clickable* buttonNo = new Clickable(642, 462, 316, 56, theme().c3, theme().c6, "No", true);
    HidHorizontal* hid = new HidHorizontal(2, 2);

    u32 w, h;
    SDLH_GetTextDimensions(28, text.c_str(), &w, &h);

    while(appletMainLoop())
    {
        hidScanInput();
        hid->update(2);

        if (buttonYes->released() ||
            ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) && hid->index() == 0))
        {
            ret = true;
            break;
        }
        else if (buttonNo->released() ||
            (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B) ||
            ((hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) && hid->index() == 1))
        {
            break;
        }

        hid->index(buttonYes->held() ? 0 : buttonNo->held() ? 1 : hid->index());
        buttonYes->selected(hid->index() == 0);
        buttonNo->selected(hid->index() == 1);
        
        SDLH_DrawRect(320, 200, 640, 260, theme().c3);
        SDLH_DrawText(28, ceilf(1280 - w) / 2, 200 + ceilf((260 - h) / 2), theme().c6, text.c_str());
        drawOutline(322, 462, 316, 56, 2, theme().c5);
        drawOutline(642, 462, 316, 56, 2, theme().c5);
        buttonYes->draw(28, COLOR_BLUE);
        buttonNo->draw(28, COLOR_BLUE);

        if (hid->index() == 0)
        {
            drawPulsingOutline(324, 464, 312, 52, 4, COLOR_BLUE);
        }
        else
        {
            drawPulsingOutline(644, 464, 312, 52, 4, COLOR_BLUE);
        }
        
        SDLH_Render();
    }
    
    delete hid;
    delete buttonYes;
    delete buttonNo;
    return ret;
}

void Gui::showInfo(const std::string& message)
{
    Clickable* button = new Clickable(322, 462, 636, 56, theme().c3, theme().c6, "OK", true);
    button->selected(true);
    u32 w, h;
    SDLH_GetTextDimensions(28, message.c_str(), &w, &h);

    while(appletMainLoop())
    {
        hidScanInput();

        if (button->released() ||
            (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) ||
            (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B))
        {
            break;
        }
        
        SDLH_DrawRect(320, 200, 640, 260, theme().c3);
        SDLH_DrawText(28, ceilf(1280 - w) / 2, 200 + ceilf((260 - h) / 2), theme().c6, message.c_str());
        button->draw(28, COLOR_BLUE);
        drawPulsingOutline(322, 462, 636, 56, 4, COLOR_BLUE);
        
        SDLH_Render();
    }

    delete button;
}

void Gui::showError(Result res, const std::string& message)
{
    Clickable* button = new Clickable(322, 462, 636, 56, theme().c1, theme().c6, "OK", true);
    button->selected(true);
    u32 w, h;
    SDLH_GetTextDimensions(28, message.c_str(), &w, &h);

    while(appletMainLoop())
    {
        hidScanInput();

        if (button->released() ||
            (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_A) ||
            (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B))
        {
            break;
        }
        
        SDLH_DrawRect(320, 200, 640, 260, theme().c0);
        SDLH_DrawText(20, 330, 210, COLOR_RED, StringUtils::format("Error: 0x%0llX", res).c_str());
        SDLH_DrawText(28, ceilf(1280 - w) / 2, 200 + ceilf((260 - h) / 2), theme().c6, message.c_str());
        button->draw(28, COLOR_RED);
        drawPulsingOutline(322, 462, 636, 56, 4, COLOR_RED);
        
        SDLH_Render();
    }

    delete button;
}

bool Gui::init(void)
{
    if (!SDLH_Init())
    {
        return false;
    }

    sprintf(ver, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    hid = new HidHorizontal(rowlen * collen, collen);
    backupList = new Scrollable(538, 276, 414, 380, rows);
    buttonBackup = new Clickable(956, 276, 220, 80, theme().c2, theme().c6, "Backup \ue004", true);
    buttonRestore = new Clickable(956, 360, 220, 80, theme().c2, theme().c6, "Restore \ue005", true);
    buttonCheats = new Clickable(956, 444, 220, 80, theme().c2, theme().c6, "Cheats", true);
    buttonBackup->canChangeColorWhenSelected(true);
    buttonRestore->canChangeColorWhenSelected(true);
    buttonCheats->canChangeColorWhenSelected(true);
    
    return true;
}

void Gui::exit(void)
{
    delete hid;
    delete backupList;
    delete buttonCheats;
    delete buttonBackup;
    delete buttonRestore;
    SDLH_Exit();
}

void Gui::draw(void)
{
    auto selEnt = MS::selectedEntries();
    const size_t entries = hid->maxVisibleEntries();
    const size_t max = hid->maxEntries(getTitleCount(g_currentUId)) + 1;

    SDLH_ClearScreen(theme().c1);
    SDL_Color colorBar = Gui::getPKSMBridgeFlag() ? COLOR_HIGHBLUE : FC_MakeColor(theme().c1.r - 15, theme().c1.g - 15, theme().c1.b - 15, 255);
    SDLH_DrawRect(0, 0, 532, 662, FC_MakeColor(theme().c1.r + 5, theme().c1.g + 5, theme().c1.b + 5, 255));
    SDLH_DrawRect(1280 - SIDEBAR_w, 0, SIDEBAR_w, 720, colorBar);

    u32 nick_w, nick_h;
    SDLH_GetTextDimensions(13, Account::username(g_currentUId).c_str(), &nick_w, &nick_h);
    drawPulsingOutline(1280 - SIDEBAR_w + (SIDEBAR_w - USER_ICON_SIZE) / 2, 720 - USER_ICON_SIZE - 30, USER_ICON_SIZE, USER_ICON_SIZE, 2, COLOR_GREEN);
    SDLH_DrawImageScale(Account::icon(g_currentUId), 1280 - SIDEBAR_w + (SIDEBAR_w - USER_ICON_SIZE) / 2, 720 - USER_ICON_SIZE - 30, USER_ICON_SIZE, USER_ICON_SIZE);
    SDLH_DrawTextBox(13, 1280 - SIDEBAR_w + (SIDEBAR_w - nick_w) / 2, 720 - 28 + (28 - nick_h) / 2, theme().c6, SIDEBAR_w, Account::username(g_currentUId).c_str());

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
            SDLH_DrawRect(selectorx, selectory, 128, 128, theme().c0);
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
        const int x = selectorX(hid->index()) + 4/2;
        const int y = selectorY(hid->index()) + 4/2;
        drawPulsingOutline(x, y, 124, 124, 4, COLOR_BLUE);
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
            backupList->push_back(theme().c2, theme().c6, dirs.at(i), i == backupList->index());
        }

        if (title.icon() != NULL)
        {
            drawOutline(1018, 6, 256, 256, 4, theme().c3);
            SDLH_DrawImage(title.icon(), 1018, 6);
        }

        // draw infos
        u32 title_w, title_h, h, titleid_w, producer_w, user_w, subtitle_w;
        auto displayName = title.displayName();
        SDLH_GetTextDimensions(28, displayName.first.c_str(), &title_w, &title_h);
        SDLH_GetTextDimensions(23, "Title: ", &subtitle_w, NULL);
        SDLH_GetTextDimensions(23, "Title ID: ", &titleid_w, &h);
        SDLH_GetTextDimensions(23, "Author: ", &producer_w, NULL);
        SDLH_GetTextDimensions(23, "User: ", &user_w, NULL);

        h += 6;
        u32 offset = 10 + title_h + h/2;
        int i = 0;
        
        SDLH_DrawRect(534, 2, 482, 16 + title_h, theme().c3);
        SDLH_DrawRect(534, offset - h/2 - 2, 480, h*(displayName.second.length() > 0 ? 4 : 3) + h/2, theme().c2);
        
        SDLH_DrawText(28, 538 - 8 + 482 - title_w, 8, theme().c5, displayName.first.c_str());
        if (displayName.second.length() > 0)
        {
            SDLH_DrawText(23, 538, offset + h*i, theme().c5, "Title:");
            SDLH_DrawTextBox(23, 538 + subtitle_w, offset + h*(i++), theme().c6, 478 - 4*2 - subtitle_w, displayName.second.c_str());
        }
        
        SDLH_DrawText(23, 538, offset + h*i, theme().c5, "Title ID:");
        SDLH_DrawTextBox(23, 538 + titleid_w, offset + h*(i++), theme().c6, 478 - 4*2 - titleid_w, StringUtils::format("%016llX", title.id()).c_str());
        
        SDLH_DrawText(23, 538, offset + h*i, theme().c5, "Author:");
        SDLH_DrawTextBox(23, 538 + producer_w, offset + h*(i++), theme().c6, 478 - 4*2 - producer_w, title.author().c_str());
        
        SDLH_DrawText(23, 538, offset + h*i, theme().c5, "User:");
        SDLH_DrawTextBox(23, 538 + user_w, offset + h*(i++), theme().c6, 478 - 4*2 - user_w, title.userName().c_str());

        drawOutline(538, 276, 414, 380, 4, theme().c3);
        drawOutline(956, 276, 220, 80, 4, theme().c3);
        drawOutline(956, 360, 220, 80, 4, theme().c3);
        drawOutline(956, 444, 220, 80, 4, theme().c3);
        backupList->draw(g_backupScrollEnabled);
        buttonBackup->draw(30, FC_MakeColor(0, 0, 0, 0));
        buttonRestore->draw(30, FC_MakeColor(0, 0, 0, 0));
        buttonCheats->draw(30, FC_MakeColor(0, 0, 0, 0));
    }
    
    if (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_MINUS)
    {
        SDLH_DrawRect(0, 0, 1280, 720, FC_MakeColor(0, 0, 0, 190));
        SDLH_DrawText(27, 1205, 646, theme().c6, "\ue085\ue086");
        SDLH_DrawText(24, 58, 69, theme().c6, "\ue058 Tap to select title");
        SDLH_DrawText(24, 100, 330, theme().c6, "\ue006 \ue080 to scroll between titles");
        SDLH_DrawText(24, 100, 360, theme().c6, "\ue000 to enter the selected title");
        SDLH_DrawText(24, 100, 390, theme().c6, "\ue001 to exit the selected title");
    }

    SDL_Color lightBlack = FC_MakeColor(theme().c0.r + 20, theme().c0.g + 20, theme().c0.b + 20, 255);
    u32 ver_w, ver_h, checkpoint_h, checkpoint_w, inst_h;
    SDLH_GetTextDimensions(20, ver, &ver_w, &ver_h);
    SDLH_GetTextDimensions(26, "checkpoint", &checkpoint_w, &checkpoint_h);
    SDLH_GetTextDimensions(24, "\ue046 Instructions", NULL, &inst_h);
    SDLH_DrawRect(0, 672, checkpoint_w + ver_w + 2*16 + 8, 40, lightBlack);
    SDLH_DrawText(26, 16, 672 + (40 - checkpoint_h) / 2 + 2, theme().c6, "checkpoint");
    SDLH_DrawText(20, 16 + checkpoint_w + 8, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - ver_h, theme().c6, ver);
    SDLH_DrawText(24, 16*3 + checkpoint_w + 8 + ver_w, 672 + (40 - checkpoint_h) / 2 + checkpoint_h - inst_h, theme().c6, "\ue046 Instructions");
 
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

bool Gui::isCheatReleased(void)
{
    return buttonCheats->released();
}

void Gui::updateButtons(void)
{
    if (MS::multipleSelectionEnabled())
    {
        buttonRestore->canChangeColorWhenSelected(true);
        buttonRestore->canChangeColorWhenSelected(false);
        buttonCheats->canChangeColorWhenSelected(false);
        buttonBackup->setColors(theme().c2, theme().c6);
        buttonRestore->setColors(theme().c2, theme().c5);
        buttonCheats->setColors(theme().c2, theme().c5);
    }
    else if (g_backupScrollEnabled)
    {
        buttonBackup->canChangeColorWhenSelected(true);
        buttonRestore->canChangeColorWhenSelected(true);
        buttonCheats->canChangeColorWhenSelected(true);
        buttonBackup->setColors(theme().c2, theme().c6);
        buttonRestore->setColors(theme().c2, theme().c6);
        buttonCheats->setColors(theme().c2, theme().c6);
    }
    else
    {
        buttonBackup->setColors(theme().c2, theme().c6);
        buttonRestore->setColors(theme().c2, theme().c6);
        buttonCheats->setColors(theme().c2, theme().c6);
    }
    
    if (getPKSMBridgeFlag())
    {
        buttonBackup->text("Send \ue004");
        buttonRestore->text("Receive \ue005");
    }
    else
    {
        buttonBackup->text("Backup \ue004");
        buttonRestore->text("Restore \ue005");            
    }

    if (CheatManager::loaded())
    {
        buttonCheats->text("Cheats");
        buttonCheats->setColors(COLOR_GREY_DARKER, COLOR_WHITE);
    }
    else
    {
        buttonCheats->text("Loading...");
        buttonCheats->setColors(COLOR_GREY_DARKER, COLOR_GREY_LIGHT);
    }
}

void Gui::updateSelector(void)
{
    if (!g_backupScrollEnabled)
    {
        size_t oldindex = hid->index();
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
        if (hid->index() != oldindex)
        {
            Gui::setPKSMBridgeFlag(false);
        }
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
    return 128*((i % (rowlen*collen)) / collen) + 4 * (((i % (rowlen*collen)) / collen) + 1);
}

bool Gui::getPKSMBridgeFlag(void)
{
    return Configuration::getInstance().isPKSMBridgeEnabled() ? PKSMBridge : false;
}

void Gui::setPKSMBridgeFlag(bool f)
{
    PKSMBridge = f;
    Gui::updateButtons();
}
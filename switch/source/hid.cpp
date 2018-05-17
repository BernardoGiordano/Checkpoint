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

#include "hid.hpp"

static entryType_t type;

static size_t refreshMaxEntries(int page, size_t entries)
{
    return (Gui::count(type) - page*entries) > entries ? entries - 1 : Gui::count(type) - page*entries - 1;
}

static void page_back(int& page, int maxpages)
{
    if (page > 0) 
    {
        page--;
    }
    else if (page == 0)
    {
        page = maxpages - 1;
    }
}

static void page_forward(int& page, int maxpages)
{
    if (page < maxpages - 1)
    {
        page++;
    }
    else if (page == maxpages - 1)
    {		
        page = 0;
    }
}

entryType_t hid::entryType(void)
{
    return type;
}

void hid::entryType(entryType_t v)
{
    type = v;	
}

void hid::index(size_t& currentEntry, int& page, size_t maxpages, size_t maxentries, const size_t entries, const size_t columns) {
    maxentries--;

    u64 kHeld = hidKeysHeld(CONTROLLER_P1_AUTO);
    u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
    bool sleep = false;
    
    if (kDown & KEY_ZL)
    {
        page_back(page, maxpages);
        if (currentEntry > refreshMaxEntries(page, entries))
        {
            currentEntry = refreshMaxEntries(page, entries);
        }
    }
    else if (kDown & KEY_ZR)
    {
        page_forward(page, maxpages);
        if (currentEntry > refreshMaxEntries(page, entries))
        {
            currentEntry = refreshMaxEntries(page, entries);
        }
    }
    else if (columns > 1)
    {
        if (kHeld & KEY_LEFT)
        {
            if (currentEntry > 0) 
            {
                currentEntry--;
            }
            else if (currentEntry == 0)
            {
                page_back(page, maxpages);
                currentEntry = refreshMaxEntries(page, entries);
            }
            sleep = true;
        }
        else if (kHeld & KEY_RIGHT)
        {
            if (currentEntry < maxentries)
            {
                currentEntry++;
            }
            else if (currentEntry == maxentries)
            {
                page_forward(page, maxpages);
                currentEntry = 0;
            }
            sleep = true;
        }
        else if (kHeld & KEY_UP)
        {
            if (currentEntry <= columns - 1)
            {
                page_back(page, maxpages);
                if (currentEntry > refreshMaxEntries(page, entries))
                {
                    currentEntry = refreshMaxEntries(page, entries);
                }
            }
            else if (currentEntry >= columns)
            {			
                currentEntry -= columns;
            }
            sleep = true;
        }
        else if (kHeld & KEY_DOWN)
        {
            if ((int)(maxentries - columns) >= 0)
            {
                if (currentEntry <= maxentries - columns)
                {
                    currentEntry += columns;
                }
                else if (currentEntry >= maxentries - columns + 1)
                {
                    page_forward(page, maxpages);
                    if (currentEntry > refreshMaxEntries(page, entries))
                    {
                        currentEntry = refreshMaxEntries(page, entries);
                    }
                }
                sleep = true;
            }
        }
    }
    else
    {
        if (kHeld & KEY_UP)
        {
            if (currentEntry > 0) 
            {
                currentEntry--;
            }
            else if (currentEntry == 0)
            {
                page_back(page, maxpages);
                currentEntry = refreshMaxEntries(page, entries);
            }
            sleep = true;
        }
        else if (kHeld & KEY_DOWN)
        {
            if (currentEntry < maxentries)
            {
                currentEntry++;
            }
            else if (currentEntry == maxentries)
            {
                page_forward(page, maxpages);
                currentEntry = 0;
            }
            sleep = true;
        }		
    }

    if (sleep)
    {
        svcSleepThread(FASTSCROLL_WAIT);
    }
}
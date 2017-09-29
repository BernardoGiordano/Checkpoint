/*  This file is part of Checkpoint
>	Copyright (C) 2017 Bernardo Giordano
>
>   This program is free software: you can redistribute it and/or modify
>   it under the terms of the GNU General Public License as published by
>   the Free Software Foundation, either version 3 of the License, or
>   (at your option) any later version.
>
>   This program is distributed in the hope that it will be useful,
>   but WITHOUT ANY WARRANTY; without even the implied warranty of
>   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
>   GNU General Public License for more details.
>
>   You should have received a copy of the GNU General Public License
>   along with this program.  If not, see <http://www.gnu.org/licenses/>.
>   See LICENSE for information.
*/

#include "hid.h"

static entryType type;

entryType getEntryType(void)
{
	return type;
}

void setEntryType(entryType type_)
{
	type = type_;
}

static size_t refreshMaxEntries(int page, size_t entries)
{
	switch (type)
	{
		case TITLES: return (getTitlesCount() - page*entries) > entries ? entries - 1 : getTitlesCount() - page*entries - 1;
		case CELLS: return (getCellsCount() - page*entries) > entries ? entries - 1 : getCellsCount() - page*entries - 1;
	}
	
	return 0;
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
	if ((size_t)page < maxpages - 1)
	{
		page++;
	}
	else if ((size_t)page == maxpages - 1)
	{		
		page = 0;
	}
}

void calculateIndex(size_t &currentEntry, int &page, size_t maxpages, size_t maxentries, const size_t entries, const size_t columns) {
	maxentries--;
	
	if (hidKeysDown() & KEY_L)
	{
		page_back(page, maxpages);
		if (currentEntry > refreshMaxEntries(page, entries))
		{
			currentEntry = refreshMaxEntries(page, entries);
		}
	}
	
	if (hidKeysDown() & KEY_R)
	{
		page_forward(page, maxpages);
		if (currentEntry > refreshMaxEntries(page, entries))
		{
			currentEntry = refreshMaxEntries(page, entries);
		}
	}
	
	if (columns > 1)
	{
		if (hidKeysDown() & KEY_LEFT)
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
		}
		
		if (hidKeysDown() & KEY_RIGHT)
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
		}
		
		if (hidKeysDown() & KEY_UP)
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
		}
		
		if (hidKeysDown() & KEY_DOWN)
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
		}
	}
	else
	{
		if (hidKeysDown() & KEY_UP)
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
		}
		
		if (hidKeysDown() & KEY_DOWN)
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
		}		
	}
}
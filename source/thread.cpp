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

#include "thread.h"

static std::vector<Thread> threads;

static volatile bool isLoadingTitles = false;

void createThread(ThreadFunc entrypoint)
{
	s32 prio = 0;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	Thread thread = threadCreate((ThreadFunc)entrypoint, NULL, 4*1024, prio-1, -2, false);
	threads.push_back(thread);
}

void destroyThreads(void)
{
	for (u32 i = 0; i < threads.size(); i++)
	{
		threadJoin(threads.at(i), U64_MAX);
		threadFree(threads.at(i));
	}
}

void threadLoadTitles(void)
{
	// don't load titles while they're loading
	if (isLoadingTitles)
	{
		return;
	}

	isLoadingTitles = true;
	loadTitles();
	isLoadingTitles = false;
}
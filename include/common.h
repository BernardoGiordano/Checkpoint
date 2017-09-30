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

#ifndef COMMON_H
#define COMMON_H

#include <3ds.h>
#include <algorithm>
#include <codecvt>
#include <locale>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <vector>
#include "archive.h"
#include "clickable.h"
#include "datetime.h"
#include "directory.h"
#include "error.h"
#include "fsstream.h"
#include "gui.h"
#include "hid.h"
#include "info.h"
#include "../source/pp2d/pp2d/pp2d.h"
#include "scrollable.h"
#include "smdh.h"
#include "stringutil.h"
#include "swkbd.h"
#include "thread.h"
#include "title.h"
#include "util.h"

void createInfo(std::string title, std::string message);
void createError(Result res, std::string message);

#endif
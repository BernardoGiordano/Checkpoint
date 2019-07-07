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

#ifndef GUI_HPP
#define GUI_HPP

#include "colors.hpp"
#include "main.hpp"
#include "sprites.h"
#include "title.hpp"
#include "util.hpp"
#include <algorithm>
#include <citro2d.h>
#include <codecvt>
#include <locale>
#include <string>
#include <vector>

inline C3D_RenderTarget* g_top;
inline C3D_RenderTarget* g_bottom;

inline C2D_SpriteSheet spritesheet;
inline C2D_Image flag;
inline C2D_Sprite checkbox, star;

namespace Gui {
    void init(void);
    void exit(void);
    void frameEnd(void);

    C2D_Image TWLIcon(void);
    C2D_Image noIcon(void);
}

#endif
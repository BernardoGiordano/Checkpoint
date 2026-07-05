/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
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

#ifndef GFXUTILS_HPP
#define GFXUTILS_HPP

#include "gfx.hpp"
#include <string>

// Backend-agnostic drawing helpers built on top of Gfx::*; see gfxutils.cpp.
void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, Color color);
void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, Color color);
std::string trimToFit(const std::string& text, u32 maxsize, size_t textsize, FontFamily family = FontFamily::Sans);

#endif

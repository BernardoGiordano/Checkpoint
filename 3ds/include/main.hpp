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

#ifndef MAIN_HPP
#define MAIN_HPP

#include "Screen.hpp"
#include "logger.hpp"
#include <citro2d.h>
#include <memory>
#include <vector>

inline std::shared_ptr<Screen> g_screen = nullptr;
inline bool g_bottomScrollEnabled       = false;
inline float g_timer                    = 0;
inline std::string g_selectedCheatKey;
inline std::vector<std::string> g_selectedCheatCodes;
inline volatile bool g_isLoadingTitles = false;

inline std::u16string g_currentFile;
inline bool g_isTransferringFile = false;
static inline constexpr Tex3DS_SubTexture dsIconSubt3x = {32, 32, 0.0f, 1.0f, 1.0f, 0.0f};
inline C2D_Image dsIcon                         = {nullptr, &dsIconSubt3x};


#endif
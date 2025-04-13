/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#ifndef SDLHELPER_HPP
#define SDLHELPER_HPP

#include "SDL_FontCache.h"
#include "colors.hpp"
#include "logging.hpp"
#include "main.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <switch.h>
#include <unordered_map>

bool SDLH_Init(void);
void SDLH_Exit(void);

void SDLH_ClearScreen(SDL_Color color);
void SDLH_DrawRect(int x, int y, int w, int h, SDL_Color color);
void SDLH_DrawText(int size, int x, int y, SDL_Color color, const char* text);
void SDLH_LoadImage(SDL_Texture** texture, char* path);
void SDLH_LoadImage(SDL_Texture** texture, u8* buff, size_t size);
void SDLH_DrawImage(SDL_Texture* texture, int x, int y);
void SDLH_DrawImageScale(SDL_Texture* texture, int x, int y, int w, int h);
void SDLH_DrawIcon(std::string icon, int x, int y);
void SDLH_GetTextDimensions(int size, const char* text, u32* w, u32* h);
void SDLH_DrawTextBox(int size, int x, int y, SDL_Color color, int max, const char* text);
void SDLH_Render(void);

void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color);
void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color);
std::string trimToFit(const std::string& text, u32 maxsize, size_t textsize);

#endif
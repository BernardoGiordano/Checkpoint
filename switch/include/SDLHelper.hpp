/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <string>
#include <unordered_map>
#include <memory>

#include <switch.h>

struct WindowDeleter {
    void operator()(SDL_Window* w)
    {
        SDL_DestroyWindow(w);
    }
};
struct RenderDeleter {
    void operator()(SDL_Renderer* w)
    {
        SDL_DestroyRenderer(w);
    }
};
struct TexDeleter {
    void operator()(SDL_Texture* w)
    {
        SDL_DestroyTexture(w);
    }
};
struct FontDeleter {
    void operator()(FC_Font* f)
    {
        FC_FreeFont(f);
    }
};

using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
using RenderPtr = std::unique_ptr<SDL_Renderer, RenderDeleter>;
using TexPtr = std::unique_ptr<SDL_Texture, TexDeleter>;
using FontPtr = std::unique_ptr<FC_Font, FontDeleter>;

struct SDLH {
    SDLH(u32& username_dotsize);
    ~SDLH();

    bool ready;

    void clearScreen(SDL_Color color);
    void drawRect(int x, int y, int w, int h, SDL_Color color);
    void drawText(int size, int x, int y, SDL_Color color, const char* text);
    void drawTextBox(int size, int x, int y, SDL_Color color, int max, const char* text);
    void loadImage(TexPtr& texture, const char* path);
    void loadImage(TexPtr& texture, u8* buff, size_t size);
    void drawImage(SDL_Texture* texture, int x, int y);
    void drawImageScale(SDL_Texture* texture, int x, int y, int w, int h);
    void drawIcon(const std::string& icon, int x, int y);
    void getTextDimensions(int size, const char* text, u32* w, u32* h);
    void render();

    void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color);
    void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color);
    std::string trimToFit(const std::string& text, u32 maxsize, size_t textsize);

private:
    WindowPtr s_window;
    RenderPtr s_renderer;
    TexPtr s_star, s_checkbox;
    using FontHolder = std::unordered_map<int, FontPtr>;
    FontHolder s_fonts;

    FC_Font* getFontFromMap(int size);
};

#endif
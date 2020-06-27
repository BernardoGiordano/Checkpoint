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

#include "SDLHelper.hpp"
#include "logger.hpp"

struct SurfaceDeleter {
    void operator()(SDL_Surface* s)
    {
        SDL_FreeSurface(s);
    }
};

using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

namespace {
    PlFontData fontData, fontExtData;
    float g_currentTime;
}

SDLH::SDLH(u32& username_dotsize) : ready(false)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        Logger::log(Logger::ERROR, "SDL_Init: %s.", SDL_GetError());
        return;
    }
    s_window.reset(SDL_CreateWindow("Checkpoint", 0, 0, 1280, 720, SDL_WINDOW_FULLSCREEN));
    if (!s_window) {
        Logger::log(Logger::ERROR, "SDL_CreateWindow: %s.", SDL_GetError());
        return;
    }
    s_renderer.reset(SDL_CreateRenderer(s_window, 0, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!s_renderer) {
        Logger::log(Logger::ERROR, "SDL_CreateRenderer: %s.", SDL_GetError());
        return;
    }
    SDL_SetRenderDrawBlendMode(s_renderer.get(), SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

    const int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        Logger::log(Logger::ERROR, "IMG_Init: %s.", IMG_GetError());
        return false;
    }
    LoadImage(&s_star, "romfs:/star.png");
    LoadImage(&s_checkbox, "romfs:/checkbox.png");
    SDL_SetTextureColorMod(s_checkbox, theme().c1.r, theme().c1.g, theme().c1.b);

    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);
    plGetSharedFontByType(&fontExtData, PlSharedFontType_NintendoExt);

    // utils
    GetTextDimensions(13, "...", &username_dotsize, NULL);

    ready = true;
}

SDLH::~SDLH()
{
    s_fonts.~FontHolder();
    s_checkbox = nullptr;
    s_star = nullptr;
    s_renderer = nullptr;
    s_window = nullptr;

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

void SDLH::clearScreen(SDL_Color color)
{
    SDL_SetRenderDrawColor(s_renderer.get(), color.r, color.g, color.b, color.a);
    SDL_RenderClear(s_renderer.get());
}
void SDLH::drawRect(int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_SetRenderDrawColor(s_renderer.get(), color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(s_renderer.get(), &rect);
}

void SDLH::drawText(int size, int x, int y, SDL_Color color, const char* text)
{
    FC_DrawColor(getFontFromMap(size), s_renderer.get(), x, y, color, text);
}
void SDLH::drawTextBox(int size, int x, int y, SDL_Color color, int max, const char* text)
{
    u32 h;
    FC_Font* font = getFontFromMap(size);
    GetTextDimensions(size, text, NULL, &h);
    FC_Rect rect = FC_MakeRect(x, y, max, h);
    FC_DrawBoxColor(font, s_renderer.get(), rect, color, text);
}

void SDLH::loadImage(TexPtr& texture, const char* path)
{
    SurfacePtr loaded_surface(IMG_Load(path));

    if (loaded_surface) {
        Uint32 colorkey = SDL_MapRGB(loaded_surface->format, 0, 0, 0);
        SDL_SetColorKey(loaded_surface.get(), SDL_TRUE, colorkey);
        texture.reset(SDL_CreateTextureFromSurface(s_renderer.get(), loaded_surface.get()));
    }
}
void SDLH::loadImage(TexPtr& texture, u8* buff, size_t size)
{
    SurfacePtr loaded_surface(IMG_Load_RW(SDL_RWFromMem(buff, size), 1));

    if (loaded_surface) {
        Uint32 colorkey = SDL_MapRGB(loaded_surface->format, 0, 0, 0);
        SDL_SetColorKey(loaded_surface.get(), SDL_TRUE, colorkey);
        texture.reset(SDL_CreateTextureFromSurface(s_renderer.get(), loaded_surface.get()));
    }
}

void SDLH::drawImage(SDL_Texture* texture, int x, int y)
{
    SDL_Rect position;
    position.x = x;
    position.y = y;
    SDL_QueryTexture(texture, nullptr, nullptr, &position.w, &position.h);
    SDL_RenderCopy(s_renderer.get(), texture, nullptr, &position);
}
void SDLH::drawImageScale(SDL_Texture* texture, int x, int y, int w, int h)
{
    SDL_Rect position;
    position.x = x;
    position.y = y;
    position.w = w;
    position.h = h;
    SDL_RenderCopy(s_renderer.get(), texture, nullptr, &position);
}
void SDLH::drawIcon(const std::string& icon, int x, int y);
void SDLH::getTextDimensions(int size, const char* text, u32* w, u32* h)
{
    FC_Font* f = getFontFromMap(size);
    if (w)
        *w = FC_GetWidth(f, text);
    if (h)
        *h = FC_GetHeight(f, text);
}

void SDLH::render()
{
    g_currentTime = SDL_GetTicks() / 1000.f;
    SDL_RenderPresent(s_renderer.get());
}

void SDLH::drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color)
{
    SDLH_DrawRect(x - size, y - size, w + 2 * size, size, color); // top
    SDLH_DrawRect(x - size, y, size, h, color);                   // left
    SDLH_DrawRect(x + w, y, size, h, color);                      // right
    SDLH_DrawRect(x - size, y + h, w + 2 * size, size, color);    // bottom
}
void SDLH::drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color)
{
    const float highlight_multiplier = fmax(0.0, fabs(fmod(g_currentTime, 1.0) - 0.5) / 0.5);
    color                      = FC_MakeColor(color.r + (255 - color.r) * highlight_multiplier, color.g + (255 - color.g) * highlight_multiplier,
        color.b + (255 - color.b) * highlight_multiplier, 255);
    drawOutline(x, y, w, h, size, color);
}
std::string trimToFit(const std::string& text, u32 maxsize, size_t textsize)
{
    u32 width;
    std::string newtext = "";
    for (const auto c : text) {
        GetTextDimensions(textsize, newtext.c_str(), &width, nullptr);
        if (width < maxsize) {
            newtext += c;
        }
        else {
            newtext += "...";
            break;
        }
    }
    return newtext;
}

FC_Font* SDLH::getFontFromMap(int size)
{
    if(auto it = s_fonts.find(size); it == s_fonts.end())
    {
        FC_Font* f = FC_CreateFont();
        FC_LoadFont_RW(f, s_renderer.get(), SDL_RWFromMem((void*)fontData.address, fontData.size),
            SDL_RWFromMem((void*)fontExtData.address, fontExtData.size), 1, size, COLOR_BLACK, TTF_STYLE_NORMAL);
        s_fonts.try_emplace(size, f);
        return f;
    }
    else
    {
        return it->second.get();
    }
}

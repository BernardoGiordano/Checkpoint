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

#include "SDLHelper.hpp"

static SDL_Window* s_window;
static SDL_Renderer* s_renderer;
static SDL_Texture* s_star;
static SDL_Texture* s_checkbox;

static PlFontData fontData, fontExtData;

struct FallbackFontData {
    void* address;
    size_t size;
};
static FallbackFontData s_fallbackData[4];
static int s_numFallbacks = 0;
static std::unordered_map<int, FC_Font*> s_fonts;

// Bundled Space Mono (OFL), read once into memory at init. Backup names,
// title IDs and paths draw through this instead of the shared system font,
// which has no monospace variant. Left null (falls back to Sans) if the
// romfs asset failed to load.
struct MonoFontData {
    void* address = nullptr;
    size_t size   = 0;
};
static MonoFontData s_monoData;
static std::unordered_map<int, FC_Font*> s_monoFonts;

// Every call site asks for a size in the redesign's type scale; we
// render each one a touch larger for legibility on a handheld screen. Because
// both drawing and measurement (SDLH_GetTextDimensions) resolve through the
// same enlarged FC_Font, layouts that centre/measure text stay consistent —
// only the glyphs grow. Keyed by the requested size, so the scale is invisible
// to callers.
static int scaledFontPx(int size)
{
    return (size * 6 + 2) / 5; // ~1.2x, rounded
}

static FC_Font* getFontFromMap(int size)
{
    std::unordered_map<int, FC_Font*>::const_iterator got = s_fonts.find(size);
    if (got == s_fonts.end() || got->second == NULL) {
        const int px = scaledFontPx(size);
        FC_Font* f   = FC_CreateFont();
        FC_LoadFont_RW(f, s_renderer, SDL_RWFromMem((void*)fontData.address, fontData.size),
            SDL_RWFromMem((void*)fontExtData.address, fontExtData.size), 1, px, COLOR_BLACK, TTF_STYLE_NORMAL);
        // Register CJK/Korean fallback fonts for this size
        for (int i = 0; i < s_numFallbacks; i++) {
            TTF_Font* fallback = TTF_OpenFontRW(SDL_RWFromMem(s_fallbackData[i].address, s_fallbackData[i].size), 1, px);
            if (fallback != NULL) {
                FC_AddFallbackFont(f, fallback);
            }
        }
        s_fonts.insert({size, f});
        return f;
    }
    return got->second;
}

static FC_Font* getMonoFontFromMap(int size)
{
    if (s_monoData.address == NULL) {
        return getFontFromMap(size); // romfs asset missing: degrade to Sans rather than draw nothing
    }

    std::unordered_map<int, FC_Font*>::const_iterator got = s_monoFonts.find(size);
    if (got == s_monoFonts.end() || got->second == NULL) {
        FC_Font* f = FC_CreateFont();
        // FC_LoadFont_RW requires a valid "ext" font too; Space Mono has no
        // separate glyph-extension file, so it is passed as its own ext.
        FC_LoadFont_RW(f, s_renderer, SDL_RWFromMem(s_monoData.address, s_monoData.size), SDL_RWFromMem(s_monoData.address, s_monoData.size), 1,
            scaledFontPx(size), COLOR_BLACK, TTF_STYLE_NORMAL);
        s_monoFonts.insert({size, f});
        return f;
    }
    return got->second;
}

static FC_Font* fontFor(int size, FontFamily family)
{
    return family == FontFamily::Mono ? getMonoFontFromMap(size) : getFontFromMap(size);
}

bool SDLH_Init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        Logging::error("SDL_Init: {}.", SDL_GetError());
        return false;
    }
    s_window = SDL_CreateWindow("Checkpoint", 0, 0, 1280, 720, SDL_WINDOW_FULLSCREEN);
    if (!s_window) {
        Logging::error("SDL_CreateWindow: {}.", SDL_GetError());
        return false;
    }
    s_renderer = SDL_CreateRenderer(s_window, 0, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        Logging::error("SDL_CreateRenderer: {}.", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

    const int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        Logging::error("IMG_Init: {}.", IMG_GetError());
        return false;
    }
    SDLH_LoadImage(&s_star, "romfs:/star.png");
    SDLH_LoadImage(&s_checkbox, "romfs:/checkbox.png");
    // The multi-select badge is accent-filled with a white check on top (new
    // design); the checkbox asset itself is a black-on-transparent checkmark.
    SDL_SetTextureColorMod(s_checkbox, COLOR_WHITE.r, COLOR_WHITE.g, COLOR_WHITE.b);

    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);
    plGetSharedFontByType(&fontExtData, PlSharedFontType_NintendoExt);

    // Bundled monospace font (romfs, not a shared-font handle): read the whole
    // file into memory once; getMonoFontFromMap() re-wraps it in a fresh
    // SDL_RWops per size, the same pattern the shared font uses.
    FILE* monoFile = fopen("romfs:/fonts/SpaceMono-Regular.ttf", "rb");
    if (monoFile != NULL) {
        fseek(monoFile, 0, SEEK_END);
        long monoSize = ftell(monoFile);
        fseek(monoFile, 0, SEEK_SET);
        if (monoSize > 0) {
            void* buf = malloc((size_t)monoSize);
            if (buf != NULL && fread(buf, 1, (size_t)monoSize, monoFile) == (size_t)monoSize) {
                s_monoData.address = buf;
                s_monoData.size    = (size_t)monoSize;
            }
            else {
                free(buf);
            }
        }
        fclose(monoFile);
    }
    if (s_monoData.address == NULL) {
        Logging::error("Failed to load romfs:/fonts/SpaceMono-Regular.ttf, falling back to the system font.");
    }

    // Load CJK/Korean fallback fonts
    static const PlSharedFontType fallbackTypes[] = {
        PlSharedFontType_KO,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional,
    };
    s_numFallbacks = 0;
    for (size_t i = 0; i < sizeof(fallbackTypes) / sizeof(fallbackTypes[0]); i++) {
        PlFontData fd;
        if (R_SUCCEEDED(plGetSharedFontByType(&fd, fallbackTypes[i])) && fd.address != NULL) {
            s_fallbackData[s_numFallbacks].address = fd.address;
            s_fallbackData[s_numFallbacks].size    = fd.size;
            s_numFallbacks++;
        }
    }

    // utils
    SDLH_GetTextDimensions(13, "...", &g_username_dotsize, NULL);

    return true;
}

void SDLH_Exit(void)
{
    for (auto& value : s_fonts) {
        FC_FreeFont(value.second);
    }
    for (auto& value : s_monoFonts) {
        FC_FreeFont(value.second);
    }
    free(s_monoData.address);

    TTF_Quit();
    SDL_DestroyTexture(s_star);
    SDL_DestroyTexture(s_checkbox);
    IMG_Quit();
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
}

void SDLH_ClearScreen(SDL_Color color)
{
    SDL_SetRenderDrawColor(s_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(s_renderer);
}

void SDLH_Render(void)
{
    g_currentTime = SDL_GetTicks() / 1000.f;
    SDL_RenderPresent(s_renderer);
}

void SDLH_DrawRect(int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    SDL_SetRenderDrawColor(s_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(s_renderer, &rect);
}

void SDLH_DrawText(int size, int x, int y, SDL_Color color, const char* text, FontFamily family)
{
    FC_DrawColor(fontFor(size, family), s_renderer, x, y, color, "%s", text);
}

void SDLH_DrawTextBox(int size, int x, int y, SDL_Color color, int max, const char* text, FontFamily family)
{
    u32 h;
    FC_Font* font = fontFor(size, family);
    SDLH_GetTextDimensions(size, text, NULL, &h, family);
    FC_Rect rect = FC_MakeRect(x, y, max, h);
    FC_DrawBoxColor(font, s_renderer, rect, color, "%s", text);
}

void SDLH_LoadImage(SDL_Texture** texture, char* path)
{
    SDL_Surface* loaded_surface = NULL;
    loaded_surface              = IMG_Load(path);

    if (loaded_surface) {
        Uint32 colorkey = SDL_MapRGB(loaded_surface->format, 0, 0, 0);
        SDL_SetColorKey(loaded_surface, SDL_TRUE, colorkey);
        *texture = SDL_CreateTextureFromSurface(s_renderer, loaded_surface);
    }

    SDL_FreeSurface(loaded_surface);
}

void SDLH_LoadImage(SDL_Texture** texture, u8* buff, size_t size)
{
    SDL_Surface* loaded_surface = NULL;
    loaded_surface              = IMG_Load_RW(SDL_RWFromMem(buff, size), 1);

    if (loaded_surface) {
        Uint32 colorkey = SDL_MapRGB(loaded_surface->format, 0, 0, 0);
        SDL_SetColorKey(loaded_surface, SDL_TRUE, colorkey);
        *texture = SDL_CreateTextureFromSurface(s_renderer, loaded_surface);
    }

    SDL_FreeSurface(loaded_surface);
}

void SDLH_DrawImage(SDL_Texture* texture, int x, int y)
{
    SDL_Rect position;
    position.x = x;
    position.y = y;
    SDL_QueryTexture(texture, NULL, NULL, &position.w, &position.h);
    SDL_RenderCopy(s_renderer, texture, NULL, &position);
}

void SDLH_DrawImageScale(SDL_Texture* texture, int x, int y, int w, int h)
{
    SDL_Rect position;
    position.x = x;
    position.y = y;
    position.w = w;
    position.h = h;
    SDL_RenderCopy(s_renderer, texture, NULL, &position);
}

void SDLH_GetTextDimensions(int size, const char* text, u32* w, u32* h, FontFamily family)
{
    FC_Font* f = fontFor(size, family);
    if (w != NULL)
        *w = FC_GetWidth(f, "%s", text);
    if (h != NULL)
        *h = FC_GetHeight(f, "%s", text);
}

SDL_Texture* SDLH_StarTexture(void)
{
    return s_star;
}

SDL_Texture* SDLH_CheckboxTexture(void)
{
    return s_checkbox;
}

void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color)
{
    SDLH_DrawRect(x - size, y - size, w + 2 * size, size, color); // top
    SDLH_DrawRect(x - size, y, size, h, color);                   // left
    SDLH_DrawRect(x + w, y, size, h, color);                      // right
    SDLH_DrawRect(x - size, y + h, w + 2 * size, size, color);    // bottom
}

void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, SDL_Color color)
{
    float highlight_multiplier = fmax(0.0, fabs(fmod(g_currentTime, 1.0) - 0.5) / 0.5);
    color                      = FC_MakeColor(color.r + (255 - color.r) * highlight_multiplier, color.g + (255 - color.g) * highlight_multiplier,
                             color.b + (255 - color.b) * highlight_multiplier, 255);
    drawOutline(x, y, w, h, size, color);
}

std::string trimToFit(const std::string& text, u32 maxsize, size_t textsize, FontFamily family)
{
    u32 width;
    std::string newtext = "";
    const char* src     = text.c_str();
    while (*src != '\0') {
        int charsize = U8_charsize(src);
        if (charsize < 1)
            break;
        std::string candidate = newtext + std::string(src, charsize);
        SDLH_GetTextDimensions(textsize, candidate.c_str(), &width, NULL, family);
        if (width >= maxsize) {
            newtext += "...";
            break;
        }
        newtext = candidate;
        src += charsize;
    }
    return newtext;
}

void SDLH_CreateColorTexture(SDL_Texture** texture, int w, int h, SDL_Color color)
{
    SDL_Surface* surface = SDL_CreateRGBSurface(0, w, h, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (surface) {
        SDL_FillRect(surface, NULL, SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a));
        *texture = SDL_CreateTextureFromSurface(s_renderer, surface);
        SDL_FreeSurface(surface);
    }
}
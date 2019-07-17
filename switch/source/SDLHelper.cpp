#include "SDLHelper.hpp"

static SDL_Window* s_window;
static SDL_Renderer* s_renderer;
static SDL_Texture* s_star;
static SDL_Texture* s_checkbox;

static PlFontData fontData, fontExtData;
static std::unordered_map<int, FC_Font*> s_fonts;

static FC_Font* getFontFromMap(int size)
{
    std::unordered_map<int, FC_Font*>::const_iterator got = s_fonts.find(size);
    if (got == s_fonts.end() || got->second == NULL) {
        FC_Font* f = FC_CreateFont();
        FC_LoadFont_RW(f, s_renderer, SDL_RWFromMem((void*)fontData.address, fontData.size),
            SDL_RWFromMem((void*)fontExtData.address, fontExtData.size), 1, size, COLOR_BLACK, TTF_STYLE_NORMAL);
        s_fonts.insert({size, f});
        return f;
    }
    return got->second;
}

bool SDLH_Init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        Logger::getInstance().error("SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    s_window = SDL_CreateWindow("Checkpoint", 0, 0, 1280, 720, SDL_WINDOW_FULLSCREEN);
    if (!s_window) {
        Logger::getInstance().error("SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    s_renderer = SDL_CreateRenderer(s_window, 0, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        Logger::getInstance().error("SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

    const int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags) {
        Logger::getInstance().error("IMG_Init: %s\n", IMG_GetError());
        return false;
    }
    SDLH_LoadImage(&s_star, "romfs:/star.png");
    SDLH_LoadImage(&s_checkbox, "romfs:/checkbox.png");
    SDL_SetTextureColorMod(s_checkbox, theme().c1.r, theme().c1.g, theme().c1.b);

    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);
    plGetSharedFontByType(&fontExtData, PlSharedFontType_NintendoExt);

    return true;
}

void SDLH_Exit(void)
{
    for (auto& value : s_fonts) {
        FC_FreeFont(value.second);
    }

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

void SDLH_DrawText(int size, int x, int y, SDL_Color color, const char* text)
{
    FC_DrawColor(getFontFromMap(size), s_renderer, x, y, color, text);
}

void SDLH_DrawTextBox(int size, int x, int y, SDL_Color color, int max, const char* text)
{
    u32 h;
    FC_Font* font = getFontFromMap(size);
    SDLH_GetTextDimensions(size, text, NULL, &h);
    FC_Rect rect = FC_MakeRect(x, y, max, h);
    FC_DrawBoxColor(font, s_renderer, rect, color, text);
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

void SDLH_GetTextDimensions(int size, const char* text, u32* w, u32* h)
{
    FC_Font* f = getFontFromMap(size);
    if (w != NULL)
        *w = FC_GetWidth(f, text);
    if (h != NULL)
        *h = FC_GetHeight(f, text);
}

void SDLH_DrawIcon(std::string icon, int x, int y)
{
    SDL_Texture* t = nullptr;
    if (icon.compare("checkbox") == 0) {
        t = s_checkbox;
        SDLH_DrawRect(x + 8, y + 8, 24, 24, theme().c6);
    }
    else if (icon.compare("star") == 0) {
        t = s_star;
    }

    if (t != nullptr) {
        SDLH_DrawImage(t, x, y);
    }
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
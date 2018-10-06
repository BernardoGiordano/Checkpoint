#include "SDLHelper.hpp"

static SDL_Window* s_window;
static SDL_Renderer* s_renderer;
static SDL_Texture* s_star;
static SDL_Texture* s_checkbox;
static SDL_Texture* s_flag;
static FC_Font* font4;
static FC_Font* font5;
static FC_Font* font6;
static FC_Font* font7;

bool SDLHelperInit(void)
{
    bool ok = false;
    SDL_Init(SDL_INIT_EVERYTHING);
    s_window = SDL_CreateWindow("Checkpoint", 0, 0, 1280, 720, SDL_WINDOW_FULLSCREEN);
    s_renderer = SDL_CreateRenderer(s_window, 0, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    TTF_Init();
    u64 languageCode = 0;
    PlFontData fontData;
    PlFontData fonts[PlSharedFontType_Total];
    size_t total_fonts = 0;
    plGetSharedFont(languageCode, fonts, PlSharedFontType_Total, &total_fonts);
    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);

    font4 = FC_CreateFont();
    font5 = FC_CreateFont();
    font6 = FC_CreateFont();
    font7 = FC_CreateFont();

    FC_LoadFont_RW(font4, s_renderer, SDL_RWFromMem((void*)fontData.address, fontData.size), 1, 20, FC_MakeColor(0, 0, 0, 255), TTF_STYLE_NORMAL);
    FC_LoadFont_RW(font5, s_renderer, SDL_RWFromMem((void*)fontData.address, fontData.size), 1, 25, FC_MakeColor(0, 0, 0, 255), TTF_STYLE_NORMAL);
    FC_LoadFont_RW(font6, s_renderer, SDL_RWFromMem((void*)fontData.address, fontData.size), 1, 30, FC_MakeColor(0, 0, 0, 255), TTF_STYLE_NORMAL);
    FC_LoadFont_RW(font7, s_renderer, SDL_RWFromMem((void*)fontData.address, fontData.size), 1, 40, FC_MakeColor(0, 0, 0, 255), TTF_STYLE_NORMAL);

    SDL_LoadImage(&s_flag, "romfs:/flag.png");
    SDL_LoadImage(&s_star, "romfs:/star.png");
    SDL_LoadImage(&s_checkbox, "romfs:/checkbox.png");
    
    return true;
}

void SDLHelperExit(void)
{
    FC_FreeFont(font4);
    FC_FreeFont(font5);
    FC_FreeFont(font6);
    FC_FreeFont(font7);
    TTF_Quit();
    SDL_DestroyTexture(s_flag);
    SDL_DestroyTexture(s_star);
    SDL_DestroyTexture(s_checkbox);
    IMG_Quit();
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();
}

void SDL_ClearScreen(SDL_Color colour)
{
    SDL_SetRenderDrawColor(s_renderer, colour.r, colour.g, colour.b, colour.a);
    SDL_RenderClear(s_renderer);
}

void SDL_Render(void)
{
    SDL_RenderPresent(s_renderer);
}

void SDL_DrawRect(int x, int y, int w, int h, SDL_Color colour)
{
    SDL_Rect rect;
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    SDL_SetRenderDrawColor(s_renderer, colour.r, colour.g, colour.b, colour.a);
    SDL_RenderFillRect(s_renderer, &rect);
}

void SDL_DrawCircle(int x, int y, int r, SDL_Color colour)
{
    filledCircleRGBA(s_renderer, x, y, r, colour.r, colour.g, colour.b, colour.a);
}

void SDL_DrawText(int size, int x, int y, SDL_Color colour, const char* text)
{
    FC_Font* f = nullptr;
    switch(size) {
        case 4: f = font4; break;
        case 5: f = font5; break;
        case 6: f = font6; break;
        case 7: f = font7; break;
    }
    if (f != nullptr) {
        FC_DrawColor(f, s_renderer, x, y, colour, text);
    }
}

void SDL_LoadImage(SDL_Texture** texture, char* path)
{
    SDL_Surface* loaded_surface = NULL;
    loaded_surface = IMG_Load(path);

    if (loaded_surface)
    {
        Uint32 colorkey = SDL_MapRGB(loaded_surface->format, 0, 0, 0);
        SDL_SetColorKey(loaded_surface, SDL_TRUE, colorkey);
        *texture = SDL_CreateTextureFromSurface(s_renderer, loaded_surface);
    }

    SDL_FreeSurface(loaded_surface);
}

void SDL_LoadImage(SDL_Texture** texture, u8* buff, size_t size)
{
    SDL_Surface* loaded_surface = NULL;
    loaded_surface = IMG_Load_RW(SDL_RWFromMem(buff, size), 1);

    if (loaded_surface)
    {
        Uint32 colorkey = SDL_MapRGB(loaded_surface->format, 0, 0, 0);
        SDL_SetColorKey(loaded_surface, SDL_TRUE, colorkey);
        *texture = SDL_CreateTextureFromSurface(s_renderer, loaded_surface);
    }

    SDL_FreeSurface(loaded_surface);
}

void SDL_DrawImage(SDL_Texture* texture, int x, int y)
{
    SDL_Rect position;
    position.x = x; position.y = y;
    SDL_QueryTexture(texture, NULL, NULL, &position.w, &position.h);
    SDL_RenderCopy(s_renderer, texture, NULL, &position);
}

void SDL_DrawImageScale(SDL_Texture* texture, int x, int y, int w, int h)
{
    SDL_Rect position;
    position.x = x; position.y = y; position.w = w; position.h = h;
    SDL_RenderCopy(s_renderer, texture, NULL, &position);
}

void GetTextDimensions(int size, const char* text, u32* w, u32* h)
{
    FC_Font* f = nullptr;
    switch(size) {
        case 4: f = font4; break;
        case 5: f = font5; break;
        case 6: f = font6; break;
        case 7: f = font7; break;
    }
    if (f != nullptr) {
        if (w != NULL) *w = FC_GetWidth(f, text);
        if (h != NULL) *h = FC_GetHeight(f, text);
    }
}

void DrawIcon(std::string icon, int x, int y)
{
    SDL_Texture* t = nullptr;
    if (icon.compare("flag") == 0) {
        t = s_flag;
    } else if (icon.compare("checkbox") == 0) {
        t = s_checkbox;
    } else if (icon.compare("star") == 0) {
        t = s_star;
    }

    if (t != nullptr) {
        SDL_DrawImage(t, x, y);
    }
}
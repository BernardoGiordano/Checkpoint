#ifndef SDLHELPER_HPP
#define SDLHELPER_HPP

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h> 
#include <SDL2/SDL_image.h>
#include <SDL_ttf.h>
#include <string>
#include "SDL_FontCache.h"

static inline SDL_Color SDL_MakeColour(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	SDL_Color colour = {r, g, b, a};
	return colour;
}

bool SDLHelperInit(void);
void SDLHelperExit(void);

void SDL_ClearScreen(SDL_Color colour);
void SDL_DrawRect(int x, int y, int w, int h, SDL_Color colour);
void SDL_DrawCircle(int x, int y, int r, SDL_Color colour);
void SDL_DrawText(int size, int x, int y, SDL_Color colour, const char *text);
void SDL_LoadImage(SDL_Texture **texture, char *path);
void SDL_LoadImage(SDL_Texture** texture, u8* buff, size_t size);
void SDL_DrawImage(SDL_Texture *texture, int x, int y);
void SDL_DrawImageScale(SDL_Texture *texture, int x, int y, int w, int h);
void DrawIcon(std::string icon, int x, int y);
void GetTextDimensions(int size, const char* text, u32* w, u32* h);
void SDL_Render(void);

#endif
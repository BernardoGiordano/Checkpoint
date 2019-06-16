#ifndef SDLHELPER_HPP
#define SDLHELPER_HPP

#include "SDL_FontCache.h"
#include "colors.hpp"
#include "status.hpp"
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

#endif
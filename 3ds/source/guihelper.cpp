#include "main.hpp"

void drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color)
{
    u8 r = color & 0xFF;
    u8 g = (color >> 8) & 0xFF;
    u8 b = (color >> 16) & 0xFF;
    float highlight_multiplier = fmax(0.0, fabs(fmod(g_timer, 1.0) - 0.5) / 0.5);
    color = C2D_Color32(r + (255 - r) * highlight_multiplier, g + (255 - g) * highlight_multiplier, b + (255 - b) * highlight_multiplier, 255);
    C2D_DrawRectSolid(x - size, y - size, 0.5f, w + 2*size, size, color); // g_top
    C2D_DrawRectSolid(x - size, y, 0.5f, size, h, color); // left
    C2D_DrawRectSolid(x + w, y, 0.5f, size, h, color); // right
    C2D_DrawRectSolid(x - size, y + h, 0.5f, w + 2*size, size, color); // g_bottom
}

void drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color)
{
    C2D_DrawRectSolid(x - size, y - size, 0.5f, w + 2*size, size, color); // g_top
    C2D_DrawRectSolid(x - size, y, 0.5f, size, h, color); // left
    C2D_DrawRectSolid(x + w, y, 0.5f, size, h, color); // right
    C2D_DrawRectSolid(x - size, y + h, 0.5f, w + 2*size, size, color); // g_bottom
}
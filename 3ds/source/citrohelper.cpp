#include "citrohelper.hpp"

CTRH::CTRH() : timer(0.0f)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    staticBuf  = C2D_TextBufNew(512);
    dynamicBuf = C2D_TextBufNew(512);

    spritesheet = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
    wifiSlotIcons[0] = getImage(0);
    wifiSlotIcons[1] = getImage(1);
    wifiSlotIcons[2] = getImage(2);
}

CTRH::~CTRH()
{
    C2D_TextBufDelete(dynamicBuf);
    C2D_TextBufDelete(staticBuf);
    C2D_SpriteSheetFree(spritesheet);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

void CTRH::staticText(C2D_Text* t, const char* msg)
{
    C2D_TextParse(t, staticBuf, msg);
    C2D_TextOptimize(t);
}
void CTRH::dynamicText(C2D_Text* t, const char* msg)
{
    C2D_TextParse(t, dynamicBuf, msg);
    C2D_TextOptimize(t);
}

void CTRH::beginFrame()
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
}

void CTRH::clear(u32 clearColor)
{
    C2D_TextBufClear(dynamicBuf);
    C2D_TargetClear(top, clearColor);
    C2D_TargetClear(bottom, clearColor);
}

void CTRH::targetTop()
{
    C2D_SceneBegin(top);
}
void CTRH::targetBottom()
{
    C2D_SceneBegin(bottom);
}

void CTRH::endFrame()
{
    C3D_FrameEnd(0);
    timer += 0.025f;
}

float CTRH::getTimer() const
{
    return timer;
}

C2D_Image CTRH::getImage(size_t idx) const
{
    return C2D_SpriteSheetGetImage(spritesheet, idx);
}

void CTRH::drawPulsingOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color) const
{
    u8 r                       = color & 0xFF;
    u8 g                       = (color >> 8) & 0xFF;
    u8 b                       = (color >> 16) & 0xFF;
    float highlight_multiplier = fmax(0.0, fabs(fmod(timer, 1.0) - 0.5) / 0.5);
    color = C2D_Color32(r + (255 - r) * highlight_multiplier, g + (255 - g) * highlight_multiplier, b + (255 - b) * highlight_multiplier, 255);
    drawOutline(x, y, w, h, size, color);
}
void CTRH::drawOutline(u32 x, u32 y, u16 w, u16 h, u8 size, u32 color) const
{
    C2D_DrawRectSolid(x - size, y - size, 0.5f, w + 2 * size, size, color); // top
    C2D_DrawRectSolid(x - size, y, 0.5f, size, h, color);                   // left
    C2D_DrawRectSolid(x + w, y, 0.5f, size, h, color);                      // right
    C2D_DrawRectSolid(x - size, y + h, 0.5f, w + 2 * size, size, color);    // bottom
}

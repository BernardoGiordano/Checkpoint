/*
*   Copyright 2017-2018 nx-hbmenu Authors
*
*   Permission to use, copy, modify, and/or distribute this software for any purpose 
*   with or without fee is hereby granted, provided that the above copyright notice 
*   and this permission notice appear in all copies.
*
*   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH 
*   REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND 
*   FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, 
*   OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA
*   OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, 
*   ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "draw.hpp"

static FT_Error s_font_libret = 1;
static FT_Error s_font_facesret[FONT_FACES_MAX];
static FT_Library s_font_library;
static FT_Face s_font_faces[FONT_FACES_MAX];
static FT_Face s_font_lastusedface;
static size_t s_font_faces_total = 0;
static u64 s_textLanguageCode = 0;

Result textInit(void)
{
    Result res = setInitialize();
    if (R_SUCCEEDED(res)) 
    {
        res = setGetSystemLanguage(&s_textLanguageCode);
    }
    setExit();
    return res;
}

static bool FontSetType(u32 scale)
{
    FT_Error ret = 0;
    for (u32 i = 0; i < s_font_faces_total; i++)
    {
        ret = FT_Set_Char_Size(s_font_faces[i], 0, scale*64, 300, 300);
        if (ret) return false;
    }
    return true;
}

static inline bool FontLoadGlyph(glyph_t* glyph, u32 font, u32 codepoint)
{
    FT_Face face;
    FT_Error ret = 0;
    FT_GlyphSlot slot;
    FT_UInt glyph_index;
    FT_Bitmap* bitmap;

    if (s_font_faces_total == 0) return false;

    for (u32 i = 0; i < s_font_faces_total; i++)
    {
        face = s_font_faces[i];
        s_font_lastusedface = face;
        glyph_index = FT_Get_Char_Index(face, codepoint);
        if (glyph_index == 0) continue;

        ret = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
        if (ret == 0)
        {
            ret = FT_Render_Glyph( face->glyph, FT_RENDER_MODE_NORMAL);
        }
        if (ret) return false;

        break;
    }

    slot = face->glyph;
    bitmap = &slot->bitmap;

    glyph->width   = bitmap->width;
    glyph->height  = bitmap->rows;
    glyph->pitch   = bitmap->pitch;
    glyph->data    = bitmap->buffer;
    glyph->advance = slot->advance.x >> 6;
    glyph->posX    = slot->bitmap_left;
    glyph->posY    = slot->bitmap_top;
    return true;
}

static void DrawGlyph(u32 x, u32 y, color_t clr, const glyph_t* glyph)
{
    const u8* data = glyph->data;
    x += glyph->posX;
    y -= glyph->posY;
    for (u32 j = 0; j < glyph->height; j ++)
    {
        for (u32 i = 0; i < glyph->width; i ++)
        {
            clr.a = data[i];
            if (!clr.a) continue;
            DrawPixel(x+i, y+j, clr);
        }
        data += glyph->pitch;
    }
}

static inline u8 DecodeByte(const char** ptr)
{
    u8 c = (u8)**ptr;
    *ptr += 1;
    return c;
}

// UTF-8 code adapted from http://www.json.org/JSON_checker/utf8_decode.c
static inline int8_t DecodeUTF8Cont(const char** ptr)
{
    int c = DecodeByte(ptr);
    return ((c & 0xC0) == 0x80) ? (c & 0x3F) : -1;
}

static inline u32 DecodeUTF8(const char** ptr)
{
    u32 r;
    u8 c;
    int8_t c1, c2, c3;

    c = DecodeByte(ptr);
    if ((c & 0x80) == 0)
        return c;
    if ((c & 0xE0) == 0xC0)
    {
        c1 = DecodeUTF8Cont(ptr);
        if (c1 >= 0)
        {
            r = ((c & 0x1F) << 6) | c1;
            if (r >= 0x80)
                return r;
        }
    } else if ((c & 0xF0) == 0xE0)
    {
        c1 = DecodeUTF8Cont(ptr);
        if (c1 >= 0)
        {
            c2 = DecodeUTF8Cont(ptr);
            if (c2 >= 0)
            {
                r = ((c & 0x0F) << 12) | (c1 << 6) | c2;
                if (r >= 0x800 && (r < 0xD800 || r >= 0xE000))
                    return r;
            }
        }
    } else if ((c & 0xF8) == 0xF0)
    {
        c1 = DecodeUTF8Cont(ptr);
        if (c1 >= 0)
        {
            c2 = DecodeUTF8Cont(ptr);
            if (c2 >= 0)
            {
                c3 = DecodeUTF8Cont(ptr);
                if (c3 >= 0)
                {
                    r = ((c & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3;
                    if (r >= 0x10000 && r < 0x110000)
                        return r;
                }
            }
        }
    }
    return 0xFFFD;
}

static void DrawText_(u32 font, u32 x, u32 y, color_t clr, const char* text, u32 max_width, const char* end_text)
{
    u32 origX = x;
    if (s_font_faces_total == 0) return;
    if (!FontSetType(font)) return;
    s_font_lastusedface = s_font_faces[0];

    while (*text)
    {
        if (max_width && x-origX >= max_width) {
            text = end_text;
            max_width = 0;
        }

        glyph_t glyph;
        u32 codepoint = DecodeUTF8(&text);

        if (codepoint == '\n')
        {
            if (max_width) {
                text = end_text;
                max_width = 0;
                continue;
            }

            x = origX;
            y += s_font_lastusedface->size->metrics.height / 64;
            continue;
        }

        if (!FontLoadGlyph(&glyph, font, codepoint))
        {
            if (!FontLoadGlyph(&glyph, font, '?'))
                continue;
        }

        DrawGlyph(x, y + font*3, clr, &glyph);
        x += glyph.advance;
    }
}

void DrawText(u32 font, u32 x, u32 y, color_t clr, const char* text)
{
    DrawText_(font, x, y, clr, text, 0, NULL);
}

void DrawTextTruncate(u32 font, u32 x, u32 y, color_t clr, const char* text, u32 max_width, const char* end_text)
{
    DrawText_(font, x, y, clr, text, max_width, end_text);
}

void GetTextDimensions(u32 font, const char* text, u32* width_out, u32* height_out)
{
    if (s_font_faces_total == 0) return;
    if (!FontSetType(font)) return;
    s_font_lastusedface = s_font_faces[0];
    u32 x = 0;
    u32 width = 0, height = s_font_lastusedface->size->metrics.height / 64 - font*3;

    while (*text)
    {
        glyph_t glyph;
        u32 codepoint = DecodeUTF8(&text);

        if (codepoint == '\n')
        {
            x = 0;
            height += s_font_lastusedface->size->metrics.height / 64;
            height -= font*3;
            continue;
        }

        if (!FontLoadGlyph(&glyph, font, codepoint))
        {
            if (!FontLoadGlyph(&glyph, font, '?'))
                continue;
        }

        x += glyph.advance;

        if (x > width)
            width = x;
    }

    if (width_out)
        *width_out = width;
    if (height_out)
        *height_out = height;
}

bool fontInitialize(void)
{
    FT_Error ret = 0;

    for (u32 i = 0; i < FONT_FACES_MAX; i++)
    {
        s_font_facesret[i] = 1;
    }

    ret = FT_Init_FreeType(&s_font_library);
    s_font_libret = ret;
    if (s_font_libret) return false;

    PlFontData fonts[PlSharedFontType_Total];
    Result rc = 0;
    rc = plGetSharedFont(s_textLanguageCode, fonts, FONT_FACES_MAX, &s_font_faces_total);
    if (R_FAILED(rc)) return false;

    for (u32 i = 0; i < s_font_faces_total; i++)
    {
        ret = FT_New_Memory_Face( 
            s_font_library,
            (const FT_Byte*)fonts[i].address,
            fonts[i].size,
            0,
            &s_font_faces[i]
        );

        s_font_facesret[i] = ret;
        if (ret) return false;
    }

    return true;
}

void fontExit()
{
    for (u32 i = 0; i < s_font_faces_total; i++)
    {
        if (s_font_facesret[i] == 0)
        {
            FT_Done_Face(s_font_faces[i]);
        }
    }

    if (s_font_libret == 0)
    {
        FT_Done_FreeType(s_font_library);
    }
}

void DrawImage(int x, int y, int width, int height, const u8 *image, ImageMode mode)
{
    int tmpx, tmpy;
    int pos;
    color_t current_color;

    for (tmpy = 0; tmpy < height; tmpy++)
    {
        for (tmpx = 0; tmpx < width; tmpx++)
        {
            switch (mode)
            {
                case IMAGE_MODE_RGB24:
                    pos = ((tmpy*width) + tmpx) * 3;
                    current_color = MakeColor(image[pos+0], image[pos+1], image[pos+2], 255);
                    break; 
                case IMAGE_MODE_RGBA32:
                    pos = ((tmpy*width) + tmpx) * 4;
                    current_color = MakeColor(image[pos+0], image[pos+1], image[pos+2], image[pos+3]);
                    break;
            }
            DrawPixel(x+tmpx, y+tmpy, current_color);
        }
    }
}

void rectangled(u32 x, u32 y, u32 w, u32 h, color_t color)
{
    for (u32 j = y; j < y + h; j++)
    {
        for (u32 i = x; i < x + w; i++)
        {
            DrawPixel(i, j, color);
        }
    }
}

void rectangle(u32 x, u32 y, u32 w, u32 h, color_t color)
{
    for (u32 j = y; j < y + h; j++)
    {
        for (u32 i = x; i < x + w; i+=4)
        {
            Draw4PixelsRaw(i, j, color);
        }
    }
}

void downscaleRGBImg(const u8 *image, u8* out, int srcWidth, int srcHeight, int destWidth, int destHeight)
{
    int tmpx, tmpy;
    int pos;
    float sourceX, sourceY;
    float xScale = (float)srcWidth / (float)destWidth;
    float yScale = (float)srcHeight / (float)destHeight;
    int pixelX, pixelY;
    u8 r1, r2, r3, r4;
    u8 g1, g2, g3, g4;
    u8 b1, b2, b3, b4;
    float fx, fy, fx1, fy1;
    int w1, w2, w3, w4;

    for (tmpx=0; tmpx<destWidth; tmpx++)
    {
        for (tmpy=0; tmpy<destHeight; tmpy++)
        {
            sourceX = tmpx * xScale;
            sourceY = tmpy * yScale;
            pixelX = (int)sourceX;
            pixelY = (int)sourceY;

            // get colours from four surrounding pixels
            pos = ((pixelY + 0) * srcWidth + pixelX + 0) * 3;
            r1 = image[pos+0];
            g1 = image[pos+1];
            b1 = image[pos+2];
            
            pos = ((pixelY + 0) * srcWidth + pixelX + 1) * 3;
            r2 = image[pos+0];
            g2 = image[pos+1];
            b2 = image[pos+2];
            
            pos = ((pixelY + 1) * srcWidth + pixelX + 0) * 3;
            r3 = image[pos+0];
            g3 = image[pos+1];
            b3 = image[pos+2];

            pos = ((pixelY + 1) * srcWidth + pixelX + 1) * 3;
            r4 = image[pos+0];
            g4 = image[pos+1];
            b4 = image[pos+2];

            // determine weights
            fx = sourceX - pixelX;
            fy = sourceY - pixelY;
            fx1 = 1.0f - fx;
            fy1 = 1.0f - fy;

            w1 = (int)(fx1*fy1*256.0);
            w2 = (int)(fx*fy1*256.0);
            w3 = (int)(fx1*fy*256.0);
            w4 = (int)(fx*fy*256.0);
 
            // set output pixels
            pos = ((tmpy*destWidth) + tmpx) * 3;
            out[pos+0] = (u8)((r1 * w1 + r2 * w2 + r3 * w3 + r4 * w4) >> 8);
            out[pos+1] = (u8)((g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) >> 8);
            out[pos+2] = (u8)((b1 * w1 + b2 * w2 + b3 * w3 + b4 * w4) >> 8);
        }
    }
}
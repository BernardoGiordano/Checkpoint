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

static inline const ffnt_page_t* FontGetPage(const ffnt_header_t* font, u32 page_id)
{
    if (page_id >= font->npages)
        return NULL;
    ffnt_pageentry_t* ent = &((ffnt_pageentry_t*)(font+1))[page_id];
    if (ent->size == 0)
        return NULL;
    return (const ffnt_page_t*)((const u8*)font + ent->offset);
}

static inline bool FontLoadGlyph(glyph_t* glyph, const ffnt_header_t* font, u32 codepoint)
{
    const ffnt_page_t* page = FontGetPage(font, codepoint >> 8);
    if (!page)
        return false;

    codepoint &= 0xFF;
    u32 off = page->hdr.pos[codepoint];
    if (off == ~(u32)0)
        return false;

    glyph->width   = page->hdr.widths[codepoint];
    glyph->height  = page->hdr.heights[codepoint];
    glyph->advance = page->hdr.advances[codepoint];
    glyph->posX    = page->hdr.posX[codepoint];
    glyph->posY    = page->hdr.posY[codepoint];
    glyph->data    = &page->data[off];
    return true;
}

static void DrawGlyph(u32 x, u32 y, color_t clr, const glyph_t* glyph)
{
    u32 i, j;
    const u8* data = glyph->data;
    x += glyph->posX;
    y += glyph->posY;
    for (j = 0; j < glyph->height; j ++)
    {
        for (i = 0; i < glyph->width; i ++)
        {
            clr.a = *data++;
            if (!clr.a) continue;
            DrawPixel(x+i, y+j, clr);
        }
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

static void DrawText_(const ffnt_header_t* font, u32 x, u32 y, color_t clr, const char* text, u32 max_width)
{
    y += font->baseline;
    u32 origX = x;

    while (*text)
    {
        if (max_width && x-origX >= max_width) {
            break;
        }

        glyph_t glyph;
        u32 codepoint = DecodeUTF8(&text);

        if (codepoint == '\n')
        {
            if (max_width) {
                break;
            }

            x = origX;
            y += font->height;
            continue;
        }

        if (!FontLoadGlyph(&glyph, font, codepoint))
        {
            if (!FontLoadGlyph(&glyph, font, '?'))
                continue;
        }

        DrawGlyph(x, y, clr, &glyph);
        x += glyph.advance;
    }
}

void DrawText(const ffnt_header_t* font, u32 x, u32 y, color_t clr, const char* text)
{
    DrawText_(font, x, y, clr, text, 0);
}

void DrawTextTruncate(const ffnt_header_t* font, u32 x, u32 y, color_t clr, const char* text, u32 max_width)
{
    DrawText_(font, x, y, clr, text, max_width);
}

void GetTextDimensions(const ffnt_header_t* font, const char* text, u32* width_out, u32* height_out)
{
    u32 x = 0;
    u32 width = 0, height = 0;
    while (*text)
    {
        glyph_t glyph;
        u32 codepoint = DecodeUTF8(&text);

        if (codepoint == '\n')
        {
            x = 0;
            height += font->height;
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
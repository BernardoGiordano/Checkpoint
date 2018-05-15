#ifndef TYPES_HPP
#define TYPES_HPP

typedef enum {
    TITLES,
    CELLS
} entryType_t;

typedef union {
    u32 abgr;
    struct {
        u8 r,g,b,a;
    };
} color_t;

typedef enum
{
    IMAGE_MODE_RGB24,
    IMAGE_MODE_RGBA32
} ImageMode;

typedef struct {
    u8 width, height;
    int8_t posX, posY, advance, pitch;
    const u8* data;
} glyph_t;

#endif
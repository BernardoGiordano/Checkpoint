/*
SDL_FontCache: A font cache for SDL and SDL_ttf
by Jonathan Dearborn

See SDL_FontCache.h for license info.
*/

#include "SDL_FontCache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FC_GET_ALPHA(sdl_color) ((sdl_color).a)

#define FC_MIN(a,b) ((a) < (b)? (a) : (b))
#define FC_MAX(a,b) ((a) > (b)? (a) : (b))

#define FC_EXTRACT_VARARGS(buffer, start_args) \
{ \
    va_list lst; \
    va_start(lst, start_args); \
    vsnprintf(buffer, fc_buffer_size, start_args, lst); \
    va_end(lst); \
}

// Extra pixels of padding around each glyph to avoid linear filtering artifacts
#define FC_CACHE_PADDING 1

static Uint8 has_clip(FC_Target* dest)
{
    return SDL_RenderIsClipEnabled(dest);
}

static FC_Rect get_clip(FC_Target* dest)
{
    SDL_Rect r;
    SDL_RenderGetClipRect(dest, &r);
    return r;
}

static void set_clip(FC_Target* dest, FC_Rect* rect)
{
    SDL_RenderSetClipRect(dest, rect);
}

static void set_color(FC_Image* src, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetTextureColorMod(src, r, g, b);
    SDL_SetTextureAlphaMod(src, a);
}

static char* new_concat(const char* a, const char* b)
{
    // Create new buffer
    unsigned int size = strlen(a) + strlen(b);
    char* new_string = (char*)malloc(size+1);

    // Concatenate strings in the new buffer
    strcpy(new_string, a);
    strcat(new_string, b);

    return new_string;
}

static char* replace_concat(char** a, const char* b)
{
    char* new_string = new_concat(*a, b);
    free(*a);
    *a = new_string;
    return *a;
}

// Shared buffer for variadic text
static char* fc_buffer = NULL;
static unsigned int fc_buffer_size = 1024;

static Uint8 fc_has_render_target_support = 0;

char* FC_GetStringASCII(void)
{
    static char* buffer = NULL;
    if(buffer == NULL)
    {
        int i;
        char c;
        buffer = (char*)malloc(512);
        memset(buffer, 0, 512);
        i = 0;
        c = 32;
        while(1)
        {
            buffer[i] = c;
            if(c == 126)
                break;
            ++i;
            ++c;
        }
    }
    return U8_strdup(buffer);
}

FC_Rect FC_MakeRect(float x, float y, float w, float h)
{
    FC_Rect r = {x, y, w, h};
    return r;
}

FC_Scale FC_MakeScale(float x, float y)
{
    FC_Scale s = {x, y};
    return s;
}

SDL_Color FC_MakeColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Color c = {r, g, b, a};
    return c;
}

FC_Effect FC_MakeEffect(FC_AlignEnum alignment, FC_Scale scale, SDL_Color color)
{
    FC_Effect e;

    e.alignment = alignment;
    e.scale = scale;
    e.color = color;

    return e;
}

FC_GlyphData FC_MakeGlyphData(int cache_level, Sint16 x, Sint16 y, Uint16 w, Uint16 h)
{
    FC_GlyphData gd;

    gd.rect.x = x;
    gd.rect.y = y;
    gd.rect.w = w;
    gd.rect.h = h;
    gd.cache_level = cache_level;

    return gd;
}

// Enough to hold all of the ascii characters and some.
#define FC_DEFAULT_NUM_BUCKETS 300

typedef struct FC_MapNode
{
    Uint32 key;
    FC_GlyphData value;
    struct FC_MapNode* next;
} FC_MapNode;

typedef struct FC_Map
{
    int num_buckets;
    FC_MapNode** buckets;
} FC_Map;

static FC_Map* FC_MapCreate(int num_buckets)
{
    int i;
    FC_Map* map = (FC_Map*)malloc(sizeof(FC_Map));

    map->num_buckets = num_buckets;
    map->buckets = (FC_MapNode**)malloc(num_buckets * sizeof(FC_MapNode*));

    for(i = 0; i < num_buckets; ++i)
    {
        map->buckets[i] = NULL;
    }

    return map;
}

static void FC_MapFree(FC_Map* map)
{
    int i;
    if(map == NULL)
        return;

    // Go through each bucket
    for(i = 0; i < map->num_buckets; ++i)
    {
        // Delete the nodes in order
        FC_MapNode* node = map->buckets[i];
        while(node != NULL)
        {
            FC_MapNode* last = node;
            node = node->next;
            free(last);
        }
    }

    free(map->buckets);
    free(map);
}

// Note: Does not handle duplicates in any special way.
static FC_GlyphData* FC_MapInsert(FC_Map* map, Uint32 codepoint, FC_GlyphData glyph)
{
    Uint32 index;
    FC_MapNode* node;
    if(map == NULL)
        return NULL;

    // Get index for bucket
    index = codepoint % map->num_buckets;

    // If this bucket is empty, create a node and return its value
    if(map->buckets[index] == NULL)
    {
        node = map->buckets[index] = (FC_MapNode*)malloc(sizeof(FC_MapNode));
        node->key = codepoint;
        node->value = glyph;
        node->next = NULL;
        return &node->value;
    }

    for(node = map->buckets[index]; node != NULL; node = node->next)
    {
        // Find empty node and add a new one on.
        if(node->next == NULL)
        {
            node->next = (FC_MapNode*)malloc(sizeof(FC_MapNode));
            node = node->next;

            node->key = codepoint;
            node->value = glyph;
            node->next = NULL;
            return &node->value;
        }
    }

    return NULL;
}

static FC_GlyphData* FC_MapFind(FC_Map* map, Uint32 codepoint)
{
    Uint32 index;
    FC_MapNode* node;
    if(map == NULL)
        return NULL;

    // Get index for bucket
    index = codepoint % map->num_buckets;

    // Go through list until we find a match
    for(node = map->buckets[index]; node != NULL; node = node->next)
    {
        if(node->key == codepoint)
            return &node->value;
    }

    return NULL;
}

struct FC_Font
{
    SDL_Renderer* renderer;
    TTF_Font* ttf_source;  // TTF_Font source of characters
    TTF_Font* ttf_ext;     // TTF_Font source of system symbols 
    Uint8 owns_ttf_source; // Can we delete the TTF_Font ourselves?

    FC_FilterEnum filter;

    SDL_Color default_color;
    Uint16 height;

    Uint16 maxWidth;
    Uint16 baseline;
    int ascent;
    int descent;

    int lineSpacing;
    int letterSpacing;

    // Uses 32-bit (4-byte) Unicode codepoints to refer to each glyph
    // Codepoints are little endian (reversed from UTF-8) so that something like 0x00000005 is ASCII 5 and the map can be indexed by ASCII values
    FC_Map* glyphs;

    FC_GlyphData last_glyph;  // Texture packing cursor
    int glyph_cache_size;
    int glyph_cache_count;
    FC_Image** glyph_cache;

    char* loading_string;
};

// Private
static FC_GlyphData* FC_PackGlyphData(FC_Font* font, Uint32 codepoint, Uint16 width, Uint16 maxWidth, Uint16 maxHeight);

static FC_Rect FC_RenderLeft(FC_Font* font, FC_Target* dest, float x, float y, FC_Scale scale, const char* text);
static FC_Rect FC_RenderCenter(FC_Font* font, FC_Target* dest, float x, float y, FC_Scale scale, const char* text);
static FC_Rect FC_RenderRight(FC_Font* font, FC_Target* dest, float x, float y, FC_Scale scale, const char* text);

static inline SDL_Surface* FC_CreateSurface32(Uint32 width, Uint32 height)
{
    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
        return SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
    #else
        return SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    #endif
}

char* U8_alloc(unsigned int size)
{
    char* result;
    if(size == 0)
        return NULL;

    result = (char*)malloc(size);
    result[0] = '\0';

    return result;
}

void U8_free(char* string)
{
    free(string);
}

char* U8_strdup(const char* string)
{
    char* result;
    if(string == NULL)
        return NULL;

    result = (char*)malloc(strlen(string)+1);
    strcpy(result, string);

    return result;
}

int U8_strlen(const char* string)
{
    int length = 0;
    if(string == NULL)
        return 0;

    while(*string != '\0')
    {
        string = U8_next(string);
        ++length;
    }

    return length;
}

int U8_charsize(const char* character)
{
    if(character == NULL)
        return 0;

    if((unsigned char)*character <= 0x7F)
        return 1;
    else if((unsigned char)*character < 0xE0)
        return 2;
    else if((unsigned char)*character < 0xF0)
        return 3;
    else
        return 4;
    return 1;
}

int U8_charcpy(char* buffer, const char* source, int buffer_size)
{
    int charsize;
    if(buffer == NULL || source == NULL || buffer_size < 1)
        return 0;

    charsize = U8_charsize(source);
    if(charsize > buffer_size)
        return 0;

    memcpy(buffer, source, charsize);
    return charsize;
}

const char* U8_next(const char* string)
{
    return string + U8_charsize(string);
}

int U8_strinsert(char* string, int position, const char* source, int max_bytes)
{
    int pos_bytes;
    int len;
    int add_len;
    int ulen;

    if(string == NULL || source == NULL)
        return 0;

    len = strlen(string);
    add_len = strlen(source);
    ulen = U8_strlen(string);

    if(position == -1)
        position = ulen;

    if(position < 0 || position > ulen || len + add_len + 1 > max_bytes)
        return 0;

    // Move string pointer to the proper position
    pos_bytes = 0;
    while(*string != '\0' && pos_bytes < position)
    {
        string = (char*)U8_next(string);
        ++pos_bytes;
    }

    // Move the rest of the string out of the way
    memmove(string + add_len, string, len - pos_bytes + 1);

    // Copy in the new characters
    memcpy(string, source, add_len);

    return 1;
}

void U8_strdel(char* string, int position)
{
    if(string == NULL || position < 0)
        return;

    while(*string != '\0')
    {
        if(position == 0)
        {
            int chars_to_erase = U8_charsize(string);
            int remaining_bytes = strlen(string) + 1;
            memmove(string, string + chars_to_erase, remaining_bytes);
            break;
        }

        string = (char*)U8_next(string);
        --position;
    }
}

static inline FC_Rect FC_RectUnion(FC_Rect A, FC_Rect B)
{
    float x,x2,y,y2;
    x = FC_MIN(A.x, B.x);
    y = FC_MIN(A.y, B.y);
    x2 = FC_MAX(A.x+A.w, B.x+B.w);
    y2 = FC_MAX(A.y+A.h, B.y+B.h);
    {
        FC_Rect result = {x, y, FC_MAX(0, x2 - x), FC_MAX(0, y2 - y)};
        return result;
    }
}

// Adapted from SDL_IntersectRect
static inline FC_Rect FC_RectIntersect(FC_Rect A, FC_Rect B)
{
    FC_Rect result;
    float Amin, Amax, Bmin, Bmax;

    // Horizontal intersection
    Amin = A.x;
    Amax = Amin + A.w;
    Bmin = B.x;
    Bmax = Bmin + B.w;
    if(Bmin > Amin)
            Amin = Bmin;
    result.x = Amin;
    if(Bmax < Amax)
            Amax = Bmax;
    result.w = Amax - Amin > 0 ? Amax - Amin : 0;

    // Vertical intersection
    Amin = A.y;
    Amax = Amin + A.h;
    Bmin = B.y;
    Bmax = Bmin + B.h;
    if(Bmin > Amin)
            Amin = Bmin;
    result.y = Amin;
    if(Bmax < Amax)
            Amax = Bmax;
    result.h = Amax - Amin > 0 ? Amax - Amin : 0;

    return result;
}

FC_Rect FC_DefaultRenderCallback(FC_Image* src, FC_Rect* srcrect, FC_Target* dest, float x, float y, float xscale, float yscale)
{
    float w = srcrect->w * xscale;
    float h = srcrect->h * yscale;
    FC_Rect result;

    // FIXME: Why does the scaled offset look so wrong?
    {
        SDL_RendererFlip flip = SDL_FLIP_NONE;
        if(xscale < 0)
        {
            xscale = -xscale;
            flip = (SDL_RendererFlip) ((int)flip | (int)SDL_FLIP_HORIZONTAL);
        }
        if(yscale < 0)
        {
            yscale = -yscale;
            flip = (SDL_RendererFlip) ((int)flip | (int)SDL_FLIP_VERTICAL);
        }

        SDL_Rect r = *srcrect;
        SDL_Rect dr = {(int)x, (int)y, (int)(xscale*r.w), (int)(yscale*r.h)};
        SDL_RenderCopyEx(dest, src, &r, &dr, 0, NULL, flip);
    }

    result.x = x;
    result.y = y;
    result.w = w;
    result.h = h;
    return result;
}

static FC_Rect (*fc_render_callback)(FC_Image* src, FC_Rect* srcrect, FC_Target* dest, float x, float y, float xscale, float yscale) = &FC_DefaultRenderCallback;

void FC_SetRenderCallback(FC_Rect (*callback)(FC_Image* src, FC_Rect* srcrect, FC_Target* dest, float x, float y, float xscale, float yscale))
{
    if(callback == NULL)
        fc_render_callback = &FC_DefaultRenderCallback;
    else
        fc_render_callback = callback;
}

void FC_GetUTF8FromCodepoint(char* result, Uint32 codepoint)
{
    char a, b, c, d;

    if(result == NULL)
        return;

    a = (codepoint >> 24) & 0xFF;
    b = (codepoint >> 16) & 0xFF;
    c = (codepoint >> 8) & 0xFF;
    d = codepoint & 0xFF;

    if(a == 0)
    {
        if(b == 0)
        {
            if(c == 0)
            {
                result[0] = d;
                result[1] = '\0';
            }
            else
            {
                result[0] = c;
                result[1] = d;
                result[2] = '\0';
            }
        }
        else
        {
            result[0] = b;
            result[1] = c;
            result[2] = d;
            result[3] = '\0';
        }
    }
    else
    {
        result[0] = a;
        result[1] = b;
        result[2] = c;
        result[3] = d;
        result[4] = '\0';
    }
}

Uint32 FC_GetCodepointFromUTF8(const char** c, Uint8 advance_pointer)
{
    Uint32 result = 0;
    const char* str;
    if(c == NULL || *c == NULL)
        return 0;

    str = *c;
    if((unsigned char)*str <= 0x7F)
        result = *str;
    else if((unsigned char)*str < 0xE0)
    {
        result |= (unsigned char)(*str) << 8;
        result |= (unsigned char)(*(str+1));
        if(advance_pointer)
            *c += 1;
    }
    else if((unsigned char)*str < 0xF0)
    {
        result |= (unsigned char)(*str) << 16;
        result |= (unsigned char)(*(str+1)) << 8;
        result |= (unsigned char)(*(str+2));
        if(advance_pointer)
            *c += 2;
    }
    else
    {
        result |= (unsigned char)(*str) << 24;
        result |= (unsigned char)(*(str+1)) << 16;
        result |= (unsigned char)(*(str+2)) << 8;
        result |= (unsigned char)(*(str+3));
        if(advance_pointer)
            *c += 3;
    }
    return result;
}

void FC_SetLoadingString(FC_Font* font, const char* string)
{
    if(font == NULL)
        return;

    free(font->loading_string);
    font->loading_string = U8_strdup(string);
}

unsigned int FC_GetBufferSize(void)
{
    return fc_buffer_size;
}

void FC_SetBufferSize(unsigned int size)
{
    free(fc_buffer);
    if(size > 0)
    {
        fc_buffer_size = size;
        fc_buffer = (char*)malloc(fc_buffer_size);
    }
    else
        fc_buffer = (char*)malloc(fc_buffer_size);
}

// Constructors

static void FC_Init(FC_Font* font)
{
    if(font == NULL)
        return;

    font->renderer = NULL;

    font->ttf_source = NULL;
    font->ttf_ext = NULL;
    font->owns_ttf_source = 0;

    font->filter = FC_FILTER_NEAREST;

    font->default_color.r = 0;
    font->default_color.g = 0;
    font->default_color.b = 0;
    FC_GET_ALPHA(font->default_color) = 255;

    font->height = 0; // ascent+descent

    font->maxWidth = 0;
    font->baseline = 0;
    font->ascent = 0;
    font->descent = 0;

    font->lineSpacing = 0;
    font->letterSpacing = 0;

    // Give a little offset for when filtering/mipmaps are used.  Depending on mipmap level, this will still not be enough.
    font->last_glyph.rect.x = FC_CACHE_PADDING;
    font->last_glyph.rect.y = FC_CACHE_PADDING;
    font->last_glyph.rect.w = 0;
    font->last_glyph.rect.h = 0;
    font->last_glyph.cache_level = 0;

    if(font->glyphs != NULL)
        FC_MapFree(font->glyphs);

    font->glyphs = FC_MapCreate(FC_DEFAULT_NUM_BUCKETS);

    font->glyph_cache_size = 3;
    font->glyph_cache_count = 0;

    font->glyph_cache = (FC_Image**)malloc(font->glyph_cache_size * sizeof(FC_Image*));

    if (font->loading_string == NULL)
        font->loading_string = FC_GetStringASCII();

    if(fc_buffer == NULL)
        fc_buffer = (char*)malloc(fc_buffer_size);
}

static Uint8 FC_GrowGlyphCache(FC_Font* font)
{
    if(font == NULL)
        return 0;
    SDL_Texture* new_level = SDL_CreateTexture(font->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, font->height * 12, font->height * 12);
    if(new_level == NULL || !FC_SetGlyphCacheLevel(font, font->glyph_cache_count, new_level))
    {
        FC_Log("Error: SDL_FontCache ran out of packing space and could not add another cache level.\n");
        SDL_DestroyTexture(new_level);
        return 0;
    }
    return 1;
}

Uint8 FC_UploadGlyphCache(FC_Font* font, int cache_level, SDL_Surface* data_surface)
{
    if(font == NULL || data_surface == NULL)
        return 0;
    SDL_Texture* new_level;
    if(!fc_has_render_target_support)
        new_level = SDL_CreateTextureFromSurface(font->renderer, data_surface);
    else
    {
        // Must upload with render target enabled so we can put more glyphs on later
        SDL_Renderer* renderer = font->renderer;

        // Set filter mode for new texture
        char old_filter_mode[16];  // Save it so we can change the hint value in the meantime
        snprintf(old_filter_mode, 16, "%s", SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY));

        if(FC_GetFilterMode(font) == FC_FILTER_LINEAR)
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        else
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

        new_level = SDL_CreateTexture(renderer, data_surface->format->format, SDL_TEXTUREACCESS_TARGET, data_surface->w, data_surface->h);
        SDL_SetTextureBlendMode(new_level, SDL_BLENDMODE_BLEND);

        // Reset filter mode for the temp texture
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

        Uint8 r, g, b, a;
        SDL_Texture* temp = SDL_CreateTextureFromSurface(renderer, data_surface);
        SDL_SetTextureBlendMode(temp, SDL_BLENDMODE_NONE);
        SDL_SetRenderTarget(renderer, new_level);

        SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, r, g, b, a);

        SDL_RenderCopy(renderer, temp, NULL, NULL);
        SDL_SetRenderTarget(renderer, NULL);

        SDL_DestroyTexture(temp);

        // Reset to the old filter value
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, old_filter_mode);

    }
    if(new_level == NULL || !FC_SetGlyphCacheLevel(font, cache_level, new_level))
    {
        FC_Log("Error: SDL_FontCache ran out of packing space and could not add another cache level.\n");
        SDL_DestroyTexture(new_level);
        return 0;
    }
    return 1;
}

static FC_GlyphData* FC_PackGlyphData(FC_Font* font, Uint32 codepoint, Uint16 width, Uint16 maxWidth, Uint16 maxHeight)
{
    FC_Map* glyphs = font->glyphs;
    FC_GlyphData* last_glyph = &font->last_glyph;
    Uint16 height = font->height + FC_CACHE_PADDING;

    if(last_glyph->rect.x + last_glyph->rect.w + width >= maxWidth - FC_CACHE_PADDING)
    {
        if(last_glyph->rect.y + height + height >= maxHeight - FC_CACHE_PADDING)
        {
            // Get ready to pack on the next cache level when it is ready
            last_glyph->cache_level = font->glyph_cache_count;
            last_glyph->rect.x = FC_CACHE_PADDING;
            last_glyph->rect.y = FC_CACHE_PADDING;
            last_glyph->rect.w = 0;
            return NULL;
        }
        else
        {
            // Go to next row
            last_glyph->rect.x = FC_CACHE_PADDING;
            last_glyph->rect.y += height;
            last_glyph->rect.w = 0;
        }
    }

    // Move to next space
    last_glyph->rect.x += last_glyph->rect.w + 1 + FC_CACHE_PADDING;
    last_glyph->rect.w = width;

    return FC_MapInsert(glyphs, codepoint, FC_MakeGlyphData(last_glyph->cache_level, last_glyph->rect.x, last_glyph->rect.y, last_glyph->rect.w, last_glyph->rect.h));
}

FC_Image* FC_GetGlyphCacheLevel(FC_Font* font, int cache_level)
{
    if(font == NULL || cache_level < 0 || cache_level > font->glyph_cache_count)
        return NULL;

    return font->glyph_cache[cache_level];
}

Uint8 FC_SetGlyphCacheLevel(FC_Font* font, int cache_level, FC_Image* cache_texture)
{
    if(font == NULL || cache_level < 0)
        return 0;

    // Must be sequentially added
    if(cache_level > font->glyph_cache_count + 1)
        return 0;

    if(cache_level == font->glyph_cache_count)
    {
        font->glyph_cache_count++;

        // Grow cache?
        if(font->glyph_cache_count > font->glyph_cache_size)
        {
            // Copy old cache to new one
            int i;
            FC_Image** new_cache;
            new_cache = (FC_Image**)malloc(font->glyph_cache_count * sizeof(FC_Image*));
            for(i = 0; i < font->glyph_cache_size; ++i)
                new_cache[i] = font->glyph_cache[i];

            // Save new cache
            free(font->glyph_cache);
            font->glyph_cache_size = font->glyph_cache_count;
            font->glyph_cache = new_cache;
        }
    }

    font->glyph_cache[cache_level] = cache_texture;
    return 1;
}

FC_Font* FC_CreateFont(void)
{
    FC_Font* font;

    font = (FC_Font*)malloc(sizeof(FC_Font));
    memset(font, 0, sizeof(FC_Font));

    FC_Init(font);

    return font;
}

// Assume this many will be enough...
#define FC_LOAD_MAX_SURFACES 10

static void FC_LoadGlyphsFromTTF(FC_Font* font, TTF_Font* ttf, SDL_Surface** surfaces, int* num_surfaces, int w, int h)
{
    Uint8 packed = 0;
    SDL_Surface* glyph_surf;
    SDL_Color white = {255, 255, 255, 255};
    char buff[5] = {0};
    const char* buff_ptr = buff;
    const char* source_string = font->loading_string;

    for(; *source_string != '\0'; source_string = U8_next(source_string))
    {
        if(!U8_charcpy(buff, source_string, 5))
            continue;
        glyph_surf = TTF_RenderUTF8_Blended(ttf, buff, white);
        if(glyph_surf == NULL)
            continue;

        // Try packing.  If it fails, create a new surface for the next cache level.
        packed = (FC_PackGlyphData(font, FC_GetCodepointFromUTF8(&buff_ptr, 0), glyph_surf->w, surfaces[*num_surfaces-1]->w, surfaces[*num_surfaces-1]->h) != NULL);
        if(!packed)
        {
            int i = *num_surfaces-1;
            if(*num_surfaces >= FC_LOAD_MAX_SURFACES)
            {
                // Can't do any more!
                FC_Log("SDL_FontCache error: Could not create enough cache surfaces to fit all of the loading string!\n");
                SDL_FreeSurface(glyph_surf);
                break;
            }

            // Upload the current surface to the glyph cache now so we can keep the cache level packing cursor up to date as we go.
            FC_UploadGlyphCache(font, i, surfaces[i]);
            SDL_FreeSurface(surfaces[i]);
            SDL_SetTextureBlendMode(font->glyph_cache[i], SDL_BLENDMODE_BLEND);
            // Update the glyph cursor to the new cache level.  We need to do this here because the actual cache lags behind our use of the packing above.
            font->last_glyph.cache_level = *num_surfaces;

            surfaces[*num_surfaces] = FC_CreateSurface32(w, h);
            *num_surfaces++;
        }

        // Try packing for the new surface, then blit onto it.
        if(packed || FC_PackGlyphData(font, FC_GetCodepointFromUTF8(&buff_ptr, 0), glyph_surf->w, surfaces[*num_surfaces-1]->w, surfaces[*num_surfaces-1]->h) != NULL)
        {
            SDL_SetSurfaceBlendMode(glyph_surf, SDL_BLENDMODE_NONE);
            SDL_Rect srcRect = {0, 0, glyph_surf->w, glyph_surf->h};
            SDL_Rect destrect = font->last_glyph.rect;
            SDL_BlitSurface(glyph_surf, &srcRect, surfaces[*num_surfaces-1], &destrect);
        }

        SDL_FreeSurface(glyph_surf);
    }
}

Uint8 FC_LoadFontFromTTF(FC_Font* font, SDL_Renderer* renderer, TTF_Font* ttf, TTF_Font* ext, SDL_Color color)
{
    if(font == NULL || ttf == NULL || ext == NULL)
        return 0;
    if(renderer == NULL)
        return 0;

    FC_ClearFont(font);

    // Might as well check render target support here
    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    fc_has_render_target_support = (info.flags & SDL_RENDERER_TARGETTEXTURE);

    font->renderer = renderer;
    font->ttf_source = ttf;
    font->ttf_ext = ext;

    //font->line_height = TTF_FontLineSkip(ttf);
    font->height = TTF_FontHeight(ttf) + 0.16666666666 * TTF_FontHeight(ttf);
    font->ascent = TTF_FontAscent(ttf);
    font->descent = -TTF_FontDescent(ttf);

    font->baseline = font->height - font->descent;
    font->default_color = color;

    // Copy glyphs from the surface to the font texture and store the position data
    // Pack row by row into a square texture
    // Try figuring out dimensions that make sense for the font size.
    unsigned int w = font->height*12;
    unsigned int h = font->height*12;
    SDL_Surface* surfaces[FC_LOAD_MAX_SURFACES];
    int num_surfaces = 1;
    surfaces[0] = FC_CreateSurface32(w, h);
    font->last_glyph.rect.x = FC_CACHE_PADDING;
    font->last_glyph.rect.y = FC_CACHE_PADDING;
    font->last_glyph.rect.w = 0;
    font->last_glyph.rect.h = font->height;

    // load standard glyphs
    FC_LoadGlyphsFromTTF(font, ttf, surfaces, &num_surfaces, w, h);

    // load system symbols
    FC_SetLoadingString(font,
        "\ue000\ue001\ue002\ue003\ue004\ue005\ue041\ue045\ue046\ue085\ue086"

        // TODO: figure out why it crashes while loading the whole symbols
        // "\ue000\ue001\ue002\ue003\ue004\ue005\ue006\ue007\ue008\ue009\ue00a\ue00b\ue00c\ue00d\ue00e\ue00f"
        // "\ue010\ue011\ue012\ue013\ue014\ue015\ue016\ue017\ue018\ue019\ue01a\ue01b\ue01c\ue01d\ue01e\ue01f"
        // "\ue020\ue021\ue022\ue023\ue024\ue025\ue026\ue027\ue028\ue029\ue02a\ue02b\ue02c\ue02d\ue02e\ue02f"
        // "\ue030\ue031\ue032\ue033\ue034\ue035\ue036\ue037\ue038\ue039\ue03a\ue03b\ue03c\ue03d\ue03e\ue03f"
        // "\ue040\ue041\ue042\ue043\ue044\ue045\ue046\ue047\ue048\ue049\ue04a\ue04b\ue04c\ue04d\ue04e\ue04f"
        // "\ue050\ue051\ue052\ue053\ue054\ue055\ue056\ue057\ue058\ue059\ue05a\ue05b\ue05c\ue05d\ue05e\ue05f"
        // "\ue060\ue061\ue062\ue063\ue064\ue065\ue066\ue067\ue068\ue069\ue06a\ue06b"
        // "\ue070\ue071\ue072\ue073\ue074\ue075\ue076\ue077\ue078\ue079\ue07a\ue07b\ue07c\ue07d\ue07e"
        // "\ue080\ue081\ue082\ue083\ue084\ue085\ue086\ue087\ue088\ue089\ue08a\ue08b\ue08c\ue08d\ue08e\ue08f"
        // "\ue090\ue091\ue092\ue093\ue094\ue095\ue096\ue097\ue098\ue099"
        // "\ue0a0\ue0a1\ue0a2\ue0a3\ue0a4\ue0a5\ue0a6\ue0a7\ue0a8\ue0a9\ue0aa\ue0ab\ue0ac\ue0ad\ue0ae\ue0af"
        // "\ue0b0\ue0b1\ue0b2\ue0b3\ue0b4\ue0b5\ue0b6\ue0b7\ue0b8\ue0b9\ue0ba"
        // "\ue0c0\ue0c1\ue0c2\ue0c3\ue0c4\ue0c5\ue0c6\ue0c7\ue0c8\ue0c9"
        // "\ue0d0\ue0d1\ue0d2\ue0d3\ue0d4\ue0d5\ue0d6"
        // "\ue0e0\ue0e1\ue0e2\ue0e3\ue0e4\ue0e5\ue0e6\ue0e7\ue0e8\ue0e9\ue0ea\ue0eb\ue0ec\ue0ed\ue0ee\ue0ef"
        // "\ue0f0\ue0f1\ue0f2\ue0f3\ue0f4\ue0f5"
        // "\ue100\ue101\ue102\ue103\ue104\ue105"
        // "\ue110\ue111\ue112\ue113\ue114\ue115\ue116"
        // "\ue120\ue121\ue122\ue123\ue124\ue125\ue126\ue127\ue128\ue129\ue12a\ue12b\ue12c"
        // "\ue130\ue131\ue132\ue133\ue134\ue135\ue136\ue137\ue138\ue139\ue13a\ue13b\ue13c"
        // "\ue140\ue141\ue142\ue143\ue144\ue145\ue146\ue147\ue148\ue149\ue14a\ue14b\ue14c"
        // "\ue150\ue151\ue152"
    );
    FC_LoadGlyphsFromTTF(font, ext, surfaces, &num_surfaces, w, h);

    int i = num_surfaces-1;
    FC_UploadGlyphCache(font, i, surfaces[i]);
    SDL_FreeSurface(surfaces[i]);
    SDL_SetTextureBlendMode(font->glyph_cache[i], SDL_BLENDMODE_BLEND);

    return 1;
}

Uint8 FC_LoadFont_RW(FC_Font* font, FC_Target* renderer, SDL_RWops* file_rwops_ttf, SDL_RWops* file_rwops_ext, Uint8 own_rwops, Uint32 pointSize, SDL_Color color, int style)
{
    Uint8 result;
    TTF_Font* ttf;
    TTF_Font* ext;
    Uint8 outline;

    if(font == NULL)
        return 0;

    if(!TTF_WasInit() && TTF_Init() < 0)
    {
        FC_Log("Unable to initialize SDL_ttf: %s \n", TTF_GetError());
        if(own_rwops) {
            SDL_RWclose(file_rwops_ttf);
            SDL_RWclose(file_rwops_ext);
        }
        return 0;
    }

    ttf = TTF_OpenFontRW(file_rwops_ttf, own_rwops, pointSize);
    ext = TTF_OpenFontRW(file_rwops_ext, own_rwops, pointSize);

    if(ttf == NULL || ext == NULL)
    {
        FC_Log("Unable to load TrueType font: %s \n", TTF_GetError());
        if(own_rwops) {
            SDL_RWclose(file_rwops_ttf);
            SDL_RWclose(file_rwops_ext);
        }
        return 0;
    }

    outline = (style & TTF_STYLE_OUTLINE);
    if(outline)
    {
        style &= ~TTF_STYLE_OUTLINE;
        TTF_SetFontOutline(ttf, 1);
        TTF_SetFontOutline(ext, 1);
    }
    TTF_SetFontStyle(ttf, style);
    TTF_SetFontStyle(ext, style);

    result = FC_LoadFontFromTTF(font, renderer, ttf, ext, color);

    // Can only load new (uncached) glyphs if we can keep the SDL_RWops open.
    font->owns_ttf_source = own_rwops;
    if(!own_rwops)
    {
        TTF_CloseFont(font->ttf_source);
        TTF_CloseFont(font->ttf_ext);
        font->ttf_source = NULL;
        font->ttf_ext = NULL;
    }

    return result;
}

void FC_ClearFont(FC_Font* font)
{
    int i;
    if(font == NULL)
        return;

    // Release resources
    if(font->owns_ttf_source) {
        TTF_CloseFont(font->ttf_source);
        TTF_CloseFont(font->ttf_ext);
    }

    font->owns_ttf_source = 0;
    font->ttf_source = NULL;
    font->ttf_ext = NULL;

    // Delete glyph map
    FC_MapFree(font->glyphs);
    font->glyphs = NULL;

    // Delete glyph cache
    for(i = 0; i < font->glyph_cache_count; ++i)
    {
        SDL_DestroyTexture(font->glyph_cache[i]);
    }
    free(font->glyph_cache);
    font->glyph_cache = NULL;

    // Reset font
    FC_Init(font);
}

void FC_FreeFont(FC_Font* font)
{
    int i;
    if(font == NULL)
        return;

    // Release resources
    if(font->owns_ttf_source) {
        TTF_CloseFont(font->ttf_source);
        TTF_CloseFont(font->ttf_ext);
    }

    // Delete glyph map
    FC_MapFree(font->glyphs);

    // Delete glyph cache
    for(i = 0; i < font->glyph_cache_count; ++i)
    {
        SDL_DestroyTexture(font->glyph_cache[i]);
    }
    free(font->glyph_cache);

    free(font->loading_string);

    free(font);
}

int FC_GetNumCacheLevels(FC_Font* font)
{
    return font->glyph_cache_count;
}

Uint8 FC_AddGlyphToCache(FC_Font* font, SDL_Surface* glyph_surface)
{
    if(font == NULL || glyph_surface == NULL)
        return 0;

    SDL_SetSurfaceBlendMode(glyph_surface, SDL_BLENDMODE_NONE);
    FC_Image* dest = FC_GetGlyphCacheLevel(font, font->last_glyph.cache_level);
    if(dest == NULL)
        return 0;

    {
        SDL_Renderer* renderer = font->renderer;
        Uint8 use_clip;
        FC_Rect clip_rect;
        SDL_Texture* img;
        SDL_Rect destrect;

        use_clip = has_clip(renderer);
        if(use_clip)
        {
            clip_rect = get_clip(renderer);
            set_clip(renderer, NULL);
        }

        img = SDL_CreateTextureFromSurface(renderer, glyph_surface);

        destrect = font->last_glyph.rect;
        SDL_SetRenderTarget(renderer, dest);
        SDL_RenderCopy(renderer, img, NULL, &destrect);
        SDL_SetRenderTarget(renderer, NULL);
        SDL_DestroyTexture(img);

        if(use_clip)
            set_clip(renderer, &clip_rect);
    }

    return 1;
}

unsigned int FC_GetNumCodepoints(FC_Font* font)
{
    FC_Map* glyphs;
    int i;
    unsigned int result = 0;
    if(font == NULL || font->glyphs == NULL)
        return 0;

    glyphs = font->glyphs;

    for(i = 0; i < glyphs->num_buckets; ++i)
    {
        FC_MapNode* node;
        for(node = glyphs->buckets[i]; node != NULL; node = node->next)
        {
            result++;
        }
    }

    return result;
}

void FC_GetCodepoints(FC_Font* font, Uint32* result)
{
    FC_Map* glyphs;
    int i;
    unsigned int count = 0;
    if(font == NULL || font->glyphs == NULL)
        return;

    glyphs = font->glyphs;

    for(i = 0; i < glyphs->num_buckets; ++i)
    {
        FC_MapNode* node;
        for(node = glyphs->buckets[i]; node != NULL; node = node->next)
        {
            result[count] = node->key;
            count++;
        }
    }
}

Uint8 FC_GetGlyphData(FC_Font* font, FC_GlyphData* result, Uint32 codepoint)
{
    FC_GlyphData* e = FC_MapFind(font->glyphs, codepoint);
    if(e == NULL)
    {
        char buff[5];
        int w, h;
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* surf;
        FC_Image* cache_image;

        if(font->ttf_source == NULL || font->ttf_ext)
            return 0;

        FC_GetUTF8FromCodepoint(buff, codepoint);
        cache_image = FC_GetGlyphCacheLevel(font, font->last_glyph.cache_level);
        if(cache_image == NULL)
        {
            FC_Log("SDL_FontCache: Failed to load cache image, so cannot add new glyphs!\n");
            return 0;
        }

        SDL_QueryTexture(cache_image, NULL, NULL, &w, &h);

        surf = TTF_RenderUTF8_Blended(codepoint >= 0xE000 ? font->ttf_ext : font->ttf_source, buff, white);
        if(surf == NULL)
        {
            return 0;
        }

        e = FC_PackGlyphData(font, codepoint, surf->w, w, h);
        if(e == NULL)
        {
            // Grow the cache
            FC_GrowGlyphCache(font);

            // Try packing again
            e = FC_PackGlyphData(font, codepoint, surf->w, w, h);
            if(e == NULL)
            {
                SDL_FreeSurface(surf);
                return 0;
            }
        }

        // Render onto the cache texture
        FC_AddGlyphToCache(font, surf);

        SDL_FreeSurface(surf);
    }

    if(result != NULL && e != NULL)
        *result = *e;

    return 1;
}

FC_GlyphData* FC_SetGlyphData(FC_Font* font, Uint32 codepoint, FC_GlyphData glyph_data)
{
    return FC_MapInsert(font->glyphs, codepoint, glyph_data);
}

// Drawing
static FC_Rect FC_RenderLeft(FC_Font* font, FC_Target* dest, float x, float y, FC_Scale scale, const char* text)
{
    const char* c = text;
    FC_Rect srcRect;
    FC_Rect dstRect;
    FC_Rect dirtyRect = FC_MakeRect(x, y, 0, 0);

    FC_GlyphData glyph;
    Uint32 codepoint;

    float destX = x;
    float destY = y;
    float destH;
    float destLineSpacing;
    float destLetterSpacing;

    if(font == NULL)
        return dirtyRect;

    destH = font->height * scale.y;
    destLineSpacing = font->lineSpacing*scale.y;
    destLetterSpacing = font->letterSpacing*scale.x;

    if(c == NULL || font->glyph_cache_count == 0 || dest == NULL)
        return dirtyRect;

    int newlineX = x;

    for(; *c != '\0'; c++)
    {
        if(*c == '\n')
        {
            destX = newlineX;
            destY += destH + destLineSpacing;
            continue;
        }
  
        codepoint = FC_GetCodepointFromUTF8(&c, 1);  // Increments 'c' to skip the extra UTF-8 bytes
        if(!FC_GetGlyphData(font, &glyph, codepoint))
        {
            codepoint = ' ';
            if(!FC_GetGlyphData(font, &glyph, codepoint))
                continue;  // Skip bad characters
        }

        if (codepoint == ' ')
        {
            destX += glyph.rect.w*scale.x + destLetterSpacing;
            continue;
        }
        /*if(destX >= dest->w)
            continue;
        if(destY >= dest->h)
            continue;*/

        srcRect = glyph.rect;
        dstRect = fc_render_callback(FC_GetGlyphCacheLevel(font, glyph.cache_level), &srcRect, dest, destX, destY, scale.x, scale.y);
        if(dirtyRect.w == 0 || dirtyRect.h == 0)
            dirtyRect = dstRect;
        else
            dirtyRect = FC_RectUnion(dirtyRect, dstRect);

        destX += glyph.rect.w*scale.x + destLetterSpacing;
    }

    return dirtyRect;
}

static void set_color_for_all_caches(FC_Font* font, SDL_Color color)
{
    // TODO: How can I predict which glyph caches are to be used?
    FC_Image* img;
    int i;
    int num_levels = FC_GetNumCacheLevels(font);
    for(i = 0; i < num_levels; ++i)
    {
        img = FC_GetGlyphCacheLevel(font, i);
        set_color(img, color.r, color.g, color.b, FC_GET_ALPHA(color));
    }
}

FC_Rect FC_Draw(FC_Font* font, FC_Target* dest, float x, float y, const char* formatted_text, ...)
{
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, font->default_color);

    return FC_RenderLeft(font, dest, x, y, FC_MakeScale(1,1), fc_buffer);
}

typedef struct FC_StringList
{
    char* value;
    struct FC_StringList* next;
} FC_StringList;

void FC_StringListFree(FC_StringList* node)
{
    // Delete the nodes in order
    while(node != NULL)
    {
        FC_StringList* last = node;
        node = node->next;

        free(last->value);
        free(last);
    }
}

FC_StringList** FC_StringListPushBack(FC_StringList** node, char* value, Uint8 copy)
{
    if(node == NULL)
    {
        return node;
    }
    
    // Get to the last node
    while(*node != NULL)
    {
        node = &(*node)->next;
    }

    *node = (FC_StringList*)malloc(sizeof(FC_StringList));

    (*node)->value = (copy? U8_strdup(value) : value);
    (*node)->next = NULL;

    return node;
}

static FC_StringList* FC_Explode(const char* text, char delimiter)
{
    FC_StringList* head;
    FC_StringList* new_node;
    FC_StringList** node;
    const char* start;
    const char* end;
    unsigned int size;
    if(text == NULL)
        return NULL;

    head = NULL;
    node = &head;

    // Doesn't technically support UTF-8, but it's probably fine, right?
    size = 0;
    start = end = text;
    while(1)
    {
        if(*end == delimiter || *end == '\0')
        {
            *node = (FC_StringList*)malloc(sizeof(FC_StringList));
            new_node = *node;

            new_node->value = (char*)malloc(size + 1);
            memcpy(new_node->value, start, size);
            new_node->value[size] = '\0';

            new_node->next = NULL;

            if(*end == '\0')
                break;

            node = &((*node)->next);
            start = end+1;
            size = 0;
        }
        else
            ++size;

        ++end;
    }

    return head;
}

static FC_StringList* FC_ExplodeAndKeep(const char* text, char delimiter)
{
    FC_StringList* head;
    FC_StringList* new_node;
    FC_StringList** node;
    const char* start;
    const char* end;
    unsigned int size;
    if(text == NULL)
        return NULL;

    head = NULL;
    node = &head;

    // Doesn't technically support UTF-8, but it's probably fine, right?
    size = 0;
    start = end = text;
    while(1)
    {
        if(*end == delimiter || *end == '\0')
        {
            *node = (FC_StringList*)malloc(sizeof(FC_StringList));
            new_node = *node;

            new_node->value = (char*)malloc(size + 1);
            memcpy(new_node->value, start, size);
            new_node->value[size] = '\0';

            new_node->next = NULL;

            if(*end == '\0')
                break;

            node = &((*node)->next);
            start = end;
            size = 1;
        }
        else
            ++size;

        ++end;
    }

    return head;
}

static void FC_RenderAlign(FC_Font* font, FC_Target* dest, float x, float y, int width, FC_Scale scale, FC_AlignEnum align, const char* text)
{
    switch(align)
    {
        case FC_ALIGN_LEFT:
            FC_RenderLeft(font, dest, x, y, scale, text);
            break;
        case FC_ALIGN_CENTER:
            FC_RenderCenter(font, dest, x + width/2, y, scale, text);
            break;
        case FC_ALIGN_RIGHT:
            FC_RenderRight(font, dest, x + width, y, scale, text);
            break;
    }
}

static FC_StringList* FC_GetBufferFitToColumn(FC_Font* font, int width, FC_Scale scale, Uint8 keep_newlines)
{
    FC_StringList* result = NULL;
    FC_StringList** current = &result;

    FC_StringList *ls, *iter;

    ls = (keep_newlines? FC_ExplodeAndKeep(fc_buffer, '\n') : FC_Explode(fc_buffer, '\n'));
    for(iter = ls; iter != NULL; iter = iter->next)
    {
        char* line = iter->value;

        // If line is too long, then add words one at a time until we go over.
        if(width > 0 && FC_GetWidth(font, "%s", line) > width)
        {
            FC_StringList *words, *word_iter;

            words = FC_Explode(line, ' ');
            // Skip the first word for the iterator, so there will always be at least one word per line
            line = new_concat(words->value, " ");
            for(word_iter = words->next; word_iter != NULL; word_iter = word_iter->next)
            {
                char* line_plus_word = new_concat(line, word_iter->value);
                char* word_plus_space = new_concat(word_iter->value, " ");
                if(FC_GetWidth(font, "%s", line_plus_word) > width)
                {
                    current = FC_StringListPushBack(current, line, 0);

                    line = word_plus_space;
                }
                else
                {
                    replace_concat(&line, word_plus_space);
                    free(word_plus_space);
                }
                free(line_plus_word);
            }
            current = FC_StringListPushBack(current, line, 0);
            FC_StringListFree(words);
        }
        else
        {
            current = FC_StringListPushBack(current, line, 0);
            iter->value = NULL;
        }
    }
    FC_StringListFree(ls);

    return result;
}

static void FC_DrawColumnFromBuffer(FC_Font* font, FC_Target* dest, FC_Rect box, int* total_height, FC_Scale scale, FC_AlignEnum align)
{
    int y = box.y;
    FC_StringList *ls, *iter;

    ls = FC_GetBufferFitToColumn(font, box.w, scale, 0);
    for(iter = ls; iter != NULL; iter = iter->next)
    {
        FC_RenderAlign(font, dest, box.x, y, box.w, scale, align, iter->value);
        y += FC_GetLineHeight(font);
    }
    FC_StringListFree(ls);

    if(total_height != NULL)
        *total_height = y - box.y;
}

FC_Rect FC_DrawBox(FC_Font* font, FC_Target* dest, FC_Rect box, const char* formatted_text, ...)
{
    Uint8 useClip;
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(box.x, box.y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    useClip = has_clip(dest);
    FC_Rect oldclip, newclip;
    if(useClip)
    {
        oldclip = get_clip(dest);
        newclip = FC_RectIntersect(oldclip, box);
    }
    else
        newclip = box;

    set_clip(dest, &newclip);

    set_color_for_all_caches(font, font->default_color);

    FC_DrawColumnFromBuffer(font, dest, box, NULL, FC_MakeScale(1,1), FC_ALIGN_LEFT);

    if(useClip)
        set_clip(dest, &oldclip);
    else
        set_clip(dest, NULL);

    return box;
}

FC_Rect FC_DrawBoxAlign(FC_Font* font, FC_Target* dest, FC_Rect box, FC_AlignEnum align, const char* formatted_text, ...)
{
    Uint8 useClip;
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(box.x, box.y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    useClip = has_clip(dest);
    FC_Rect oldclip, newclip;
    if(useClip)
    {
        oldclip = get_clip(dest);
        newclip = FC_RectIntersect(oldclip, box);
    }
    else
        newclip = box;
    set_clip(dest, &newclip);

    set_color_for_all_caches(font, font->default_color);

    FC_DrawColumnFromBuffer(font, dest, box, NULL, FC_MakeScale(1,1), align);

    if(useClip)
        set_clip(dest, &oldclip);
    else
        set_clip(dest, NULL);

    return box;
}

FC_Rect FC_DrawBoxScale(FC_Font* font, FC_Target* dest, FC_Rect box, FC_Scale scale, const char* formatted_text, ...)
{
    Uint8 useClip;
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(box.x, box.y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    useClip = has_clip(dest);
    FC_Rect oldclip, newclip;
    if(useClip)
    {
        oldclip = get_clip(dest);
        newclip = FC_RectIntersect(oldclip, box);
    }
    else
        newclip = box;
    set_clip(dest, &newclip);

    set_color_for_all_caches(font, font->default_color);

    FC_DrawColumnFromBuffer(font, dest, box, NULL, scale, FC_ALIGN_LEFT);

    if(useClip)
        set_clip(dest, &oldclip);
    else
        set_clip(dest, NULL);

    return box;
}

FC_Rect FC_DrawBoxColor(FC_Font* font, FC_Target* dest, FC_Rect box, SDL_Color color, const char* formatted_text, ...)
{
    Uint8 useClip;
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(box.x, box.y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    useClip = has_clip(dest);
    FC_Rect oldclip, newclip;
    if(useClip)
    {
        oldclip = get_clip(dest);
        newclip = FC_RectIntersect(oldclip, box);
    }
    else
        newclip = box;
    set_clip(dest, &newclip);

    set_color_for_all_caches(font, color);

    FC_DrawColumnFromBuffer(font, dest, box, NULL, FC_MakeScale(1,1), FC_ALIGN_LEFT);

    if(useClip)
        set_clip(dest, &oldclip);
    else
        set_clip(dest, NULL);

    return box;
}

FC_Rect FC_DrawBoxEffect(FC_Font* font, FC_Target* dest, FC_Rect box, FC_Effect effect, const char* formatted_text, ...)
{
    Uint8 useClip;
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(box.x, box.y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    useClip = has_clip(dest);
    FC_Rect oldclip, newclip;
    if(useClip)
    {
        oldclip = get_clip(dest);
        newclip = FC_RectIntersect(oldclip, box);
    }
    else
        newclip = box;
    set_clip(dest, &newclip);

    set_color_for_all_caches(font, effect.color);

    FC_DrawColumnFromBuffer(font, dest, box, NULL, effect.scale, effect.alignment);

    if(useClip)
        set_clip(dest, &oldclip);
    else
        set_clip(dest, NULL);

    return box;
}

FC_Rect FC_DrawColumn(FC_Font* font, FC_Target* dest, float x, float y, Uint16 width, const char* formatted_text, ...)
{
    FC_Rect box = {x, y, width, 0};
    int total_height;

    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, font->default_color);

    FC_DrawColumnFromBuffer(font, dest, box, &total_height, FC_MakeScale(1,1), FC_ALIGN_LEFT);

    return FC_MakeRect(box.x, box.y, width, total_height);
}

FC_Rect FC_DrawColumnAlign(FC_Font* font, FC_Target* dest, float x, float y, Uint16 width, FC_AlignEnum align, const char* formatted_text, ...)
{
    FC_Rect box = {x, y, width, 0};
    int total_height;

    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, font->default_color);

    switch(align)
    {
    case FC_ALIGN_CENTER:
        box.x -= width/2;
        break;
    case FC_ALIGN_RIGHT:
        box.x -= width;
        break;
    default:
        break;
    }

    FC_DrawColumnFromBuffer(font, dest, box, &total_height, FC_MakeScale(1,1), align);

    return FC_MakeRect(box.x, box.y, width, total_height);
}

FC_Rect FC_DrawColumnScale(FC_Font* font, FC_Target* dest, float x, float y, Uint16 width, FC_Scale scale, const char* formatted_text, ...)
{
    FC_Rect box = {x, y, width, 0};
    int total_height;

    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, font->default_color);

    FC_DrawColumnFromBuffer(font, dest, box, &total_height, scale, FC_ALIGN_LEFT);

    return FC_MakeRect(box.x, box.y, width, total_height);
}

FC_Rect FC_DrawColumnColor(FC_Font* font, FC_Target* dest, float x, float y, Uint16 width, SDL_Color color, const char* formatted_text, ...)
{
    FC_Rect box = {x, y, width, 0};
    int total_height;

    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, color);

    FC_DrawColumnFromBuffer(font, dest, box, &total_height, FC_MakeScale(1,1), FC_ALIGN_LEFT);

    return FC_MakeRect(box.x, box.y, width, total_height);
}

FC_Rect FC_DrawColumnEffect(FC_Font* font, FC_Target* dest, float x, float y, Uint16 width, FC_Effect effect, const char* formatted_text, ...)
{
    FC_Rect box = {x, y, width, 0};
    int total_height;

    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, effect.color);

    switch(effect.alignment)
    {
    case FC_ALIGN_CENTER:
        box.x -= width/2;
        break;
    case FC_ALIGN_RIGHT:
        box.x -= width;
        break;
    default:
        break;
    }

    FC_DrawColumnFromBuffer(font, dest, box, &total_height, effect.scale, effect.alignment);

    return FC_MakeRect(box.x, box.y, width, total_height);
}

static FC_Rect FC_RenderCenter(FC_Font* font, FC_Target* dest, float x, float y, FC_Scale scale, const char* text)
{
    FC_Rect result = {x, y, 0, 0};
    if(text == NULL || font == NULL)
        return result;

    char* str = U8_strdup(text);
    char* del = str;
    char* c;

    // Go through str, when you find a \n, replace it with \0 and print it
    // then move down, back, and continue.
    for(c = str; *c != '\0';)
    {
        if(*c == '\n')
        {
            *c = '\0';
            result = FC_RectUnion(FC_RenderLeft(font, dest, x - scale.x*FC_GetWidth(font, "%s", str)/2.0f, y, scale, str), result);
            *c = '\n';
            c++;
            str = c;
            y += scale.y*font->height;
        }
        else
            c++;
    }

    result = FC_RectUnion(FC_RenderLeft(font, dest, x - scale.x*FC_GetWidth(font, "%s", str)/2.0f, y, scale, str), result);

    free(del);
    return result;
}

static FC_Rect FC_RenderRight(FC_Font* font, FC_Target* dest, float x, float y, FC_Scale scale, const char* text)
{
    FC_Rect result = {x, y, 0, 0};
    if(text == NULL || font == NULL)
        return result;

    char* str = U8_strdup(text);
    char* del = str;
    char* c;

    for(c = str; *c != '\0';)
    {
        if(*c == '\n')
        {
            *c = '\0';
            result = FC_RectUnion(FC_RenderLeft(font, dest, x - scale.x*FC_GetWidth(font, "%s", str), y, scale, str), result);
            *c = '\n';
            c++;
            str = c;
            y += scale.y*font->height;
        }
        else
            c++;
    }

    result = FC_RectUnion(FC_RenderLeft(font, dest, x - scale.x*FC_GetWidth(font, "%s", str), y, scale, str), result);

    free(del);
    return result;
}

FC_Rect FC_DrawScale(FC_Font* font, FC_Target* dest, float x, float y, FC_Scale scale, const char* formatted_text, ...)
{
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, font->default_color);

    return FC_RenderLeft(font, dest, x, y, scale, fc_buffer);
}

FC_Rect FC_DrawAlign(FC_Font* font, FC_Target* dest, float x, float y, FC_AlignEnum align, const char* formatted_text, ...)
{
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, font->default_color);

    FC_Rect result;
    switch(align)
    {
        case FC_ALIGN_LEFT:
            result = FC_RenderLeft(font, dest, x, y, FC_MakeScale(1,1), fc_buffer);
            break;
        case FC_ALIGN_CENTER:
            result = FC_RenderCenter(font, dest, x, y, FC_MakeScale(1,1), fc_buffer);
            break;
        case FC_ALIGN_RIGHT:
            result = FC_RenderRight(font, dest, x, y, FC_MakeScale(1,1), fc_buffer);
            break;
        default:
            result = FC_MakeRect(x, y, 0, 0);
            break;
    }

    return result;
}

FC_Rect FC_DrawColor(FC_Font* font, FC_Target* dest, float x, float y, SDL_Color color, const char* formatted_text, ...)
{
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, color);

    return FC_RenderLeft(font, dest, x, y, FC_MakeScale(1,1), fc_buffer);
}


FC_Rect FC_DrawEffect(FC_Font* font, FC_Target* dest, float x, float y, FC_Effect effect, const char* formatted_text, ...)
{
    if(formatted_text == NULL || font == NULL)
        return FC_MakeRect(x, y, 0, 0);

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    set_color_for_all_caches(font, effect.color);

    FC_Rect result;
    switch(effect.alignment)
    {
        case FC_ALIGN_LEFT:
            result = FC_RenderLeft(font, dest, x, y, effect.scale, fc_buffer);
            break;
        case FC_ALIGN_CENTER:
            result = FC_RenderCenter(font, dest, x, y, effect.scale, fc_buffer);
            break;
        case FC_ALIGN_RIGHT:
            result = FC_RenderRight(font, dest, x, y, effect.scale, fc_buffer);
            break;
        default:
            result = FC_MakeRect(x, y, 0, 0);
            break;
    }

    return result;
}

// Getters

FC_FilterEnum FC_GetFilterMode(FC_Font* font)
{
    if(font == NULL)
        return FC_FILTER_NEAREST;

    return font->filter;
}

Uint16 FC_GetLineHeight(FC_Font* font)
{
    if(font == NULL)
        return 0;

    return font->height;
}

Uint16 FC_GetHeight(FC_Font* font, const char* formatted_text, ...)
{
    if(formatted_text == NULL || font == NULL)
        return 0;

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    Uint16 numLines = 1;
    const char* c;

    for (c = fc_buffer; *c != '\0'; c++)
    {
        if(*c == '\n')
            numLines++;
    }

    //   Actual height of letter region + line spacing
    return font->height*numLines + font->lineSpacing*(numLines - 1);  //height*numLines;
}

Uint16 FC_GetWidth(FC_Font* font, const char* formatted_text, ...)
{
    if(formatted_text == NULL || font == NULL)
        return 0;

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    const char* c;
    Uint16 width = 0;
    Uint16 bigWidth = 0;  // Allows for multi-line strings

    for (c = fc_buffer; *c != '\0'; c++)
    {
        if(*c == '\n')
        {
            bigWidth = bigWidth >= width? bigWidth : width;
            width = 0;
            continue;
        }

        FC_GlyphData glyph;
        Uint32 codepoint = FC_GetCodepointFromUTF8(&c, 1);
        if(FC_GetGlyphData(font, &glyph, codepoint) || FC_GetGlyphData(font, &glyph, ' '))
            width += glyph.rect.w;
    }
    bigWidth = bigWidth >= width? bigWidth : width;

    return bigWidth;
}

// If width == -1, use no width limit
FC_Rect FC_GetCharacterOffset(FC_Font* font, Uint16 position_index, int column_width, const char* formatted_text, ...)
{
    FC_Rect result = {0, 0, 1, FC_GetLineHeight(font)};
    FC_StringList *ls, *iter;
    int num_lines = 0;
    Uint8 done = 0;

    if(formatted_text == NULL || column_width == 0 || position_index == 0 || font == NULL)
        return result;

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    ls = FC_GetBufferFitToColumn(font, column_width, FC_MakeScale(1,1), 1);
    for(iter = ls; iter != NULL;)
    {
        char* line;
        int i = 0;
        FC_StringList* next_iter = iter->next;

        ++num_lines;
        for(line = iter->value; line != NULL && *line != '\0'; line = (char*)U8_next(line))
        {
            ++i;
            --position_index;
            if(position_index == 0)
            {
                // FIXME: Doesn't handle box-wrapped newlines correctly
                line = (char*)U8_next(line);
                line[0] = '\0';
                result.x = FC_GetWidth(font, "%s", iter->value);
                done = 1;
                break;
            }
        }
        if(done)
            break;

        // Prevent line wrapping if there are no more lines
        if(next_iter == NULL && !done)
            result.x = FC_GetWidth(font, "%s", iter->value);
        iter = next_iter;
    }
    FC_StringListFree(ls);

    if(num_lines > 1)
    {
        result.y = (num_lines - 1) * FC_GetLineHeight(font);
    }

    return result;
}

Uint16 FC_GetColumnHeight(FC_Font* font, Uint16 width, const char* formatted_text, ...)
{
    int y = 0;

    FC_StringList *ls, *iter;

    if(font == NULL)
        return 0;

    if(formatted_text == NULL || width == 0)
        return font->height;

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    ls = FC_GetBufferFitToColumn(font, width, FC_MakeScale(1,1), 0);
    for(iter = ls; iter != NULL; iter = iter->next)
    {
        y += FC_GetLineHeight(font);
    }
    FC_StringListFree(ls);

    return y;
}

static int FC_GetAscentFromCodepoint(FC_Font* font, Uint32 codepoint)
{
    FC_GlyphData glyph;

    if(font == NULL)
        return 0;

    // FIXME: Store ascent so we can return it here
    FC_GetGlyphData(font, &glyph, codepoint);
    return glyph.rect.h;
}

static int FC_GetDescentFromCodepoint(FC_Font* font, Uint32 codepoint)
{
    FC_GlyphData glyph;

    if(font == NULL)
        return 0;

    // FIXME: Store descent so we can return it here
    FC_GetGlyphData(font, &glyph, codepoint);
    return glyph.rect.h;
}

int FC_GetAscent(FC_Font* font, const char* formatted_text, ...)
{
    Uint32 codepoint;
    int max, ascent;
    const char* c;

    if(font == NULL)
        return 0;

    if(formatted_text == NULL)
        return font->ascent;

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    max = 0;
    c = fc_buffer;

    while(*c != '\0')
    {
        codepoint = FC_GetCodepointFromUTF8(&c, 1);
        if(codepoint != 0)
        {
            ascent = FC_GetAscentFromCodepoint(font, codepoint);
            if(ascent > max)
                max = ascent;
        }
        ++c;
    }
    return max;
}

int FC_GetDescent(FC_Font* font, const char* formatted_text, ...)
{
    Uint32 codepoint;
    int max, descent;
    const char* c;

    if(font == NULL)
        return 0;

    if(formatted_text == NULL)
        return font->descent;

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    max = 0;
    c = fc_buffer;

    while(*c != '\0')
    {
        codepoint = FC_GetCodepointFromUTF8(&c, 1);
        if(codepoint != 0)
        {
            descent = FC_GetDescentFromCodepoint(font, codepoint);
            if(descent > max)
                max = descent;
        }
        ++c;
    }
    return max;
}

int FC_GetBaseline(FC_Font* font)
{
    if(font == NULL)
        return 0;

    return font->baseline;
}

int FC_GetSpacing(FC_Font* font)
{
    if(font == NULL)
        return 0;

    return font->letterSpacing;
}

int FC_GetLineSpacing(FC_Font* font)
{
    if(font == NULL)
        return 0;

    return font->lineSpacing;
}

Uint16 FC_GetMaxWidth(FC_Font* font)
{
    if(font == NULL)
        return 0;

    return font->maxWidth;
}

SDL_Color FC_GetDefaultColor(FC_Font* font)
{
    if(font == NULL)
    {
        SDL_Color c = {0,0,0,255};
        return c;
    }

    return font->default_color;
}

Uint8 FC_InRect(float x, float y, FC_Rect input_rect)
{
    return (input_rect.x <= x && x <= input_rect.x + input_rect.w && input_rect.y <= y && y <= input_rect.y + input_rect.h);
}

// TODO: Make it work with alignment
Uint16 FC_GetPositionFromOffset(FC_Font* font, float x, float y, int column_width, FC_AlignEnum align, const char* formatted_text, ...)
{
    FC_StringList *ls, *iter;
    Uint8 done = 0;
    int height = FC_GetLineHeight(font);
    Uint16 position = 0;
    int current_x = 0;
    int current_y = 0;
    FC_GlyphData glyph_data;

    if(formatted_text == NULL || column_width == 0 || font == NULL)
        return 0;

    FC_EXTRACT_VARARGS(fc_buffer, formatted_text);

    ls = FC_GetBufferFitToColumn(font, column_width, FC_MakeScale(1,1), 1);
    for(iter = ls; iter != NULL; iter = iter->next)
    {
        char* line;

        for(line = iter->value; line != NULL && *line != '\0'; line = (char*)U8_next(line))
        {
            if(FC_GetGlyphData(font, &glyph_data, FC_GetCodepointFromUTF8((const char**)&line, 0)))
            {
                if(FC_InRect(x, y, FC_MakeRect(current_x, current_y, glyph_data.rect.w, glyph_data.rect.h)))
                {
                    done = 1;
                    break;
                }

                current_x += glyph_data.rect.w;
            }
            position++;
        }
        if(done)
            break;

        current_x = 0;
        current_y += height;
        if(y < current_y)
            break;
    }
    FC_StringListFree(ls);

    return position;
}

// Setters

void FC_SetFilterMode(FC_Font* font, FC_FilterEnum filter)
{
    if(font == NULL)
        return;

    if(font->filter != filter)
    {
        font->filter = filter;
    }
}


void FC_SetSpacing(FC_Font* font, int LetterSpacing)
{
    if(font == NULL)
        return;

    font->letterSpacing = LetterSpacing;
}

void FC_SetLineSpacing(FC_Font* font, int LineSpacing)
{
    if(font == NULL)
        return;

    font->lineSpacing = LineSpacing;
}

void FC_SetDefaultColor(FC_Font* font, SDL_Color color)
{
    if(font == NULL)
        return;

    font->default_color = color;
}
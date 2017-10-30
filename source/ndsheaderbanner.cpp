#include "ndsheaderbanner.h"

#include <stdio.h>
#include <malloc.h>
#include <unistd.h>

#include <string>
#include <vector>
using std::string;
using std::vector;
using std::wstring;

GX_TRANSFER_FORMAT gpuToGxFormat[13] = {
        GX_TRANSFER_FMT_RGBA8,
        GX_TRANSFER_FMT_RGB8,
        GX_TRANSFER_FMT_RGB5A1,
        GX_TRANSFER_FMT_RGB565,
        GX_TRANSFER_FMT_RGBA4,
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8, // Unsupported
        GX_TRANSFER_FMT_RGBA8  // Unsupported
};

/**
 * Convert a color from NDS BGR555 to RGB5A1.
 * @param px16 BGR555 color value.
 * @return RGB5A1 color.
 */
static inline u16 BGR555_to_RGB5A1(u16 px16) {
	// BGR555: xBBBBBGG GGGRRRRR
	// RGB565: RRRRRGGG GGGBBBBB
	// RGB5A1: RRRRRGGG GGBBBBBA
	return   (px16 << 11) |	1 |		// Red (and alpha)
		((px16 <<  1) & 0x07C0) |	// Green
		((px16 >>  9) & 0x003E);	// Blue
}

/**
 * Get the icon from an NDS banner.
 * @param binFile NDS banner.
 * @return Icon texture. (NULL on error)
 */
void* grabIcon(const sNDSBanner* ndsBanner) {
	// Convert the palette from BGR555 to RGB5A1.
	// (We need to ensure the MSB is set for all except
	// color 0, which is transparent.)
	u16 palette[16];
	palette[0] = 0;	// Color 0 is always transparent.
	for (int i = 16-1; i > 0; i--) {
		// Convert from NDS BGR555 to RGB5A1.
		// NOTE: The GPU expects byteswapped data.
		palette[i] = BGR555_to_RGB5A1(ndsBanner->palette[i]);
	}

	static const int width = 32;
	static const int height = 32;
	u8 icon[32 * 32 * 2];
	for(u32 x = 0; x < 32; x++) {
		for(u32 y = 0; y < 32; y++) {
			u32 srcPos = (((y >> 3) * 4 + (x >> 3)) * 8 + (y & 7)) * 4 + ((x & 7) >> 1);
			u32 srcShift = (x & 1) * 4;
			u16 srcPx = palette[(ndsBanner->icon[srcPos] >> srcShift) & 0xF];

			u32 dstPos = (y * 32 + x) * 2;
			icon[dstPos + 0] = (u8) (srcPx & 0xFF);
			icon[dstPos + 1] = (u8) ((srcPx >> 8) & 0xFF);
		}
	}

    u32 pixelSize = sizeof(icon) / width / height;

    u8* textureData = (u8*)linearAlloc(64 * 64 * pixelSize);

    memset(textureData, 0, 64 * 64 * pixelSize);

    for(u32 x = 0; x < width; x++) {
        for(u32 y = 0; y < height; y++) {
            u32 dataPos = (y * width + x) * pixelSize;
            u32 texPos = (y * 64 + x) * pixelSize;

            for(u32 i = 0; i < pixelSize; i++) {
                textureData[texPos + i] = ((u8*) icon)[dataPos + i];
            }
        }
    }

	return textureData;
}

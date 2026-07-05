/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

// Batched GPU quad renderer — rects, images and color textures draw through
// one pipeline (pos+uv+color vertices, texture x vertex color, a 1x1 white
// texture makes solid rects the same path). Image decode: libpng / libjpeg-turbo
// (magic-byte sniffed) -> RGBA -> GPU upload.
// Text — FreeType over the shared system fonts (Standard + NintendoExt + CJK
// fallbacks) and the bundled Space Mono, with per-glyph R8 atlas pages drawn
// through the same quad batcher. The metrics reproduce the previously vendored
// SDL_FontCache exactly (cell-width pen advance, line height =
// ceil(TTF height * 1.2)) so layouts match the retired SDL backend
// pixel-for-pixel.

#include "DekoHelper.hpp"
#include "gfx/CCmdMemRing.h"
#include "gfx/CDescriptorSet.h"
#include "gfx/CMemPool.h"
#include "gfx/CShader.h"
#include "logging.hpp"
#include "main.hpp"
#include <array>
#include <cmath>
#include <deko3d.hpp>
#include <optional>
#include <png.h>
#include <turbojpeg.h>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace {
    constexpr unsigned FB_NUM = 2;
    // The UI is authored in a fixed 1280x720 logical space (the shaders map it
    // to NDC, so it is resolution-independent). Handheld renders 1:1; docked
    // renders the same UI into a 1920x1080 framebuffer (viewport upscale) for a
    // crisp 1080p output. The swapchain/framebuffers are recreated on mode
    // change; the CPU-side layout never changes.
    constexpr u32 FB_WIDTH_HANDHELD  = 1280;
    constexpr u32 FB_HEIGHT_HANDHELD = 720;
    constexpr u32 FB_WIDTH_DOCKED    = 1920;
    constexpr u32 FB_HEIGHT_DOCKED   = 1080;
    constexpr u32 CMDMEM_SLICE       = 0x40000; // per-frame dynamic command memory
    constexpr u32 IMAGEPOOL_SZ       = 16 * 1024 * 1024;
    constexpr u32 DATAPOOL_SZ        = 4 * 1024 * 1024;
    constexpr u32 CODEPOOL_SZ        = 128 * 1024;

    constexpr u32 MAX_IMAGES     = 1024; // image descriptor slots (icons, avatars, assets)
    constexpr u32 MAX_QUADS      = 16384;
    constexpr u32 MAX_ROUNDQUADS = 4096; // SDF rounded-rect quads per frame

    // One UI quad vertex: logical-720p position, texcoord, straight-alpha
    // color (RGBA8, unpacked to floats by the Unorm vertex attribute).
    struct Vertex {
        float x, y;
        float u, v;
        u8 rgba[4];
    };

    constexpr u32 VERTS_PER_QUAD = 6;
    constexpr u32 MAX_VERTS      = MAX_QUADS * VERTS_PER_QUAD;
    constexpr u32 VTX_SLICE_SZ   = MAX_VERTS * sizeof(Vertex);

    // Which pipeline a batch draws through (they use different vertex formats
    // and shaders); a batch is homogeneous, so a mode change forces a flush.
    enum class BatchMode { Quad, Round };

    constexpr std::array VertexAttribState = {
        DkVtxAttribState{0, 0, offsetof(Vertex, x), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex, u), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex, rgba), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
    };
    constexpr std::array VertexBufferState = {
        DkVtxBufferState{sizeof(Vertex), 0},
    };

    // One SDF rounded-rect vertex: logical position, local offset from the rect
    // centre (pixels), the rect params the fragment shader needs, and color.
    struct RoundVertex {
        float x, y;
        float lx, ly;
        float halfW, halfH, radius, thickness;
        u8 rgba[4];
    };

    constexpr std::array RoundVertexAttribState = {
        DkVtxAttribState{0, 0, offsetof(RoundVertex, x), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(RoundVertex, lx), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(RoundVertex, halfW), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(RoundVertex, rgba), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
    };
    constexpr std::array RoundVertexBufferState = {
        DkVtxBufferState{sizeof(RoundVertex), 0},
    };

    constexpr u32 ROUND_MAX_VERTS = MAX_ROUNDQUADS * VERTS_PER_QUAD;
    constexpr u32 ROUND_SLICE_SZ  = ROUND_MAX_VERTS * sizeof(RoundVertex);

    dk::UniqueDevice s_device;
    dk::UniqueQueue s_queue;
    std::optional<CMemPool> s_poolImages;
    std::optional<CMemPool> s_poolData;
    std::optional<CMemPool> s_poolCode;
    dk::UniqueCmdBuf s_cmdbuf;
    std::optional<CCmdMemRing<FB_NUM>> s_cmdRing;
    CMemPool::Handle s_fbMem[FB_NUM];
    dk::Image s_framebuffers[FB_NUM];
    dk::UniqueSwapchain s_swapchain;
    u32 s_fbWidth                = FB_WIDTH_HANDHELD;
    u32 s_fbHeight               = FB_HEIGHT_HANDHELD;
    AppletOperationMode s_opMode = AppletOperationMode_Handheld;

    CShader s_vertexShader;
    CShader s_fragmentShader;
    CShader s_roundVertexShader;
    CShader s_roundFragmentShader;
    std::optional<CDescriptorSet<MAX_IMAGES>> s_imageDescs;
    std::optional<CDescriptorSet<1>> s_samplerDescs;
    std::vector<u32> s_freeDescIds; // recycled descriptor slots
    u32 s_nextDescId = 0;           // high-water mark of never-used slots

    // Per-frame vertex memory: FB_NUM slices in lockstep with s_cmdRing, so
    // the ring's fence wait in begin() also protects the vertex slice reuse.
    CMemPool::Handle s_vtxMem;
    unsigned s_vtxSlice  = 0;
    Vertex* s_vtxBase    = nullptr;
    u32 s_vtxCount       = 0;
    bool s_vtxOverflowed = false;

    // Parallel per-frame ring for SDF rounded-rect vertices (same FB_NUM
    // slices, cycled by s_vtxSlice in lockstep with the quad ring).
    CMemPool::Handle s_roundVtxMem;
    RoundVertex* s_roundBase = nullptr;
    u32 s_roundCount         = 0;
    bool s_roundOverflowed   = false;

    // Current batch: contiguous vertex range in one mode's buffer sharing
    // texture + blend state.
    BatchMode s_batchMode = BatchMode::Quad;
    u32 s_batchStart      = 0; // index into the active mode's vertex buffer
    u32 s_batchDescId     = UINT32_MAX;
    bool s_batchOpaque    = false;
    bool s_blendBound     = true; // blending state currently bound on the cmdbuf
    int s_boundMode       = -1;   // BatchMode whose pipeline is bound (-1 = none)

    // Swapchain slot acquired for the frame being recorded; -1 outside a frame.
    int s_slot = -1;

    Texture* s_white    = nullptr; // 1x1 white: solid rects sample this
    Texture* s_star     = nullptr;
    Texture* s_checkbox = nullptr;

    // ---- text (phase 3) ----

    constexpr u32 ATLAS_SIZE = 1024; // R8 glyph atlas page dimension
    constexpr u32 ATLAS_PAD  = 1;    // gap between glyphs (linear filtering bleed)

    // 26.6 fixed-point rounding, same macros SDL_ttf uses for glyph metrics.
    constexpr int ftFloor(long x)
    {
        return (int)(x >> 6);
    }
    constexpr int ftCeil(long x)
    {
        return (int)(((x + 63) & -64) / 64);
    }

    // One rasterized glyph in an atlas page. Draw semantics mirror
    // SDL_FontCache: the bitmap is placed at (penX + bmpDX, lineTopY + bmpDY)
    // and the pen advances by cellW (the width of the single-char surface
    // SDL_ttf would have rendered — NOT the FreeType advance).
    struct GlyphData {
        u16 page; // index into s_atlasPages; 0xFFFF = no bitmap (space/empty)
        u16 atlasX, atlasY;
        u16 bmpW, bmpH;
        s16 bmpDX, bmpDY;
        u16 cellW;
    };
    constexpr u16 GLYPH_NO_PAGE = 0xFFFF;

    // Shelf-packed R8 atlas page, shared by all font instances.
    struct AtlasPage {
        CMemPool::Handle mem;
        dk::Image image;
        u32 descId;
        u32 shelfX, shelfY, shelfH;
    };

    // Per-(family, requested size) state; glyphs rasterize lazily on first use.
    struct FontInstance {
        int px       = 0; // scaledFontPx(requested size)
        int fcHeight = 0; // FC line height: ceil((ascent - descent + 1) * 1.2)
        std::unordered_map<u32, GlyphData> glyphs;
    };

    FT_Library s_ftLibrary = nullptr;
    // Sans face chain, FC lookup order: [0] Standard, [1] NintendoExt (PUA
    // codepoints only), [2..] CJK/Korean fallbacks.
    FT_Face s_sansFaces[6] = {};
    int s_numSansFaces     = 0;
    FT_Face s_monoFace     = nullptr;
    void* s_monoData       = nullptr; // romfs Space Mono, kept alive for the face

    std::unordered_map<int, FontInstance> s_sansInstances;
    std::unordered_map<int, FontInstance> s_monoInstances;
    std::vector<AtlasPage> s_atlasPages;

    // Same ~1.2x size bump the retired SDL backend applied: both drawing and
    // measurement resolve through it, so it is invisible to callers.
    constexpr int scaledFontPx(int size)
    {
        return (size * 6 + 2) / 5;
    }
}

// deko3d implementation of the opaque backend handle declared in gfxtypes.hpp.
struct Texture {
    CMemPool::Handle mem;
    dk::Image image;
    u32 width;
    u32 height;
    u32 descId;
    bool opaque; // draw without alpha blending (Gfx::SetTextureOpaque)
};

// Record commands into a throwaway cmdbuf, submit, and wait for completion.
// Used for texture uploads and one-shot state setup outside the frame ring.
template <typename F>
static void oneShotCommands(F&& record)
{
    dk::UniqueCmdBuf cmd    = dk::CmdBufMaker{s_device}.create();
    CMemPool::Handle cmdMem = s_poolData->allocate(DK_MEMBLOCK_ALIGNMENT);
    cmd.addMemory(cmdMem.getMemBlock(), cmdMem.getOffset(), cmdMem.getSize());
    record(cmd);
    s_queue.submitCommands(cmd.finishList());
    s_queue.waitIdle();
    cmdMem.destroy();
}

// Grab a free image-descriptor slot; returns false (with a log) when the
// table is exhausted.
static bool allocDescId(u32& descId)
{
    if (!s_freeDescIds.empty()) {
        descId = s_freeDescIds.back();
        s_freeDescIds.pop_back();
        return true;
    }
    if (s_nextDescId < MAX_IMAGES) {
        descId = s_nextDescId++;
        return true;
    }
    Logging::error("deko3d: image descriptor table exhausted ({} slots).", MAX_IMAGES);
    return false;
}

// Upload a tightly-packed RGBA8 pixel buffer into a new GPU texture and
// publish its image descriptor. Returns nullptr (with a log) when the
// descriptor table is exhausted.
static Texture* createTexture(const u8* pixels, u32 width, u32 height)
{
    u32 descId;
    if (!allocDescId(descId)) {
        return nullptr;
    }

    dk::ImageLayout layout;
    dk::ImageLayoutMaker{s_device}.setFlags(0).setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(width, height).initialize(layout);

    Texture* texture = new Texture{};
    texture->mem     = s_poolImages->allocate(layout.getSize(), layout.getAlignment());
    texture->image.initialize(layout, texture->mem.getMemBlock(), texture->mem.getOffset());
    texture->width  = width;
    texture->height = height;
    texture->descId = descId;
    texture->opaque = false;

    const u32 dataSize       = width * height * 4;
    CMemPool::Handle scratch = s_poolData->allocate(dataSize, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    memcpy(scratch.getCpuAddr(), pixels, dataSize);

    dk::ImageView view{texture->image};
    dk::ImageDescriptor descriptor;
    descriptor.initialize(view);

    oneShotCommands([&](dk::CmdBuf& cmd) {
        cmd.copyBufferToImage({scratch.getGpuAddr()}, view, {0, 0, 0, width, height, 1});
        s_imageDescs->update(cmd, descId, descriptor);
    });

    scratch.destroy();
    return texture;
}

static u8* decodePNGToRGBA(const u8* data, size_t size, u32& width, u32& height)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_memory(&image, data, size) == 0) {
        return nullptr;
    }
    image.format = PNG_FORMAT_RGBA;
    u8* pixels   = (u8*)malloc(PNG_IMAGE_SIZE(image));
    if (!pixels || png_image_finish_read(&image, NULL, pixels, 0, NULL) == 0) {
        free(pixels);
        png_image_free(&image);
        return nullptr;
    }
    width  = image.width;
    height = image.height;
    return pixels;
}

static u8* decodeJPEGToRGBA(const u8* data, size_t size, u32& width, u32& height)
{
    tjhandle decompressor = tjInitDecompress();
    if (!decompressor) {
        return nullptr;
    }
    int w = 0, h = 0, samp = 0;
    u8* pixels = nullptr;
    if (tjDecompressHeader2(decompressor, (u8*)data, size, &w, &h, &samp) == 0 && w > 0 && h > 0) {
        pixels = (u8*)malloc((size_t)w * h * 4);
        if (pixels && tjDecompress2(decompressor, (u8*)data, size, pixels, w, 0, h, TJPF_RGBA, TJFLAG_ACCURATEDCT) != 0) {
            free(pixels);
            pixels = nullptr;
        }
    }
    tjDestroy(decompressor);
    if (pixels) {
        width  = w;
        height = h;
    }
    return pixels;
}

// Decode a PNG or JPEG (sniffed by magic bytes) into a malloc'd RGBA8 buffer,
// reproducing the retired SDL backend's black colorkey: fully-opaque
// pure-black pixels become transparent (SDL_SetColorKey only ever matched the
// opaque mapping of (0,0,0)). Caller frees.
static u8* decodeToRGBA(const u8* data, size_t size, u32& width, u32& height)
{
    if (!data || size < 4) {
        return nullptr;
    }
    u8* pixels = nullptr;
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        pixels = decodePNGToRGBA(data, size, width, height);
    }
    else if (data[0] == 0xFF && data[1] == 0xD8) {
        pixels = decodeJPEGToRGBA(data, size, width, height);
    }
    if (pixels) {
        for (u32 i = 0; i < width * height; i++) {
            u8* px = pixels + (size_t)i * 4;
            if (px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 255) {
                px[3] = 0;
            }
        }
    }
    return pixels;
}

// Read a whole file (romfs assets) and decode it. Caller frees.
static u8* decodeFileToRGBA(const char* path, u32& width, u32& height)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    u8* data   = size > 0 ? (u8*)malloc(size) : nullptr;
    u8* pixels = nullptr;
    if (data && fread(data, 1, size, f) == (size_t)size) {
        pixels = decodeToRGBA(data, size, width, height);
    }
    free(data);
    fclose(f);
    return pixels;
}

// Pick the framebuffer resolution for the current applet operation mode:
// docked outputs native 1080p, handheld 720p.
static void chooseFbSize(AppletOperationMode mode)
{
    if (mode == AppletOperationMode_Console) {
        s_fbWidth  = FB_WIDTH_DOCKED;
        s_fbHeight = FB_HEIGHT_DOCKED;
    }
    else {
        s_fbWidth  = FB_WIDTH_HANDHELD;
        s_fbHeight = FB_HEIGHT_HANDHELD;
    }
    s_opMode = mode;
}

// (Re)create the swapchain framebuffers at the current s_fbWidth/s_fbHeight.
static void createFramebuffers(void)
{
    dk::ImageLayout fbLayout;
    dk::ImageLayoutMaker{s_device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(s_fbWidth, s_fbHeight)
        .initialize(fbLayout);

    std::array<DkImage const*, FB_NUM> fbArray;
    for (unsigned i = 0; i < FB_NUM; i++) {
        s_fbMem[i] = s_poolImages->allocate(fbLayout.getSize(), fbLayout.getAlignment());
        s_framebuffers[i].initialize(fbLayout, s_fbMem[i].getMemBlock(), s_fbMem[i].getOffset());
        fbArray[i] = &s_framebuffers[i];
    }
    s_swapchain = dk::SwapchainMaker{s_device, nwindowGetDefault(), fbArray}.create();
}

static void destroyFramebuffers(void)
{
    s_swapchain.destroy();
    for (unsigned i = 0; i < FB_NUM; i++) {
        s_fbMem[i].destroy();
    }
}

bool Gfx::Init(void)
{
    s_device = dk::DeviceMaker{}.create();
    s_queue  = dk::QueueMaker{s_device}.setFlags(DkQueueFlags_Graphics).create();

    s_poolImages.emplace(s_device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, IMAGEPOOL_SZ);
    s_poolData.emplace(s_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, DATAPOOL_SZ);
    s_poolCode.emplace(s_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, CODEPOOL_SZ);

    s_cmdbuf = dk::CmdBufMaker{s_device}.create();
    s_cmdRing.emplace();
    if (!s_cmdRing->allocate(*s_poolData, CMDMEM_SLICE)) {
        Logging::error("deko3d: failed to allocate command memory ring.");
        return false;
    }

    if (!s_vertexShader.load(*s_poolCode, "romfs:/shaders/quad_vsh.dksh") || !s_fragmentShader.load(*s_poolCode, "romfs:/shaders/quad_fsh.dksh")) {
        Logging::error("deko3d: failed to load romfs:/shaders/quad_{vsh,fsh}.dksh.");
        return false;
    }
    if (!s_roundVertexShader.load(*s_poolCode, "romfs:/shaders/round_vsh.dksh") ||
        !s_roundFragmentShader.load(*s_poolCode, "romfs:/shaders/round_fsh.dksh")) {
        Logging::error("deko3d: failed to load romfs:/shaders/round_{vsh,fsh}.dksh.");
        return false;
    }

    s_imageDescs.emplace();
    s_samplerDescs.emplace();
    if (!s_imageDescs->allocate(*s_poolData) || !s_samplerDescs->allocate(*s_poolData)) {
        Logging::error("deko3d: failed to allocate descriptor sets.");
        return false;
    }

    s_vtxMem = s_poolData->allocate(FB_NUM * VTX_SLICE_SZ, alignof(Vertex));
    if (!s_vtxMem) {
        Logging::error("deko3d: failed to allocate vertex ring.");
        return false;
    }
    s_roundVtxMem = s_poolData->allocate(FB_NUM * ROUND_SLICE_SZ, alignof(RoundVertex));
    if (!s_roundVtxMem) {
        Logging::error("deko3d: failed to allocate rounded-rect vertex ring.");
        return false;
    }

    chooseFbSize(appletGetOperationMode());
    createFramebuffers();

    // Persistent queue state: linear-clamp sampler in slot 0, descriptor sets
    // bound once (deko3d queues retain bound state across submissions).
    oneShotCommands([&](dk::CmdBuf& cmd) {
        dk::Sampler sampler;
        sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
        sampler.setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge);
        dk::SamplerDescriptor samplerDescriptor;
        samplerDescriptor.initialize(sampler);
        s_samplerDescs->update(cmd, 0, samplerDescriptor);
        s_imageDescs->bindForImages(cmd);
        s_samplerDescs->bindForSamplers(cmd);
    });

    const u8 whitePixel[4] = {255, 255, 255, 255};
    s_white                = createTexture(whitePixel, 1, 1);
    if (!s_white) {
        return false;
    }

    Gfx::LoadImage(&s_star, "romfs:/star.png");
    // The multi-select badge is accent-filled with a white check on top; the
    // checkbox asset is a black-on-transparent checkmark. The SDL backend
    // tinted it via SDL_SetTextureColorMod(white); here the tint is baked into
    // the pixels before upload.
    u32 cbW = 0, cbH = 0;
    u8* cbPixels = decodeFileToRGBA("romfs:/checkbox.png", cbW, cbH);
    if (cbPixels) {
        for (u32 i = 0; i < cbW * cbH; i++) {
            u8* px = cbPixels + (size_t)i * 4;
            px[0] = px[1] = px[2] = 255;
        }
        s_checkbox = createTexture(cbPixels, cbW, cbH);
        free(cbPixels);
    }

    // ---- fonts (phase 3) ----
    if (FT_Init_FreeType(&s_ftLibrary) != 0) {
        Logging::error("deko3d: FT_Init_FreeType failed.");
        return false;
    }

    PlFontData fd;
    if (R_SUCCEEDED(plGetSharedFontByType(&fd, PlSharedFontType_Standard)) && fd.address) {
        FT_New_Memory_Face(s_ftLibrary, (const FT_Byte*)fd.address, fd.size, 0, &s_sansFaces[0]);
    }
    if (R_SUCCEEDED(plGetSharedFontByType(&fd, PlSharedFontType_NintendoExt)) && fd.address) {
        FT_New_Memory_Face(s_ftLibrary, (const FT_Byte*)fd.address, fd.size, 0, &s_sansFaces[1]);
    }
    if (!s_sansFaces[0]) {
        Logging::error("deko3d: failed to open the shared system font.");
    }
    s_numSansFaces                                    = 2;
    static constexpr PlSharedFontType fallbackTypes[] = {
        PlSharedFontType_KO,
        PlSharedFontType_ChineseSimplified,
        PlSharedFontType_ExtChineseSimplified,
        PlSharedFontType_ChineseTraditional,
    };
    for (PlSharedFontType type : fallbackTypes) {
        if (s_numSansFaces >= (int)(sizeof(s_sansFaces) / sizeof(s_sansFaces[0]))) {
            break;
        }
        if (R_SUCCEEDED(plGetSharedFontByType(&fd, type)) && fd.address &&
            FT_New_Memory_Face(s_ftLibrary, (const FT_Byte*)fd.address, fd.size, 0, &s_sansFaces[s_numSansFaces]) == 0) {
            s_numSansFaces++;
        }
    }

    // Bundled monospace font: the file buffer must outlive the face
    // (FT_New_Memory_Face does not copy), so it is kept in s_monoData.
    FILE* monoFile = fopen("romfs:/fonts/SpaceMono-Regular.ttf", "rb");
    if (monoFile != NULL) {
        fseek(monoFile, 0, SEEK_END);
        long monoSize = ftell(monoFile);
        fseek(monoFile, 0, SEEK_SET);
        if (monoSize > 0) {
            void* buf = malloc((size_t)monoSize);
            if (buf != NULL && fread(buf, 1, (size_t)monoSize, monoFile) == (size_t)monoSize &&
                FT_New_Memory_Face(s_ftLibrary, (const FT_Byte*)buf, monoSize, 0, &s_monoFace) == 0) {
                s_monoData = buf;
            }
            else {
                free(buf);
                s_monoFace = nullptr;
            }
        }
        fclose(monoFile);
    }
    if (!s_monoFace) {
        Logging::error("Failed to load romfs:/fonts/SpaceMono-Regular.ttf, falling back to the system font.");
    }

    Gfx::GetTextDimensions(13, "...", &g_username_dotsize, NULL);

    return true;
}

void Gfx::Exit(void)
{
    if (s_queue) {
        s_queue.waitIdle();
    }
    Gfx::DestroyTexture(s_checkbox);
    Gfx::DestroyTexture(s_star);
    Gfx::DestroyTexture(s_white);
    for (AtlasPage& page : s_atlasPages) {
        page.mem.destroy();
        s_freeDescIds.push_back(page.descId);
    }
    s_atlasPages.clear();
    s_sansInstances.clear();
    s_monoInstances.clear();
    for (FT_Face& face : s_sansFaces) {
        if (face) {
            FT_Done_Face(face);
            face = nullptr;
        }
    }
    if (s_monoFace) {
        FT_Done_Face(s_monoFace);
        s_monoFace = nullptr;
    }
    if (s_ftLibrary) {
        FT_Done_FreeType(s_ftLibrary);
        s_ftLibrary = nullptr;
    }
    free(s_monoData);
    s_monoData = nullptr;
    destroyFramebuffers();
    s_vtxMem.destroy();
    s_roundVtxMem.destroy();
    s_samplerDescs.reset();
    s_imageDescs.reset();
    s_cmdRing.reset();
    s_cmdbuf.destroy();
    s_poolCode.reset();
    s_poolData.reset();
    s_poolImages.reset();
    s_queue.destroy();
    s_device.destroy();
}

// First draw call of the frame: acquire the swapchain slot, start recording
// into this frame's command memory slice, bind the framebuffer and the full
// static state of the quad pipeline.
static void frameBegin(void)
{
    if (s_slot >= 0) {
        return;
    }
    // Applet operation mode can change between frames (dock/undock); recreate
    // the swapchain at the new resolution before starting the frame.
    AppletOperationMode mode = appletGetOperationMode();
    if (mode != s_opMode) {
        s_queue.waitIdle();
        destroyFramebuffers();
        chooseFbSize(mode);
        createFramebuffers();
    }

    s_slot = s_queue.acquireImage(s_swapchain);
    s_cmdRing->begin(s_cmdbuf);

    dk::ImageView colorTarget{s_framebuffers[s_slot]};
    s_cmdbuf.bindRenderTargets(&colorTarget);
    s_cmdbuf.setViewports(0, {{0.0f, 0.0f, (float)s_fbWidth, (float)s_fbHeight, 0.0f, 1.0f}});
    s_cmdbuf.setScissors(0, {{0, 0, s_fbWidth, s_fbHeight}});

    dk::RasterizerState rasterizerState;
    rasterizerState.setCullMode(DkFace_None);
    dk::DepthStencilState depthStencilState;
    depthStencilState.setDepthTestEnable(false);
    depthStencilState.setDepthWriteEnable(false);
    dk::ColorState colorState;
    colorState.setBlendEnable(0, true);
    dk::ColorWriteState colorWriteState;
    dk::BlendState blendState;
    // SDL_BLENDMODE_BLEND: straight-alpha src-over.
    blendState.setFactors(DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);

    s_cmdbuf.bindRasterizerState(rasterizerState);
    s_cmdbuf.bindDepthStencilState(depthStencilState);
    s_cmdbuf.bindColorState(colorState);
    s_cmdbuf.bindColorWriteState(colorWriteState);
    s_cmdbuf.bindBlendStates(0, blendState);
    s_blendBound = true;

    // Pipeline (shaders + vertex stream) is bound lazily per batch by bindMode,
    // so the first draw of the frame always establishes it.
    s_boundMode       = -1;
    s_vtxBase         = (Vertex*)((u8*)s_vtxMem.getCpuAddr() + s_vtxSlice * VTX_SLICE_SZ);
    s_roundBase       = (RoundVertex*)((u8*)s_roundVtxMem.getCpuAddr() + s_vtxSlice * ROUND_SLICE_SZ);
    s_vtxCount        = 0;
    s_roundCount      = 0;
    s_batchMode       = BatchMode::Quad;
    s_batchStart      = 0;
    s_batchDescId     = UINT32_MAX;
    s_vtxOverflowed   = false;
    s_roundOverflowed = false;
}

// Bind the shaders + vertex stream for `mode` (only when it differs from what
// is already bound on the cmdbuf).
static void bindMode(BatchMode mode)
{
    if ((int)mode == s_boundMode) {
        return;
    }
    if (mode == BatchMode::Quad) {
        s_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, {s_vertexShader, s_fragmentShader});
        s_cmdbuf.bindVtxBuffer(0, s_vtxMem.getGpuAddr() + s_vtxSlice * VTX_SLICE_SZ, VTX_SLICE_SZ);
        s_cmdbuf.bindVtxAttribState(VertexAttribState);
        s_cmdbuf.bindVtxBufferState(VertexBufferState);
    }
    else {
        s_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, {s_roundVertexShader, s_roundFragmentShader});
        s_cmdbuf.bindVtxBuffer(0, s_roundVtxMem.getGpuAddr() + s_vtxSlice * ROUND_SLICE_SZ, ROUND_SLICE_SZ);
        s_cmdbuf.bindVtxAttribState(RoundVertexAttribState);
        s_cmdbuf.bindVtxBufferState(RoundVertexBufferState);
    }
    s_boundMode = (int)mode;
}

// Emit the pending batch (if any) as one draw call, binding its pipeline /
// texture and switching blending on/off as needed.
static void flushBatch(void)
{
    const u32 activeCount = s_batchMode == BatchMode::Quad ? s_vtxCount : s_roundCount;
    const u32 count       = activeCount - s_batchStart;
    if (count == 0 || (s_batchMode == BatchMode::Quad && s_batchDescId == UINT32_MAX)) {
        return;
    }
    bindMode(s_batchMode);
    // Rounded rects always blend (their coverage lives in the alpha channel).
    const bool wantBlend = s_batchMode == BatchMode::Round ? true : !s_batchOpaque;
    if (wantBlend != s_blendBound) {
        dk::ColorState colorState;
        colorState.setBlendEnable(0, wantBlend);
        s_cmdbuf.bindColorState(colorState);
        s_blendBound = wantBlend;
    }
    if (s_batchMode == BatchMode::Quad) {
        s_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(s_batchDescId, 0));
    }
    s_cmdbuf.draw(DkPrimitive_Triangles, count, 1, s_batchStart, 0);
    s_batchStart = activeCount;
}

// Switch the active batch when its key (mode / texture / blend) changes,
// flushing the old one and anchoring the new batch's start in its buffer.
static void beginBatch(BatchMode mode, u32 descId, bool opaque)
{
    if (mode != s_batchMode || descId != s_batchDescId || opaque != s_batchOpaque) {
        flushBatch();
        s_batchMode   = mode;
        s_batchDescId = descId;
        s_batchOpaque = opaque;
        s_batchStart  = mode == BatchMode::Quad ? s_vtxCount : s_roundCount;
    }
}

static void pushQuadRaw(u32 descId, bool opaque, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color color)
{
    frameBegin();
    beginBatch(BatchMode::Quad, descId, opaque);
    if (s_vtxCount + VERTS_PER_QUAD > MAX_VERTS) {
        if (!s_vtxOverflowed) {
            Logging::error("deko3d: vertex ring full ({} quads), dropping draws this frame.", MAX_QUADS);
            s_vtxOverflowed = true;
        }
        return;
    }
    const Vertex tl{x, y, u0, v0, {color.r, color.g, color.b, color.a}};
    const Vertex tr{x + w, y, u1, v0, {color.r, color.g, color.b, color.a}};
    const Vertex bl{x, y + h, u0, v1, {color.r, color.g, color.b, color.a}};
    const Vertex br{x + w, y + h, u1, v1, {color.r, color.g, color.b, color.a}};
    Vertex* out = s_vtxBase + s_vtxCount;
    out[0]      = tl;
    out[1]      = bl;
    out[2]      = br;
    out[3]      = tl;
    out[4]      = br;
    out[5]      = tr;
    s_vtxCount += VERTS_PER_QUAD;
}

static void pushQuad(const Texture* texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color color)
{
    pushQuadRaw(texture->descId, texture->opaque, x, y, w, h, u0, v0, u1, v1, color);
}

// Emit one SDF rounded-rect quad. The geometry is expanded by 1px on every
// side so the antialiased edge is not clipped; local coordinates run from the
// rect centre so the fragment shader can evaluate the distance field.
// thickness <= 0 fills; thickness > 0 draws a ring of that width.
static void pushRound(float cx, float cy, float halfW, float halfH, float radius, float thickness, Color color)
{
    frameBegin();
    beginBatch(BatchMode::Round, 0, false);
    if (s_roundCount + VERTS_PER_QUAD > ROUND_MAX_VERTS) {
        if (!s_roundOverflowed) {
            Logging::error("deko3d: rounded-rect ring full ({} quads), dropping draws this frame.", MAX_ROUNDQUADS);
            s_roundOverflowed = true;
        }
        return;
    }
    const float m    = 1.0f; // AA margin
    const float lx   = halfW + m;
    const float ly   = halfH + m;
    const u8 rgba[4] = {color.r, color.g, color.b, color.a};
    const RoundVertex tl{cx - lx, cy - ly, -lx, -ly, halfW, halfH, radius, thickness, {rgba[0], rgba[1], rgba[2], rgba[3]}};
    const RoundVertex tr{cx + lx, cy - ly, lx, -ly, halfW, halfH, radius, thickness, {rgba[0], rgba[1], rgba[2], rgba[3]}};
    const RoundVertex bl{cx - lx, cy + ly, -lx, ly, halfW, halfH, radius, thickness, {rgba[0], rgba[1], rgba[2], rgba[3]}};
    const RoundVertex br{cx + lx, cy + ly, lx, ly, halfW, halfH, radius, thickness, {rgba[0], rgba[1], rgba[2], rgba[3]}};
    RoundVertex* out = s_roundBase + s_roundCount;
    out[0]           = tl;
    out[1]           = bl;
    out[2]           = br;
    out[3]           = tl;
    out[4]           = br;
    out[5]           = tr;
    s_roundCount += VERTS_PER_QUAD;
}

void Gfx::ClearScreen(Color color)
{
    frameBegin();
    flushBatch(); // preserve painter's order if anything was already batched
    s_cmdbuf.clearColor(0, DkColorMask_RGBA, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

void Gfx::Render(void)
{
    // Anchor to app start: armTicksToNs is nanoseconds since boot, which after
    // weeks of console uptime exceeds float's 24-bit mantissa and quantizes the
    // pulsing outline to ~1 s steps. Take the tick delta before the float cast.
    static const u64 s_startTick = armGetSystemTick();
    g_currentTime                = armTicksToNs(armGetSystemTick() - s_startTick) / 1000000000.0f;
    if (s_slot < 0) {
        return;
    }
    flushBatch();
    DkCmdList list = s_cmdRing->end(s_cmdbuf);
    s_queue.submitCommands(list);
    s_queue.presentImage(s_swapchain, s_slot);
    s_slot     = -1;
    s_vtxSlice = (s_vtxSlice + 1) % FB_NUM;
}

void Gfx::DrawRect(int x, int y, int w, int h, Color color)
{
    pushQuad(s_white, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void Gfx::FillRoundRect(int x, int y, int w, int h, float radius, Color color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;
    const float r     = std::max(0.0f, std::min(radius, std::min(halfW, halfH)));
    pushRound(x + halfW, y + halfH, halfW, halfH, r, 0.0f, color);
}

void Gfx::StrokeRoundRect(int x, int y, int w, int h, float radius, float thickness, Color color)
{
    if (w <= 0 || h <= 0 || thickness <= 0.0f) {
        return;
    }
    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;
    const float r     = std::max(0.0f, std::min(radius, std::min(halfW, halfH)));
    pushRound(x + halfW, y + halfH, halfW, halfH, r, thickness, color);
}

// ---- text engine (phase 3) ----

// Decode one UTF-8 sequence and advance *ptr past it. Truncated/invalid
// trail bytes end the sequence early (garbage in, garbage codepoint out —
// same tolerance as SDL_FontCache's decoder).
static u32 decodeUTF8(const char** ptr)
{
    const u8* p = (const u8*)*ptr;
    u32 cp      = *p;
    int len     = 1;
    if (cp >= 0xF0) {
        cp &= 0x07;
        len = 4;
    }
    else if (cp >= 0xE0) {
        cp &= 0x0F;
        len = 3;
    }
    else if (cp >= 0xC0) {
        cp &= 0x1F;
        len = 2;
    }
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            len = i;
            break;
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *ptr += len;
    return cp;
}

// Open a fresh zero-filled R8 atlas page. Its descriptor swizzles to
// (1, 1, 1, R), so the existing "texture x vertex color" fragment shader
// turns coverage into colored text with no second pipeline.
static bool newAtlasPage(void)
{
    u32 descId;
    if (!allocDescId(descId)) {
        return false;
    }

    AtlasPage page{};
    dk::ImageLayout layout;
    dk::ImageLayoutMaker{s_device}.setFlags(0).setFormat(DkImageFormat_R8_Unorm).setDimensions(ATLAS_SIZE, ATLAS_SIZE).initialize(layout);
    page.mem = s_poolImages->allocate(layout.getSize(), layout.getAlignment());
    page.image.initialize(layout, page.mem.getMemBlock(), page.mem.getOffset());
    page.descId = descId;

    CMemPool::Handle scratch = s_poolData->allocate(ATLAS_SIZE * ATLAS_SIZE, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    memset(scratch.getCpuAddr(), 0, ATLAS_SIZE * ATLAS_SIZE);

    dk::ImageView view{page.image};
    view.setSwizzle(DkImageSwizzle_One, DkImageSwizzle_One, DkImageSwizzle_One, DkImageSwizzle_Red);
    dk::ImageDescriptor descriptor;
    descriptor.initialize(view);

    oneShotCommands([&](dk::CmdBuf& cmd) {
        dk::ImageView copyView{page.image};
        cmd.copyBufferToImage({scratch.getGpuAddr()}, copyView, {0, 0, 0, ATLAS_SIZE, ATLAS_SIZE, 1});
        s_imageDescs->update(cmd, descId, descriptor);
    });
    scratch.destroy();

    s_atlasPages.push_back(page);
    return true;
}

// Shelf-pack a w x h glyph into the newest atlas page (older pages are
// closed), opening a new page when full.
static bool atlasAlloc(u32 w, u32 h, u32& pageIdx, u32& outX, u32& outY)
{
    if (w > ATLAS_SIZE || h > ATLAS_SIZE) {
        return false;
    }
    AtlasPage* page = s_atlasPages.empty() ? nullptr : &s_atlasPages.back();
    if (page) {
        if (page->shelfX + w > ATLAS_SIZE) {
            page->shelfY += page->shelfH + ATLAS_PAD;
            page->shelfX = 0;
            page->shelfH = 0;
        }
        if (page->shelfY + h > ATLAS_SIZE) {
            page = nullptr;
        }
    }
    if (!page) {
        if (!newAtlasPage()) {
            return false;
        }
        page = &s_atlasPages.back();
    }
    pageIdx = (u32)(s_atlasPages.size() - 1);
    outX    = page->shelfX;
    outY    = page->shelfY;
    page->shelfX += w + ATLAS_PAD;
    page->shelfH = std::max(page->shelfH, h);
    return true;
}

// FC's face choice: NintendoExt owns the PUA block (button glyphs), then the
// Standard font, then the CJK fallbacks (BMP only, as in SDL_FontCache);
// anything still missing renders Standard's .notdef box.
static FT_Face chooseSansFace(u32 cp)
{
    if (cp >= 0xE000 && cp <= 0xF8FF && s_sansFaces[1]) {
        return s_sansFaces[1];
    }
    if (s_sansFaces[0] && FT_Get_Char_Index(s_sansFaces[0], cp) != 0) {
        return s_sansFaces[0];
    }
    if (cp <= 0xFFFF) {
        for (int i = 2; i < s_numSansFaces; i++) {
            if (s_sansFaces[i] && FT_Get_Char_Index(s_sansFaces[i], cp) != 0) {
                return s_sansFaces[i];
            }
        }
    }
    return s_sansFaces[0];
}

// Rasterize (or fetch) one glyph. Cell metrics replicate what SDL_ttf's
// single-char render + SDL_FontCache produced: cellW is the surface width
// (max(maxx, advance) - min(minx, 0)) and the pen advances by it; the bitmap
// sits at (max(minx,0), ascent - bearingY) inside the cell.
static const GlyphData* getGlyph(FontFamily family, FontInstance& inst, u32 cp)
{
    auto it = inst.glyphs.find(cp);
    if (it != inst.glyphs.end()) {
        return &it->second;
    }

    if (cp == '\t') {
        const GlyphData* space = getGlyph(family, inst, ' ');
        if (!space) {
            return nullptr;
        }
        GlyphData tab{};
        tab.page  = GLYPH_NO_PAGE;
        tab.cellW = (u16)(4 * space->cellW); // FC: tab = 4 space cells
        return &inst.glyphs.emplace(cp, tab).first->second;
    }

    FT_Face face = family == FontFamily::Mono ? s_monoFace : chooseSansFace(cp);
    if (!face || FT_Set_Char_Size(face, 0, inst.px << 6, 0, 0) != 0 || FT_Load_Char(face, cp, FT_LOAD_RENDER) != 0) {
        return nullptr;
    }

    FT_GlyphSlot slot         = face->glyph;
    const FT_Glyph_Metrics& m = slot->metrics;
    const int ascent          = ftCeil(FT_MulFix(face->ascender, face->size->metrics.y_scale));
    const int minx            = ftFloor(m.horiBearingX);
    const int maxx            = ftCeil(m.horiBearingX + m.width);
    const int advance         = ftCeil(m.horiAdvance);

    GlyphData glyph{};
    glyph.page  = GLYPH_NO_PAGE;
    glyph.cellW = (u16)std::max(0, std::max(maxx, advance) - std::min(minx, 0));
    glyph.bmpW  = (u16)slot->bitmap.width;
    glyph.bmpH  = (u16)slot->bitmap.rows;
    glyph.bmpDX = (s16)std::max(minx, 0);
    glyph.bmpDY = (s16)(ascent - ftFloor(m.horiBearingY));

    if (glyph.bmpW > 0 && glyph.bmpH > 0 && slot->bitmap.buffer && slot->bitmap.pitch > 0) {
        u32 pageIdx, ax, ay;
        if (atlasAlloc(glyph.bmpW, glyph.bmpH, pageIdx, ax, ay)) {
            CMemPool::Handle scratch = s_poolData->allocate((size_t)glyph.bmpW * glyph.bmpH, 64);
            u8* dst                  = (u8*)scratch.getCpuAddr();
            for (u32 row = 0; row < glyph.bmpH; row++) {
                memcpy(dst + (size_t)row * glyph.bmpW, slot->bitmap.buffer + (size_t)row * slot->bitmap.pitch, glyph.bmpW);
            }
            dk::ImageView view{s_atlasPages[pageIdx].image};
            oneShotCommands([&](dk::CmdBuf& cmd) { cmd.copyBufferToImage({scratch.getGpuAddr()}, view, {ax, ay, 0, glyph.bmpW, glyph.bmpH, 1}); });
            scratch.destroy();
            glyph.page   = (u16)pageIdx;
            glyph.atlasX = (u16)ax;
            glyph.atlasY = (u16)ay;
        }
    }
    return &inst.glyphs.emplace(cp, glyph).first->second;
}

// Per-(family, size) instance, created lazily. Mono degrades to Sans when the
// romfs font failed to load, exactly like the SDL backend — hence family is
// taken by reference and normalized.
static FontInstance& instanceFor(FontFamily& family, int size)
{
    if (family == FontFamily::Mono && !s_monoFace) {
        family = FontFamily::Sans;
    }
    auto& map = family == FontFamily::Mono ? s_monoInstances : s_sansInstances;
    auto it   = map.find(size);
    if (it != map.end()) {
        return it->second;
    }

    FontInstance inst;
    inst.px         = scaledFontPx(size);
    FT_Face primary = family == FontFamily::Mono ? s_monoFace : s_sansFaces[0];
    int ttfHeight   = 0;
    if (primary && FT_Set_Char_Size(primary, 0, inst.px << 6, 0, 0) == 0) {
        const long yScale = primary->size->metrics.y_scale;
        const int ascent  = ftCeil(FT_MulFix(primary->ascender, yScale));
        const int descent = ftCeil(FT_MulFix(primary->descender, yScale));
        ttfHeight         = ascent - descent + 1; // TTF_FontHeight
    }
    inst.fcHeight = (int)std::ceil(ttfHeight * 1.2); // FC's inflated line height
    return map.emplace(size, std::move(inst)).first->second;
}

// FC_GetWidth: sum of cell widths per line, maximum across lines; glyphs that
// fail to rasterize count as a space.
static u32 measureWidth(FontFamily family, FontInstance& inst, const char* text)
{
    u32 width = 0, best = 0;
    for (const char* c = text; *c != '\0';) {
        if (*c == '\n') {
            best  = std::max(best, width);
            width = 0;
            c++;
            continue;
        }
        const u32 cp       = decodeUTF8(&c);
        const GlyphData* g = getGlyph(family, inst, cp);
        if (!g) {
            g = getGlyph(family, inst, ' ');
        }
        if (g) {
            width += g->cellW;
        }
    }
    return std::max(best, width);
}

// FC_RenderLeft: pen starts at (x, y = line top); '\n' returns to x and drops
// one fcHeight; spaces advance without drawing.
static void drawString(FontFamily family, FontInstance& inst, int x, int y, Color color, const char* text)
{
    float destX = x;
    float destY = y;
    for (const char* c = text; *c != '\0';) {
        if (*c == '\n') {
            destX = x;
            destY += inst.fcHeight;
            c++;
            continue;
        }
        const u32 cp       = decodeUTF8(&c);
        const GlyphData* g = getGlyph(family, inst, cp);
        if (!g) {
            g = getGlyph(family, inst, ' ');
            if (!g) {
                continue;
            }
        }
        if (cp != ' ' && g->page != GLYPH_NO_PAGE) {
            const AtlasPage& page = s_atlasPages[g->page];
            const float u0        = g->atlasX / (float)ATLAS_SIZE;
            const float v0        = g->atlasY / (float)ATLAS_SIZE;
            const float u1        = (g->atlasX + g->bmpW) / (float)ATLAS_SIZE;
            const float v1        = (g->atlasY + g->bmpH) / (float)ATLAS_SIZE;
            pushQuadRaw(page.descId, false, destX + g->bmpDX, destY + g->bmpDY, g->bmpW, g->bmpH, u0, v0, u1, v1, color);
        }
        destX += g->cellW;
    }
}

// FC_GetBufferFitToColumn: split on '\n', then greedily pack words (space
// runs stay glued to the preceding word); the first word of a line never
// wraps, so an overlong word overflows its line rather than breaking.
static std::vector<std::string> wrapText(FontFamily family, FontInstance& inst, const char* text, int width)
{
    std::vector<std::string> out;
    const std::string all(text);
    size_t start = 0;
    while (true) {
        const size_t nl        = all.find('\n', start);
        const std::string line = all.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (width <= 0 || (int)measureWidth(family, inst, line.c_str()) <= width) {
            out.push_back(line);
        }
        else {
            // (word, trailing spaces) pairs
            std::vector<std::pair<std::string, std::string>> words;
            size_t i = 0;
            while (i < line.size()) {
                size_t ws              = line.find(' ', i);
                const std::string word = line.substr(i, ws == std::string::npos ? std::string::npos : ws - i);
                std::string spaces;
                if (ws == std::string::npos) {
                    i = line.size();
                }
                else {
                    size_t j = ws;
                    while (j < line.size() && line[j] == ' ') {
                        j++;
                    }
                    spaces = line.substr(ws, j - ws);
                    i      = j;
                }
                words.emplace_back(word, spaces);
            }
            std::string current = words.empty() ? std::string() : words[0].first + words[0].second;
            for (size_t k = 1; k < words.size(); k++) {
                const std::string candidate = current + words[k].first;
                if ((int)measureWidth(family, inst, candidate.c_str()) > width) {
                    out.push_back(current);
                    current = words[k].first + words[k].second;
                }
                else {
                    current = candidate + words[k].second;
                }
            }
            out.push_back(current);
        }
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

void Gfx::DrawText(int size, int x, int y, Color color, const char* text, FontFamily family)
{
    if (!text) {
        return;
    }
    FontInstance& inst = instanceFor(family, size);
    drawString(family, inst, x, y, color, text);
}

// NOTE: FC_DrawBox also clipped to the box rect; the single caller (button
// labels in clickable.cpp) never overflows it, so clipping is not replicated.
void Gfx::DrawTextBox(int size, int x, int y, Color color, int max, const char* text, FontFamily family)
{
    if (!text) {
        return;
    }
    FontInstance& inst = instanceFor(family, size);
    int destY          = y;
    for (const std::string& line : wrapText(family, inst, text, max)) {
        drawString(family, inst, x, destY, color, line.c_str());
        destY += inst.fcHeight;
    }
}

void Gfx::GetTextDimensions(int size, const char* text, u32* w, u32* h, FontFamily family)
{
    FontInstance& inst = instanceFor(family, size);
    if (w != NULL) {
        *w = text ? measureWidth(family, inst, text) : 0;
    }
    if (h != NULL) {
        u32 lines = 1;
        for (const char* c = text; c && *c != '\0'; c++) {
            if (*c == '\n') {
                lines++;
            }
        }
        *h = text ? (u32)inst.fcHeight * lines : 0; // FC_GetHeight: fcHeight x lines
    }
}

void Gfx::LoadImage(Texture** texture, const char* path)
{
    u32 w = 0, h = 0;
    u8* pixels = decodeFileToRGBA(path, w, h);
    if (pixels) {
        Texture* t = createTexture(pixels, w, h);
        if (t) {
            *texture = t;
        }
        free(pixels);
    }
}

void Gfx::LoadImage(Texture** texture, u8* buff, size_t size)
{
    u32 w = 0, h = 0;
    u8* pixels = decodeToRGBA(buff, size, w, h);
    if (pixels) {
        Texture* t = createTexture(pixels, w, h);
        if (t) {
            *texture = t;
        }
        free(pixels);
    }
}

void Gfx::DrawImageScale(Texture* texture, int x, int y, int w, int h)
{
    if (!texture) {
        return;
    }
    pushQuad(texture, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, COLOR_WHITE);
}

void Gfx::SetTextureOpaque(Texture* texture)
{
    if (texture) {
        texture->opaque = true;
    }
}

void Gfx::DestroyTexture(Texture* texture)
{
    if (texture) {
        // The GPU may still be sampling this texture in an in-flight frame;
        // rare (icon cache refresh), so a full sync is acceptable.
        s_queue.waitIdle();
        texture->mem.destroy();
        s_freeDescIds.push_back(texture->descId);
        delete texture;
    }
}

Texture* Gfx::StarTexture(void)
{
    return s_star;
}

Texture* Gfx::CheckboxTexture(void)
{
    return s_checkbox;
}

void Gfx::CreateColorTexture(Texture** texture, int w, int h, Color color)
{
    std::vector<u8> pixels((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        pixels[(size_t)i * 4 + 0] = color.r;
        pixels[(size_t)i * 4 + 1] = color.g;
        pixels[(size_t)i * 4 + 2] = color.b;
        pixels[(size_t)i * 4 + 3] = color.a;
    }
    Texture* t = createTexture(pixels.data(), w, h);
    if (t) {
        *texture = t;
    }
}

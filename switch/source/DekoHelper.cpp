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

// deko3d implementation of the SDLH_* backend API (see HANDOFF-deko3d.md).
// Phase 2: batched GPU quad renderer — rects, images and color textures draw
// through one pipeline (pos+uv+color vertices, texture x vertex color, a 1x1
// white texture makes solid rects the same path). Image *decode* still goes
// through SDL_image (decode -> RGBA -> GPU upload); text is stubbed until
// phase 3. Build with GFX_BACKEND=deko3d; the SDL backend still ships.

#ifdef GFX_BACKEND_DEKO3D

#include "SDLHelper.hpp"
#include "gfx/CCmdMemRing.h"
#include "gfx/CDescriptorSet.h"
#include "gfx/CMemPool.h"
#include "gfx/CShader.h"
#include "logging.hpp"
#include "main.hpp"
#include <SDL2/SDL_image.h>
#include <array>
#include <deko3d.hpp>
#include <optional>
#include <vector>

namespace {
    constexpr unsigned FB_NUM  = 2;
    constexpr u32 FB_WIDTH     = 1280;
    constexpr u32 FB_HEIGHT    = 720;
    constexpr u32 CMDMEM_SLICE = 0x40000; // per-frame dynamic command memory
    constexpr u32 IMAGEPOOL_SZ = 16 * 1024 * 1024;
    constexpr u32 DATAPOOL_SZ  = 4 * 1024 * 1024;
    constexpr u32 CODEPOOL_SZ  = 128 * 1024;

    constexpr u32 MAX_IMAGES = 1024; // image descriptor slots (icons, avatars, assets)
    constexpr u32 MAX_QUADS  = 16384;

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

    constexpr std::array VertexAttribState = {
        DkVtxAttribState{0, 0, offsetof(Vertex, x), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex, u), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex, rgba), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
    };
    constexpr std::array VertexBufferState = {
        DkVtxBufferState{sizeof(Vertex), 0},
    };

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

    CShader s_vertexShader;
    CShader s_fragmentShader;
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

    // Current batch: contiguous vertex range sharing texture + blend state.
    u32 s_batchStart   = 0;
    u32 s_batchDescId  = UINT32_MAX;
    bool s_batchOpaque = false;
    bool s_blendBound  = true; // blending state currently bound on the cmdbuf

    // Swapchain slot acquired for the frame being recorded; -1 outside a frame.
    int s_slot = -1;

    Texture* s_white    = nullptr; // 1x1 white: solid rects sample this
    Texture* s_star     = nullptr;
    Texture* s_checkbox = nullptr;
}

// deko3d implementation of the opaque backend handle declared in gfxtypes.hpp.
struct Texture {
    CMemPool::Handle mem;
    dk::Image image;
    u32 width;
    u32 height;
    u32 descId;
    bool opaque; // draw without alpha blending (SDLH_SetTextureOpaque)
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

// Upload a tightly-packed RGBA8 pixel buffer into a new GPU texture and
// publish its image descriptor. Returns nullptr (with a log) when the
// descriptor table is exhausted.
static Texture* createTexture(const u8* pixels, u32 width, u32 height)
{
    u32 descId;
    if (!s_freeDescIds.empty()) {
        descId = s_freeDescIds.back();
        s_freeDescIds.pop_back();
    }
    else if (s_nextDescId < MAX_IMAGES) {
        descId = s_nextDescId++;
    }
    else {
        Logging::error("deko3d: image descriptor table exhausted ({} slots).", MAX_IMAGES);
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

// Decode an image with SDL_image into a malloc'd RGBA8 buffer, reproducing
// the SDL backend's black colorkey: fully-opaque pure-black pixels become
// transparent (SDL_SetColorKey only ever matched the opaque mapping of
// (0,0,0)). Caller frees.
static u8* decodeToRGBA(SDL_Surface* loaded, u32& width, u32& height)
{
    if (!loaded) {
        return nullptr;
    }
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!rgba) {
        return nullptr;
    }

    width      = rgba->w;
    height     = rgba->h;
    u8* pixels = (u8*)malloc((size_t)width * height * 4);
    if (pixels) {
        for (u32 row = 0; row < height; row++) {
            memcpy(pixels + (size_t)row * width * 4, (u8*)rgba->pixels + (size_t)row * rgba->pitch, (size_t)width * 4);
        }
        for (u32 i = 0; i < width * height; i++) {
            u8* px = pixels + (size_t)i * 4;
            if (px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 255) {
                px[3] = 0;
            }
        }
    }
    SDL_FreeSurface(rgba);
    return pixels;
}

bool SDLH_Init(void)
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

    dk::ImageLayout fbLayout;
    dk::ImageLayoutMaker{s_device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(FB_WIDTH, FB_HEIGHT)
        .initialize(fbLayout);

    std::array<DkImage const*, FB_NUM> fbArray;
    for (unsigned i = 0; i < FB_NUM; i++) {
        s_fbMem[i] = s_poolImages->allocate(fbLayout.getSize(), fbLayout.getAlignment());
        s_framebuffers[i].initialize(fbLayout, s_fbMem[i].getMemBlock(), s_fbMem[i].getOffset());
        fbArray[i] = &s_framebuffers[i];
    }
    s_swapchain = dk::SwapchainMaker{s_device, nwindowGetDefault(), fbArray}.create();

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

    SDLH_LoadImage(&s_star, "romfs:/star.png");
    // The multi-select badge is accent-filled with a white check on top; the
    // checkbox asset is a black-on-transparent checkmark. The SDL backend
    // tinted it via SDL_SetTextureColorMod(white); here the tint is baked into
    // the pixels before upload.
    u32 cbW = 0, cbH = 0;
    u8* cbPixels = decodeToRGBA(IMG_Load("romfs:/checkbox.png"), cbW, cbH);
    if (cbPixels) {
        for (u32 i = 0; i < cbW * cbH; i++) {
            u8* px = cbPixels + (size_t)i * 4;
            px[0] = px[1] = px[2] = 255;
        }
        s_checkbox = createTexture(cbPixels, cbW, cbH);
        free(cbPixels);
    }

    g_username_dotsize = 0; // text is stubbed until phase 3

    return true;
}

void SDLH_Exit(void)
{
    if (s_queue) {
        s_queue.waitIdle();
    }
    SDLH_DestroyTexture(s_checkbox);
    SDLH_DestroyTexture(s_star);
    SDLH_DestroyTexture(s_white);
    s_swapchain.destroy();
    for (unsigned i = 0; i < FB_NUM; i++) {
        s_fbMem[i].destroy();
    }
    s_vtxMem.destroy();
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
    s_slot = s_queue.acquireImage(s_swapchain);
    s_cmdRing->begin(s_cmdbuf);

    dk::ImageView colorTarget{s_framebuffers[s_slot]};
    s_cmdbuf.bindRenderTargets(&colorTarget);
    s_cmdbuf.setViewports(0, {{0.0f, 0.0f, (float)FB_WIDTH, (float)FB_HEIGHT, 0.0f, 1.0f}});
    s_cmdbuf.setScissors(0, {{0, 0, FB_WIDTH, FB_HEIGHT}});

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

    s_cmdbuf.bindShaders(DkStageFlag_GraphicsMask, {s_vertexShader, s_fragmentShader});
    s_cmdbuf.bindRasterizerState(rasterizerState);
    s_cmdbuf.bindDepthStencilState(depthStencilState);
    s_cmdbuf.bindColorState(colorState);
    s_cmdbuf.bindColorWriteState(colorWriteState);
    s_cmdbuf.bindBlendStates(0, blendState);
    s_blendBound = true;

    s_vtxBase = (Vertex*)((u8*)s_vtxMem.getCpuAddr() + s_vtxSlice * VTX_SLICE_SZ);
    s_cmdbuf.bindVtxBuffer(0, s_vtxMem.getGpuAddr() + s_vtxSlice * VTX_SLICE_SZ, VTX_SLICE_SZ);
    s_cmdbuf.bindVtxAttribState(VertexAttribState);
    s_cmdbuf.bindVtxBufferState(VertexBufferState);
    s_vtxCount      = 0;
    s_batchStart    = 0;
    s_batchDescId   = UINT32_MAX;
    s_vtxOverflowed = false;
}

// Emit the pending batch (if any) as one draw call, binding its texture and
// switching blending on/off as needed.
static void flushBatch(void)
{
    const u32 count = s_vtxCount - s_batchStart;
    if (count == 0 || s_batchDescId == UINT32_MAX) {
        return;
    }
    const bool wantBlend = !s_batchOpaque;
    if (wantBlend != s_blendBound) {
        dk::ColorState colorState;
        colorState.setBlendEnable(0, wantBlend);
        s_cmdbuf.bindColorState(colorState);
        s_blendBound = wantBlend;
    }
    s_cmdbuf.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(s_batchDescId, 0));
    s_cmdbuf.draw(DkPrimitive_Triangles, count, 1, s_batchStart, 0);
    s_batchStart = s_vtxCount;
}

static void pushQuad(const Texture* texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color color)
{
    frameBegin();
    if (texture->descId != s_batchDescId || texture->opaque != s_batchOpaque) {
        flushBatch();
        s_batchDescId = texture->descId;
        s_batchOpaque = texture->opaque;
    }
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

void SDLH_ClearScreen(Color color)
{
    frameBegin();
    flushBatch(); // preserve painter's order if anything was already batched
    s_cmdbuf.clearColor(0, DkColorMask_RGBA, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

void SDLH_Render(void)
{
    g_currentTime = armTicksToNs(armGetSystemTick()) / 1000000000.0f;
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

void SDLH_DrawRect(int x, int y, int w, int h, Color color)
{
    pushQuad(s_white, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void SDLH_DrawText(int, int, int, Color, const char*, FontFamily) {}

void SDLH_DrawTextBox(int, int, int, Color, int, const char*, FontFamily) {}

void SDLH_GetTextDimensions(int, const char*, u32* w, u32* h, FontFamily)
{
    if (w != NULL)
        *w = 0;
    if (h != NULL)
        *h = 0;
}

void SDLH_LoadImage(Texture** texture, const char* path)
{
    u32 w = 0, h = 0;
    u8* pixels = decodeToRGBA(IMG_Load(path), w, h);
    if (pixels) {
        Texture* t = createTexture(pixels, w, h);
        if (t) {
            *texture = t;
        }
        free(pixels);
    }
}

void SDLH_LoadImage(Texture** texture, u8* buff, size_t size)
{
    u32 w = 0, h = 0;
    u8* pixels = decodeToRGBA(IMG_Load_RW(SDL_RWFromMem(buff, size), 1), w, h);
    if (pixels) {
        Texture* t = createTexture(pixels, w, h);
        if (t) {
            *texture = t;
        }
        free(pixels);
    }
}

void SDLH_DrawImage(Texture* texture, int x, int y)
{
    if (!texture) {
        return;
    }
    pushQuad(texture, x, y, texture->width, texture->height, 0.0f, 0.0f, 1.0f, 1.0f, COLOR_WHITE);
}

void SDLH_DrawImageScale(Texture* texture, int x, int y, int w, int h)
{
    if (!texture) {
        return;
    }
    pushQuad(texture, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, COLOR_WHITE);
}

void SDLH_SetTextureOpaque(Texture* texture)
{
    if (texture) {
        texture->opaque = true;
    }
}

void SDLH_DestroyTexture(Texture* texture)
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

Texture* SDLH_StarTexture(void)
{
    return s_star;
}

Texture* SDLH_CheckboxTexture(void)
{
    return s_checkbox;
}

void SDLH_CreateColorTexture(Texture** texture, int w, int h, Color color)
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

#endif // GFX_BACKEND_DEKO3D

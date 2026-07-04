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
// Phase 1: device/swapchain bring-up, clear + vsynced present. Rects, images
// and text are still stubs; build with GFX_BACKEND=deko3d only to test the
// bring-up, ship the SDL backend.

#ifdef GFX_BACKEND_DEKO3D

#include "SDLHelper.hpp"
#include "gfx/CCmdMemRing.h"
#include "gfx/CMemPool.h"
#include "logging.hpp"
#include "main.hpp"
#include <array>
#include <deko3d.hpp>
#include <optional>

// deko3d implementation of the opaque backend handle declared in gfxtypes.hpp.
// Phase 1 never creates one (image loading is stubbed); the definition exists
// so SDLH_DestroyTexture can delete a complete type.
struct Texture {
    int unused;
};

namespace {
    constexpr unsigned FB_NUM  = 2;
    constexpr u32 FB_WIDTH     = 1280;
    constexpr u32 FB_HEIGHT    = 720;
    constexpr u32 CMDMEM_SLICE = 0x10000; // per-frame dynamic command memory
    constexpr u32 IMAGEPOOL_SZ = 16 * 1024 * 1024;
    constexpr u32 DATAPOOL_SZ  = 1 * 1024 * 1024;

    dk::UniqueDevice s_device;
    dk::UniqueQueue s_queue;
    std::optional<CMemPool> s_poolImages;
    std::optional<CMemPool> s_poolData;
    dk::UniqueCmdBuf s_cmdbuf;
    std::optional<CCmdMemRing<FB_NUM>> s_cmdRing;
    CMemPool::Handle s_fbMem[FB_NUM];
    dk::Image s_framebuffers[FB_NUM];
    dk::UniqueSwapchain s_swapchain;

    // Swapchain slot acquired for the frame being recorded; -1 outside a frame.
    int s_slot = -1;
}

bool SDLH_Init(void)
{
    s_device = dk::DeviceMaker{}.create();
    s_queue  = dk::QueueMaker{s_device}.setFlags(DkQueueFlags_Graphics).create();

    s_poolImages.emplace(s_device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, IMAGEPOOL_SZ);
    s_poolData.emplace(s_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, DATAPOOL_SZ);

    s_cmdbuf = dk::CmdBufMaker{s_device}.create();
    s_cmdRing.emplace();
    if (!s_cmdRing->allocate(*s_poolData, CMDMEM_SLICE)) {
        Logging::error("deko3d: failed to allocate command memory ring.");
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

    g_username_dotsize = 0; // text is stubbed in phase 1

    return true;
}

void SDLH_Exit(void)
{
    if (s_queue) {
        s_queue.waitIdle();
    }
    s_swapchain.destroy();
    for (unsigned i = 0; i < FB_NUM; i++) {
        s_fbMem[i].destroy();
    }
    s_cmdRing.reset();
    s_cmdbuf.destroy();
    s_poolData.reset();
    s_poolImages.reset();
    s_queue.destroy();
    s_device.destroy();
}

// First draw call of the frame: acquire the swapchain slot, start recording
// into this frame's command memory slice, bind the acquired framebuffer.
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
}

void SDLH_ClearScreen(Color color)
{
    frameBegin();
    s_cmdbuf.clearColor(0, DkColorMask_RGBA, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

void SDLH_Render(void)
{
    g_currentTime = armTicksToNs(armGetSystemTick()) / 1000000000.0f;
    if (s_slot < 0) {
        return;
    }
    DkCmdList list = s_cmdRing->end(s_cmdbuf);
    s_queue.submitCommands(list);
    s_queue.presentImage(s_swapchain, s_slot);
    s_slot = -1;
}

// ---- phase 2+ stubs ------------------------------------------------------

void SDLH_DrawRect(int, int, int, int, Color)
{
    frameBegin();
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

void SDLH_LoadImage(Texture**, const char*) {}

void SDLH_LoadImage(Texture**, u8*, size_t) {}

void SDLH_DrawImage(Texture*, int, int) {}

void SDLH_DrawImageScale(Texture*, int, int, int, int) {}

void SDLH_SetTextureOpaque(Texture*) {}

void SDLH_DestroyTexture(Texture* texture)
{
    delete texture;
}

Texture* SDLH_StarTexture(void)
{
    return nullptr;
}

Texture* SDLH_CheckboxTexture(void)
{
    return nullptr;
}

void SDLH_CreateColorTexture(Texture**, int, int, Color) {}

#endif // GFX_BACKEND_DEKO3D

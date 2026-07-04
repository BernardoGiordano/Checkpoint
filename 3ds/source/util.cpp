/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2025 Bernardo Giordano, FlagBrew
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

#include "util.hpp"
#include "backupsize.hpp"
#include "loader.hpp"
#include "server.hpp"
#include "thread.hpp"
#include "title.hpp"
#include "titlecache.hpp"
#include <malloc.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

Result consoleDisplayError(const std::string& message, Result res)
{
    consoleInit(GFX_TOP, nullptr);
    printf("\x1b[2;13HCheckpoint v%d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, GIT_REV);
    printf("\x1b[5;1HError during startup: \x1b[31m0x%08lX\x1b[0m", res);
    printf("\x1b[8;1HDescription: \x1b[33m%s\x1b[0m", message.c_str());
    printf("\x1b[29;16HPress START to exit.");
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
    while (aptMainLoop() && !(hidKeysDown() & KEY_START)) {
        hidScanInput();
    }
    return res;
}

Result servicesInit(void)
{
    Result res = 0;
    hidInit();
    ATEXIT(hidExit);

    Threads::init(0, 2);

    gfxInitDefault();
    ATEXIT(gfxExit);

    Logging::init();
    ATEXIT(Logging::exit);

    Logging::info("Checkpoint loading started...");

    Handle hbldrHandle;
    if (R_FAILED(res = svcConnectToPort(&hbldrHandle, "hb:ldr"))) {
        return consoleDisplayError("Rosalina not found on this system.\nAn updated CFW is required to launch Checkpoint.", res);
    }
    svcCloseHandle(hbldrHandle);

    if (R_FAILED(res = Archive::init())) {
        return consoleDisplayError("Archive::init failed.", res);
    }
    ATEXIT(Archive::exit);

    mkdir("sdmc:/3ds", 777);
    mkdir("sdmc:/3ds/Checkpoint", 777);
    mkdir("sdmc:/3ds/Checkpoint/saves", 777);
    mkdir("sdmc:/3ds/Checkpoint/extdata", 777);
    mkdir("sdmc:/3ds/Checkpoint/logs", 777);

    Logging::initFileLogging();

    romfsInit();
    ATEXIT(romfsExit);

    srvInit();
    ATEXIT(srvExit);

    amInit();
    ATEXIT(amExit);

    pxiDevInit();
    ATEXIT(pxiDevExit);

    Gui::init();
    ATEXIT(Gui::exit);

    u32* socketBuffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (socketBuffer != NULL) {
        if (!socInit(socketBuffer, SOC_BUFFERSIZE)) {
            ATEXIT(socExit);
            Server::init();
            ATEXIT(Server::exit);
        }
        else {
            Logging::warning("socInit failed");
        }
    }
    else {
        Logging::warning("Failed to create socket buffer.");
    }

    // BACKSTOP ONLY for exits that never reach the end of main (early errors, no
    // workers running yet). The real thread shutdown happens explicitly at the end
    // of main(), BEFORE exit(): singletons the workers use (BackupSizeCache,
    // TitleCatalog, …) are lazily constructed after these registrations, so their
    // destructors run before these handlers would — a live worker would race the
    // destruction (heap corruption). Threads::exit is idempotent so running it here
    // after the explicit call is harmless. Order still matters for the backstop:
    // atexit is LIFO, so these run flags-first, join-second, before any service
    // teardown; every persistent loop thread must have its stop flag raised before
    // the join (Server::requestStop here, cart scan via clearCartScanFlag below) or
    // Threads::exit waits on it forever.
    ATEXIT(Threads::exit);
    ATEXIT(BackupSizeCache::shutdownStatic);
    ATEXIT(Server::requestStop);

    // Verify TWLN access up front so a missing permission degrades to a logged,
    // persisted opt-out instead of a per-title failure storm during the scan.
    if (Configuration::getInstance().dsiwareSaves()) {
        FS_Archive twln;
        if (R_FAILED(res = FSUSER_OpenArchive(&twln, ARCHIVE_NAND_TWL_FS, fsMakePath(PATH_EMPTY, "")))) {
            Logging::error("Failed to open TWLN with result 0x{:08X}; disabling DSiWare saves.", (u32)res);
            Configuration::getInstance().setDSiWareSaves(false);
            Configuration::getInstance().commit();
        }
        else {
            FSUSER_CloseArchive(twln);
        }
    }

    Threads::executeTask(TitleCatalog::loadTitlesThread);

    if (Configuration::getInstance().shouldScanCard()) {
        TitleCatalog::cartScanFlagTestAndSet();
        Threads::create(TitleCatalog::cartScan);
        ATEXIT(TitleCatalog::clearCartScanFlag);
    }

    Logging::info("Checkpoint loading finished!");

    return 0;
}

void calculateTitleDBHash(u8* hash)
{
    u32 titleCount, nandCount, titlesRead, nandTitlesRead;
    AM_GetTitleCount(MEDIATYPE_SD, &titleCount);
    const bool nandSaves    = Configuration::getInstance().nandSaves();
    const bool dsiwareSaves = Configuration::getInstance().dsiwareSaves();
    // The toggles select which NAND titles the scan keeps, so they are part of
    // the cache identity even when the installed-title list is unchanged.
    const u64 configFlags = (nandSaves ? 1 : 0) | (dsiwareSaves ? 2 : 0);
    // Append the cache format version so a format change invalidates every
    // existing hash and forces the caches to regenerate.
    if (nandSaves || dsiwareSaves) {
        AM_GetTitleCount(MEDIATYPE_NAND, &nandCount);
        std::vector<u64> ordered(titleCount + nandCount);
        AM_GetTitleList(&titlesRead, MEDIATYPE_SD, titleCount, ordered.data());
        AM_GetTitleList(&nandTitlesRead, MEDIATYPE_NAND, nandCount, ordered.data() + titlesRead);
        sort(ordered.begin(), ordered.end());
        ordered.push_back(TitleCache::FORMAT_VERSION);
        ordered.push_back(configFlags);
        sha256(hash, (u8*)ordered.data(), ordered.size() * sizeof(u64));
    }
    else {
        std::vector<u64> ordered(titleCount);
        AM_GetTitleList(&titlesRead, MEDIATYPE_SD, titleCount, ordered.data());
        sort(ordered.begin(), ordered.end());
        ordered.push_back(TitleCache::FORMAT_VERSION);
        ordered.push_back(configFlags);
        sha256(hash, (u8*)ordered.data(), ordered.size() * sizeof(u64));
    }
}

std::u16string StringUtils::UTF8toUTF16(const char* src)
{
    const uint8_t* in = (const uint8_t*)src;
    ssize_t units     = utf8_to_utf16(nullptr, in, 0);
    if (units < 0) {
        return u"";
    }
    std::u16string dst(units, u'\0');
    utf8_to_utf16((uint16_t*)dst.data(), in, units + 1);
    return dst;
}

std::string StringUtils::UTF16toUTF8(const std::u16string& src)
{
    const uint16_t* in = (const uint16_t*)src.c_str();
    ssize_t units      = utf16_to_utf8(nullptr, in, 0);
    if (units < 0) {
        return "";
    }
    std::string dst(units, '\0');
    utf16_to_utf8((uint8_t*)dst.data(), in, units + 1);
    return dst;
}

std::u16string StringUtils::removeForbiddenCharacters(std::u16string src)
{
    static const std::u16string illegalChars = StringUtils::UTF8toUTF16(".,!\\/:?*\"<>|");
    for (size_t i = 0; i < src.length(); i++) {
        if (illegalChars.find(src[i]) != std::string::npos) {
            src[i] = ' ';
        }
    }

    // Trim trailing spaces. find_last_not_of handles the empty / all-spaces cases
    // (npos) without the size()-1 underflow of a reverse index loop.
    size_t end = src.find_last_not_of(u' ');
    if (end == std::u16string::npos) {
        src.clear();
    }
    else {
        src.erase(end + 1);
    }

    return src;
}

// One decoder and one glyph-width cache for every text-measuring path.
namespace {
    std::map<u16, charWidthInfo_s*> widthCache;
    std::queue<u16> widthCacheOrder;
    constexpr size_t WIDTH_CACHE_CAP = 1000;

    // Decodes the UTF-8 codepoint starting at s[i] (BMP only — the cache key is
    // u16). `extraBytes` is how many continuation bytes follow the lead byte;
    // invalid or truncated sequences decode as 0xFFFF with no advance.
    u16 nextCodepoint(const std::string& s, size_t i, int& extraBytes)
    {
        u16 codepoint = 0xFFFF;
        extraBytes    = 0;
        if (s[i] & 0x80 && s[i] & 0x40 && s[i] & 0x20 && !(s[i] & 0x10) && i + 2 < s.size()) {
            codepoint  = s[i] & 0x0F;
            codepoint  = codepoint << 6 | (s[i + 1] & 0x3F);
            codepoint  = codepoint << 6 | (s[i + 2] & 0x3F);
            extraBytes = 2;
        }
        else if (s[i] & 0x80 && s[i] & 0x40 && !(s[i] & 0x20) && i + 1 < s.size()) {
            codepoint  = s[i] & 0x1F;
            codepoint  = codepoint << 6 | (s[i + 1] & 0x3F);
            extraBytes = 1;
        }
        else if (!(s[i] & 0x80)) {
            codepoint = s[i];
        }
        return codepoint;
    }

    float glyphWidth(u16 codepoint, float scaleX)
    {
        auto width = widthCache.find(codepoint);
        if (width != widthCache.end()) {
            return width->second->charWidth * scaleX;
        }
        widthCache.insert_or_assign(codepoint, fontGetCharWidthInfo(NULL, fontGlyphIndexFromCodePoint(NULL, codepoint)));
        widthCacheOrder.push(codepoint);
        if (widthCache.size() > WIDTH_CACHE_CAP) {
            widthCache.erase(widthCacheOrder.front());
            widthCacheOrder.pop();
        }
        return widthCache[codepoint]->charWidth * scaleX;
    }
}

std::string StringUtils::splitWord(const std::string& text, float scaleX, float maxWidth)
{
    std::string word = text;
    if (StringUtils::textWidth(word, scaleX) > maxWidth) {
        float currentWidth = 0.0f;
        for (size_t i = 0; i < word.size(); i++) {
            int extraBytes;
            u16 codepoint   = nextCodepoint(word, i, extraBytes);
            float charWidth = glyphWidth(codepoint, scaleX);
            currentWidth += charWidth;
            if (currentWidth > maxWidth) {
                word.insert(i, 1, '\n');
                currentWidth = charWidth;
            }

            i += extraBytes; // Yay, variable width encodings
        }
    }
    return word;
}

float StringUtils::textWidth(const std::string& text, float scaleX)
{
    float ret        = 0.0f;
    float largestRet = 0.0f;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n') {
            largestRet = std::max(largestRet, ret);
            ret        = 0.0f;
            continue;
        }
        int extraBytes;
        u16 codepoint = nextCodepoint(text, i, extraBytes);
        i += extraBytes;
        ret += glyphWidth(codepoint, scaleX);
    }
    return std::max(largestRet, ret);
}

float StringUtils::textWidth(const C2D_Text& text, float scaleX)
{
    return ceilf(text.width * scaleX);
}

std::string StringUtils::wrap(const std::string& text, float scaleX, float maxWidth)
{
    if (textWidth(text, scaleX) <= maxWidth) {
        return text;
    }
    std::string dst, line, word;
    dst = line = word = "";

    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        word += *it;
        if (*it == ' ') {
            // split single words that are bigger than maxWidth
            if (StringUtils::textWidth(line + word, scaleX) <= maxWidth) {
                line += word;
            }
            else {
                if (StringUtils::textWidth(word, scaleX) > maxWidth) {
                    line += word;
                    line = StringUtils::splitWord(line, scaleX, maxWidth);
                    word = line.substr(line.find('\n') + 1, std::string::npos);
                    line = line.substr(0, line.find('\n')); // Split line on first newLine; assign second part to word and first to line
                }
                if (line[line.size() - 1] == ' ') {
                    dst += line.substr(0, line.size() - 1) + '\n';
                }
                else {
                    dst += line + '\n';
                }
                line = word;
            }
            word = "";
        }
    }

    // "Another iteration" of the loop b/c it probably won't end with a space.
    // If it does, no harm done.
    if (StringUtils::textWidth(line + word, scaleX) <= maxWidth) {
        dst += line + word;
    }
    else {
        if (StringUtils::textWidth(word, scaleX) > maxWidth) {
            line += word;
            line = StringUtils::splitWord(line, scaleX, maxWidth);
            word = line.substr(line.find('\n') + 1, std::string::npos);
            line = line.substr(0, line.find('\n'));
        }
        if (line[line.size() - 1] == ' ') {
            dst += line.substr(0, line.size() - 1) + '\n' + word;
        }
        else {
            dst += line + '\n' + word;
        }
    }
    return dst;
}

float StringUtils::textHeight(const std::string& text, float scaleY)
{
    size_t n = std::count(text.begin(), text.end(), '\n') + 1;
    return ceilf(scaleY * fontGetInfo(NULL)->lineFeed * n);
}

std::string StringUtils::humanBytes(u64 bytes)
{
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        return StringUtils::format("%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    if (bytes >= 1024ull * 1024ull) {
        return StringUtils::format("%.1f MB", bytes / (1024.0 * 1024.0));
    }
    if (bytes >= 1024ull) {
        return StringUtils::format("%.1f KB", bytes / 1024.0);
    }
    return StringUtils::format("%llu B", bytes);
}

std::string StringUtils::versionString(void)
{
    return StringUtils::format("v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
}
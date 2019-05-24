/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

void servicesExit(void)
{
    CheatManager::exit();
    Gui::exit();
    pxiDevExit();
    Archive::exit();
    amExit();
    srvExit();
    hidExit();
    sdmcExit();
    romfsExit();
}

Result servicesInit(void)
{
    Result res = 0;

    Handle hbldrHandle;
    if (R_FAILED(res = svcConnectToPort(&hbldrHandle, "hb:ldr")))
        return res;

    romfsInit();
    sdmcInit();
    hidInit();
    srvInit();
    amInit();
    pxiDevInit();

    if (R_FAILED(res = Archive::init()))
        return res;

    mkdir("sdmc:/3ds", 777);
    mkdir("sdmc:/3ds/Checkpoint", 777);
    mkdir("sdmc:/3ds/Checkpoint/saves", 777);
    mkdir("sdmc:/3ds/Checkpoint/extdata", 777);
    mkdir("sdmc:/3ds/Checkpoint/cheats", 777);
    mkdir("sdmc:/cheats", 777);

    Gui::init();
    Threads::create((ThreadFunc)CheatManager::init);

    // consoleDebugInit(debugDevice_SVC);
    // while (aptMainLoop() && !(hidKeysDown() & KEY_START)) { hidScanInput(); }

    Configuration::getInstance();

    return 0;
}

void calculateTitleDBHash(u8* hash)
{
    u32 titleCount, nandCount, titlesRead, nandTitlesRead;
    AM_GetTitleCount(MEDIATYPE_SD, &titleCount);
    if (Configuration::getInstance().nandSaves()) {
        AM_GetTitleCount(MEDIATYPE_NAND, &nandCount);
        u64 buf[titleCount + nandCount];
        AM_GetTitleList(&titlesRead, MEDIATYPE_SD, titleCount, buf);
        AM_GetTitleList(&nandTitlesRead, MEDIATYPE_NAND, nandCount, buf + titlesRead * sizeof(u64));
        sha256(hash, (u8*)buf, (titleCount + nandCount) * sizeof(u64));
    }
    else {
        u64 buf[titleCount];
        AM_GetTitleList(&titlesRead, MEDIATYPE_SD, titleCount, buf);
        sha256(hash, (u8*)buf, titleCount * sizeof(u64));
    }
}

std::u16string StringUtils::UTF8toUTF16(const char* src)
{
    char16_t tmp[256] = {0};
    utf8_to_utf16((uint16_t*)tmp, (uint8_t*)src, 256);
    return std::u16string(tmp);
}

std::u16string StringUtils::removeForbiddenCharacters(std::u16string src)
{
    static const std::u16string illegalChars = StringUtils::UTF8toUTF16(".,!\\/:?*\"<>|");
    for (size_t i = 0; i < src.length(); i++) {
        if (illegalChars.find(src[i]) != std::string::npos) {
            src[i] = ' ';
        }
    }

    size_t i;
    for (i = src.length() - 1; i > 0 && src[i] == L' '; i--)
        ;
    src.erase(i + 1, src.length() - i);

    return src;
}

static std::map<u16, charWidthInfo_s*> widthCache;
static std::queue<u16> widthCacheOrder;

std::string StringUtils::splitWord(const std::string& text, float scaleX, float maxWidth)
{
    std::string word = text;
    if (StringUtils::textWidth(word, scaleX) > maxWidth) {
        float currentWidth = 0.0f;
        for (size_t i = 0; i < word.size(); i++) {
            u16 codepoint = 0xFFFF;
            int iMod      = 0;
            if (word[i] & 0x80 && word[i] & 0x40 && word[i] & 0x20 && !(word[i] & 0x10) && i + 2 < word.size()) {
                codepoint = word[i] & 0x0F;
                codepoint = codepoint << 6 | (word[i + 1] & 0x3F);
                codepoint = codepoint << 6 | (word[i + 2] & 0x3F);
                iMod      = 2;
            }
            else if (word[i] & 0x80 && word[i] & 0x40 && !(word[i] & 0x20) && i + 1 < word.size()) {
                codepoint = word[i] & 0x1F;
                codepoint = codepoint << 6 | (word[i + 1] & 0x3F);
                iMod      = 1;
            }
            else if (!(word[i] & 0x80)) {
                codepoint = word[i];
            }
            float charWidth;
            auto width = widthCache.find(codepoint);
            if (width != widthCache.end()) {
                charWidth = width->second->charWidth * scaleX;
            }
            else {
                widthCache.insert_or_assign(codepoint, fontGetCharWidthInfo(fontGlyphIndexFromCodePoint(codepoint)));
                widthCacheOrder.push(codepoint);
                if (widthCache.size() > 512) {
                    widthCache.erase(widthCacheOrder.front());
                    widthCacheOrder.pop();
                }
                charWidth = widthCache[codepoint]->charWidth * scaleX;
            }
            currentWidth += charWidth;
            if (currentWidth > maxWidth) {
                word.insert(i, 1, '\n');
                currentWidth = charWidth;
            }

            i += iMod; // Yay, variable width encodings
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
        u16 codepoint = 0xFFFF;
        if (text[i] & 0x80 && text[i] & 0x40 && text[i] & 0x20 && !(text[i] & 0x10) && i + 2 < text.size()) {
            codepoint = text[i] & 0x0F;
            codepoint = codepoint << 6 | (text[i + 1] & 0x3F);
            codepoint = codepoint << 6 | (text[i + 2] & 0x3F);
            i += 2;
        }
        else if (text[i] & 0x80 && text[i] & 0x40 && !(text[i] & 0x20) && i + 1 < text.size()) {
            codepoint = text[i] & 0x1F;
            codepoint = codepoint << 6 | (text[i + 1] & 0x3F);
            i += 1;
        }
        else if (!(text[i] & 0x80)) {
            codepoint = text[i];
        }
        float charWidth;
        auto width = widthCache.find(codepoint);
        if (width != widthCache.end()) {
            charWidth = width->second->charWidth * scaleX;
        }
        else {
            widthCache.insert_or_assign(codepoint, fontGetCharWidthInfo(fontGlyphIndexFromCodePoint(codepoint)));
            widthCacheOrder.push(codepoint);
            if (widthCache.size() > 1000) {
                widthCache.erase(widthCacheOrder.front());
                widthCacheOrder.pop();
            }
            charWidth = widthCache[codepoint]->charWidth * scaleX;
        }
        ret += charWidth;
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

    // "Another iteration" of the loop b/c it probably won't end with a space
    // If it does, no harm done
    // word = StringUtils::splitWord(word, scaleX, maxWidth);
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
    return ceilf(scaleY * fontGetInfo()->lineFeed * n);
}
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

// device_seal / device_unseal: authenticated encryption for the small secrets a
// script has to keep on the SD card between runs — an OAuth refresh token, an
// API key. One copy for both consoles; everything console-specific is the 32
// bytes ScriptHost::deviceSecret answers with.
//
// WHAT THIS DOES AND DOES NOT PROTECT AGAINST. Neither console isolates
// homebrew: every app has the whole SD card, and Checkpoint is open source, so
// any other homebrew can reproduce the console-bound half of the key by
// following this file. A seal is therefore not a boundary against another
// homebrew app on the same console, and no message shown to a user may claim it
// is. What it does buy, because the console-bound half is never written to the
// SD card:
//
//   * an SD card read on a PC, or an SD image, holds nothing usable;
//   * a config folder the user zips up and shares leaks nothing;
//   * a scraper globbing for *token*.json / client_secret.json finds nothing;
//   * moving the card to another console does not carry the credential over.
//
// The passphrase half is the real boundary: it is not on the card and not in the
// console either, so an attacker holding the blob has to guess it. It is opt-in
// per blob because it costs the user a keyboard prompt per run.
//
// Blob layout, little-endian, header 60 bytes:
//
//   0   8   magic "CKPTSEAL"
//   8   1   version (1)
//   9   1   flags, bit 0 = a passphrase is part of the key
//   10  1   device key source (a DeviceKeySource; 0 = passphrase only)
//   11  1   reserved, 0
//   12  4   PBKDF2 iteration count (0 when there is no passphrase)
//   16  16  salt
//   32  12  GCM nonce
//   44  16  GCM tag
//   60  ..  ciphertext, same length as the plaintext
//
// Key derivation, with a zero-filled stand-in for whichever half is absent:
//
//   dk  = ScriptHost::deviceSecret(source)
//   pk  = PBKDF2-HMAC-SHA256(passphrase, salt, iterations, 32)
//   key = HMAC-SHA256(key = salt, msg = "CKPTSEAL1" || dk || pk)
//
// then AES-256-GCM with bytes [0, 44) of the header as additional data, so the
// version, the flags, the recorded key source, the iteration count and the salt
// are all authenticated: an attacker cannot strip the passphrase flag or swap
// the key source to steer us at a weaker derivation without failing the tag.
//
// The recorded key source is why unseal asks for one specific source rather than
// "the best available". A platform whose strongest source stops answering after
// a firmware or CFW change would otherwise silently derive a different key, and
// the user would lose their own credential to what looks like corruption.

#include "scriptargs.hpp"
#include "scriptheap.hpp"
#include "scripthost.hpp"
#include <cstdint>
#include <cstring>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#include <string>

extern "C" {
#include "checkpoint_api.h"
#include "interpreter.h"
}

namespace {

    constexpr char kMagic[8]      = {'C', 'K', 'P', 'T', 'S', 'E', 'A', 'L'};
    constexpr uint8_t kVersion    = 1;
    constexpr uint8_t kFlagPasswd = 0x01;
    constexpr size_t kSaltSize    = 16;
    constexpr size_t kNonceSize   = 12;
    constexpr size_t kTagSize     = 16;
    constexpr size_t kKeySize     = 32;
    constexpr size_t kHeaderSize  = 60;
    constexpr size_t kAadSize     = 44; // header up to (not including) the tag
    constexpr size_t kOffFlags    = 9;
    constexpr size_t kOffSource   = 10;
    constexpr size_t kOffIters    = 12;
    constexpr size_t kOffSalt     = 16;
    constexpr size_t kOffNonce    = 32;
    constexpr size_t kOffTag      = 44;

    // What a fresh seal costs the user. Stored per blob rather than assumed, so
    // this can be raised for newer consoles without orphaning older blobs — the
    // slowest hardware we run on is the 3DS's 268 MHz ARM11 with no crypto
    // acceleration, where this is on the order of a second, paid once per run.
    constexpr uint32_t kIterations = 60000;

    // Script-visible result codes. Shared by both bindings so a script can log
    // one number; kept in the <checkpoint.h> prototype comment as well.
    enum SealResult : int {
        SealOk         = 0,
        SealBadArgs    = -1, // not a sealed blob, or nothing to seal
        SealBadVersion = -2, // a blob from a newer Checkpoint
        SealNoMemory   = -3,
        SealNoKey      = -4, // no console key source and no passphrase either
        SealAuthFailed = -5, // wrong passphrase, wrong console, or tampered
        SealNoRandom   = -6, // the platform could not produce a salt/nonce
    };

    void putU32(uint8_t* p, uint32_t v)
    {
        p[0] = (uint8_t)(v & 0xFF);
        p[1] = (uint8_t)((v >> 8) & 0xFF);
        p[2] = (uint8_t)((v >> 16) & 0xFF);
        p[3] = (uint8_t)((v >> 24) & 0xFF);
    }

    uint32_t getU32(const uint8_t* p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

    bool looksSealed(const uint8_t* blob, size_t size)
    {
        return blob != nullptr && size >= kHeaderSize && memcmp(blob, kMagic, sizeof(kMagic)) == 0;
    }

    // The two halves mixed into one AES-256 key. `source` is the exact
    // DeviceKeySource to bind to (DeviceKeySourceNone for passphrase only), and
    // a passphrase of "" means there is none. False when neither half exists —
    // deriving from the label and the salt alone would be a key anyone can
    // compute, which is worse than refusing.
    bool deriveKey(int source, const char* passphrase, const uint8_t* salt, uint32_t iterations, uint8_t* outKey)
    {
        const bool hasPass = passphrase != nullptr && passphrase[0] != '\0';
        if (source == DeviceKeySourceNone && !hasPass) {
            return false;
        }

        // "CKPTSEAL1" || dk || pk, with a zero-filled half for whichever is
        // absent, so the message stays a fixed length and no two different
        // (device, passphrase) pairs can produce the same bytes.
        uint8_t material[9 + kKeySize + kKeySize];
        memset(material, 0, sizeof(material));
        memcpy(material, "CKPTSEAL1", 9);
        bool ok = true;

        if (source != DeviceKeySourceNone && ScriptHost::get().deviceSecret(material + 9, source) < 0) {
            ok = false;
        }

        if (ok && hasPass) {
            const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
            mbedtls_md_context_t md_ctx;
            mbedtls_md_init(&md_ctx);
            ok = md != nullptr && mbedtls_md_setup(&md_ctx, md, 1 /* HMAC */) == 0 &&
                 mbedtls_pkcs5_pbkdf2_hmac(&md_ctx, (const unsigned char*)passphrase, strlen(passphrase), salt, kSaltSize, iterations,
                     (uint32_t)kKeySize, material + 9 + kKeySize) == 0;
            mbedtls_md_free(&md_ctx);
        }

        if (ok) {
            // HKDF's extract step: the salt is the HMAC key, the two halves are
            // the message, and SHA-256's output is already exactly the key size.
            const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
            ok                          = md != nullptr && mbedtls_md_hmac(md, salt, kSaltSize, material, sizeof(material), outKey) == 0;
        }

        mbedtls_platform_zeroize(material, sizeof(material));
        return ok;
    }

    // Hands a run-scoped copy of `size` bytes to the script through an out
    // parameter pair, then wipes the working buffer. False = out of memory.
    bool handOver(const uint8_t* data, size_t size, char** out, int* outSize)
    {
        void* copy = ScriptHeap::get().dupString(std::string((const char*)data, size));
        if (copy == nullptr) {
            return false;
        }
        *out     = (char*)copy;
        *outSize = (int)size;
        return true;
    }
}

void ckpt_device_seal(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "device_seal");
    const size_t plainSize = args.byteCount(1);
    const char* plain      = plainSize > 0 ? args.str(0) : args.strOr(0, "");
    const char* passphrase = args.strOr(2, "");
    char** out             = args.outStr(3);
    int* outSize           = args.outInt(4);
    *out                   = nullptr;
    *outSize               = 0;

    if (plainSize == 0) {
        ReturnValue->Val->Integer = SealBadArgs;
        return;
    }

    std::string blob(kHeaderSize + plainSize, '\0');
    uint8_t* buf = (uint8_t*)&blob[0];
    memcpy(buf, kMagic, sizeof(kMagic));
    buf[8]           = kVersion;
    const bool hasPw = passphrase[0] != '\0';
    buf[kOffFlags]   = hasPw ? kFlagPasswd : 0;
    putU32(buf + kOffIters, hasPw ? kIterations : 0);

    if (!ScriptHost::get().randomBytes(buf + kOffSalt, kSaltSize) || !ScriptHost::get().randomBytes(buf + kOffNonce, kNonceSize)) {
        ReturnValue->Val->Integer = SealNoRandom;
        return;
    }

    // Which console source to record. Asking for Best tells us which one
    // actually answers; deriveKey then asks that one by name, exactly as unseal
    // will. The bytes from this probe are thrown away rather than passed along,
    // so no console key material is held across the derivation.
    uint8_t probe[kKeySize];
    int source = ScriptHost::get().deviceSecret(probe, DeviceKeySourceBest);
    mbedtls_platform_zeroize(probe, sizeof(probe));
    // A console with no usable source still seals when the user set a
    // passphrase; with neither half deriveKey below refuses.
    if (source < 0) {
        source = DeviceKeySourceNone;
    }
    buf[kOffSource] = (uint8_t)source;

    uint8_t key[kKeySize];
    if (!deriveKey(source, passphrase, buf + kOffSalt, getU32(buf + kOffIters), key)) {
        mbedtls_platform_zeroize(key, sizeof(key));
        ReturnValue->Val->Integer = SealNoKey;
        return;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, (unsigned int)(kKeySize * 8));
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plainSize, buf + kOffNonce, kNonceSize, buf, kAadSize, (const unsigned char*)plain,
            buf + kHeaderSize, kTagSize, buf + kOffTag);
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(key, sizeof(key));

    if (rc != 0) {
        ReturnValue->Val->Integer = SealBadArgs;
        return;
    }
    if (!handOver(buf, blob.size(), out, outSize)) {
        ReturnValue->Val->Integer = SealNoMemory;
        return;
    }
    ReturnValue->Val->Integer = SealOk;
}

void ckpt_device_unseal(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "device_unseal");
    const size_t blobSize  = args.byteCount(1);
    const char* blobArg    = blobSize > 0 ? args.str(0) : args.strOr(0, "");
    const char* passphrase = args.strOr(2, "");
    char** out             = args.outStr(3);
    int* outSize           = args.outInt(4);
    *out                   = nullptr;
    *outSize               = 0;

    const uint8_t* blob = (const uint8_t*)blobArg;
    if (!looksSealed(blob, blobSize)) {
        ReturnValue->Val->Integer = SealBadArgs;
        return;
    }
    if (blob[8] != kVersion) {
        ReturnValue->Val->Integer = SealBadVersion;
        return;
    }

    // The source the blob was made with, not the best one available now.
    uint8_t key[kKeySize];
    if (!deriveKey((int)blob[kOffSource], passphrase, blob + kOffSalt, getU32(blob + kOffIters), key)) {
        mbedtls_platform_zeroize(key, sizeof(key));
        ReturnValue->Val->Integer = SealNoKey;
        return;
    }

    const size_t cipherSize = blobSize - kHeaderSize;
    std::string plain(cipherSize == 0 ? 1 : cipherSize, '\0');
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, (unsigned int)(kKeySize * 8));
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm, cipherSize, blob + kOffNonce, kNonceSize, blob, kAadSize, blob + kOffTag, kTagSize, blob + kHeaderSize, (unsigned char*)&plain[0]);
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(key, sizeof(key));

    if (rc != 0) {
        // GCM verified the tag before we got here, so nothing that failed is
        // handed to the script — a wrong passphrase cannot yield garbage the
        // script would then treat as its config.
        mbedtls_platform_zeroize(&plain[0], plain.size());
        ReturnValue->Val->Integer = SealAuthFailed;
        return;
    }
    const bool ok = handOver((const uint8_t*)plain.data(), cipherSize, out, outSize);
    mbedtls_platform_zeroize(&plain[0], plain.size());
    ReturnValue->Val->Integer = ok ? SealOk : SealNoMemory;
}

void ckpt_seal_needs_passphrase(struct ParseState* Parser, struct Value* ReturnValue, struct Value** Param, int NumArgs)
{
    const ScriptArgs args(Parser, Param, NumArgs, "seal_needs_passphrase");
    const size_t blobSize = args.byteCount(1);
    const char* blob      = blobSize > 0 ? args.str(0) : args.strOr(0, "");

    if (!looksSealed((const uint8_t*)blob, blobSize)) {
        ReturnValue->Val->Integer = SealBadArgs;
        return;
    }
    ReturnValue->Val->Integer = (((const uint8_t*)blob)[kOffFlags] & kFlagPasswd) != 0 ? 1 : 0;
}

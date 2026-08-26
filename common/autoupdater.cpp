/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#include "autoupdater.hpp"
#include "json.hpp"
#include "logging.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <exception>
#include <string>

#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
#elif defined(__SWITCH__)
#include <switch.h>
#endif

namespace {
    // Published by the download loop, read by the UI thread every frame. Plain
    // relaxed atomics: the UI only needs the latest pair to size a bar, and a
    // frame drawn from a slightly stale count is invisible.
    std::atomic<size_t> g_downloaded{0};
    std::atomic<size_t> g_total{0};
    std::atomic<bool> g_busy{false};

    constexpr const char* RELEASE_API = "https://api.github.com/repos/BernardoGiordano/Checkpoint/releases/latest";

    size_t appendString(char* data, size_t size, size_t count, void* user)
    {
        static_cast<std::string*>(user)->append(data, size * count);
        return size * count;
    }

    // Bytes already on disk when the current attempt started. A resumed request
    // reports only its own slice, so the bar has to add the head back in.
    std::atomic<size_t> g_resumeBase{0};

    int reportProgress(void*, curl_off_t downloadTotal, curl_off_t downloadNow, curl_off_t, curl_off_t)
    {
        const size_t base = g_resumeBase.load(std::memory_order_relaxed);
        g_downloaded.store(base + (downloadNow > 0 ? size_t(downloadNow) : 0), std::memory_order_relaxed);
        // Content-Length only shows up once headers are in; until then the size
        // seeded from the release metadata is the better total.
        if (downloadTotal > 0)
            g_total.store(base + size_t(downloadTotal), std::memory_order_relaxed);
        return 0;
    }

    bool configure(CURL* curl, const std::string& url)
    {
        return curl && curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
               curl_easy_setopt(curl, CURLOPT_USERAGENT, "Checkpoint-updater") == CURLE_OK &&
               curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L) == CURLE_OK &&
               curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L) == CURLE_OK &&
               // Neither console ships a CA store. This matches PKSM's updater;
               // enable peer verification when Checkpoint starts bundling one.
               curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L) == CURLE_OK;
    }

    bool getLatestRelease(std::string& body)
    {
        CURL* curl = curl_easy_init();
        if (!configure(curl, RELEASE_API)) {
            if (curl)
                curl_easy_cleanup(curl);
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        const CURLcode result = curl_easy_perform(curl);
        long status           = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
        return result == CURLE_OK && status == 200;
    }

    bool parseVersion(const std::string& text, int& major, int& minor, int& micro)
    {
        const char* start = text.c_str();
        if (*start == 'v' || *start == 'V')
            start++;
        char tail = '\0';
        return sscanf(start, "%d.%d.%d%c", &major, &minor, &micro, &tail) == 3 && major >= 0 && minor >= 0 && micro >= 0;
    }

    bool isNewer(const std::string& tag)
    {
        int major, minor, micro;
        if (!parseVersion(tag, major, minor, micro))
            return false;
        return major > VERSION_MAJOR || (major == VERSION_MAJOR && minor > VERSION_MINOR) ||
               (major == VERSION_MAJOR && minor == VERSION_MINOR && micro > VERSION_MICRO);
    }

    bool findAsset(const std::string& body, const char* assetName, std::string& version, std::string& url, size_t& size)
    {
        const auto json = nlohmann::json::parse(body, nullptr, false);
        if (!json.is_object() || !json.contains("tag_name") || !json["tag_name"].is_string() || !json.contains("assets") ||
            !json["assets"].is_array()) {
            return false;
        }

        version = json["tag_name"].get<std::string>();
        for (const auto& candidate : json["assets"]) {
            if (candidate.is_object() && candidate.contains("name") && candidate["name"].is_string() &&
                candidate["name"].get<std::string>() == assetName && candidate.contains("browser_download_url") &&
                candidate["browser_download_url"].is_string()) {
                url  = candidate["browser_download_url"].get<std::string>();
                size = candidate.contains("size") && candidate["size"].is_number_unsigned() ? candidate["size"].get<size_t>() : 0;
                return true;
            }
        }
        return false;
    }

    enum class Attempt { Complete, Partial, Restart, Aborted };

    // One transfer, appending to whatever `path` already holds. `bytes` comes
    // back as the size on disk afterwards so the caller can resume from it.
    Attempt fetchOnce(const AutoUpdater::Update& update, const std::string& path, size_t offset, size_t& bytes)
    {
        bytes      = offset;
        FILE* file = fopen(path.c_str(), offset > 0 ? "ab" : "wb");
        if (!file) {
            Logging::warning("Auto-update could not open {} for writing.", path);
            return Attempt::Aborted;
        }

        CURL* curl = curl_easy_init();
        if (!configure(curl, update.url)) {
            fclose(file);
            if (curl)
                curl_easy_cleanup(curl);
            return Attempt::Aborted;
        }
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, reportProgress);
        // A whole release over console Wi-Fi legitimately outlasts the 120 s
        // cap the metadata request uses. Bound the stall instead of the run:
        // give up only if the transfer sits under 1 KB/s for 30 s.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        if (offset > 0)
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, curl_off_t(offset));

        const CURLcode result = curl_easy_perform(curl);
        long status           = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);

        // Flush before measuring: ftell counts bytes still sitting in the stdio
        // buffer, so an SD write that fails at close would otherwise look complete.
        const bool flushed = fflush(file) == 0 && ferror(file) == 0;
        const long written = ftell(file);
        fclose(file);
        bytes = written > 0 ? size_t(written) : offset;

        if (offset > 0 && status != 206) {
            // Range ignored: the body restarted at zero and got appended to the
            // head we already had. Throw the file away and start over.
            Logging::warning("Auto-update resume from {} was refused (HTTP {}); restarting the download.", offset, status);
            return Attempt::Restart;
        }
        if (result != CURLE_OK || status < 200 || status >= 300 || !flushed) {
            Logging::warning("Auto-update transfer stopped: curl {} ({}), HTTP {}, {} of {} bytes{}.", int(result), curl_easy_strerror(result),
                status, bytes, update.size, flushed ? "" : ", write error");
            return bytes > offset ? Attempt::Partial : Attempt::Restart;
        }
        if (update.size != 0 && bytes != update.size) {
            Logging::warning("Auto-update transfer ended short: {} of {} bytes.", bytes, update.size);
            return bytes > offset ? Attempt::Partial : Attempt::Restart;
        }
        return bytes > 0 ? Attempt::Complete : Attempt::Restart;
    }

    bool fetch(const AutoUpdater::Update& update, const std::string& path)
    {
        // Console Wi-Fi drops often enough that one stall should not cost the
        // whole artifact. Pick up from what is already on disk instead.
        constexpr int ATTEMPTS = 4;
        size_t offset          = 0;
        for (int attempt = 0; attempt < ATTEMPTS; attempt++) {
            g_resumeBase.store(offset, std::memory_order_relaxed);
            size_t bytes = 0;
            switch (fetchOnce(update, path, offset, bytes)) {
                case Attempt::Complete:
                    return true;
                case Attempt::Partial:
                    Logging::info("Resuming Checkpoint {} from {} bytes.", update.version, bytes);
                    offset = bytes;
                    continue;
                case Attempt::Restart:
                    offset = 0;
                    remove(path.c_str());
                    continue;
                case Attempt::Aborted:
                    // Local failure (no file handle, no curl handle): retrying
                    // under identical conditions only burns attempts.
                    attempt = ATTEMPTS;
                    continue;
            }
        }
        g_resumeBase.store(0, std::memory_order_relaxed);
        remove(path.c_str());
        return false;
    }

    bool replaceExecutable(const std::string& path, const std::string& downloaded)
    {
        const std::string backup = path + ".old";
        remove(backup.c_str());

        // FAT backends do not consistently replace an existing destination.
        // Keep the old executable until the new name is safely in place.
        if (rename(path.c_str(), backup.c_str()) != 0)
            return false;
        if (rename(downloaded.c_str(), path.c_str()) != 0) {
            rename(backup.c_str(), path.c_str());
            return false;
        }
        remove(backup.c_str());
        return true;
    }

#ifdef __3DS__
    bool installCia(const std::string& path)
    {
        FILE* cia = fopen(path.c_str(), "rb");
        if (!cia)
            return false;

        Handle destination;
        Result result = AM_StartCiaInstallOverwrite(&destination, MEDIATYPE_SD);
        if (R_FAILED(result)) {
            fclose(cia);
            return false;
        }

        constexpr size_t BUFFER_SIZE = 0x10000;
        u8* buffer                   = static_cast<u8*>(memalign(0x1000, BUFFER_SIZE));
        if (!buffer) {
            AM_CancelCIAInstall(destination);
            fclose(cia);
            return false;
        }

        u64 offset = 0;
        while (!feof(cia)) {
            const size_t read = fread(buffer, 1, BUFFER_SIZE, cia);
            if (read == 0)
                break;
            u32 written = 0;
            result      = FSFILE_Write(destination, &written, offset, buffer, read, FS_WRITE_FLUSH);
            if (R_FAILED(result) || written != read) {
                free(buffer);
                AM_CancelCIAInstall(destination);
                fclose(cia);
                return false;
            }
            offset += read;
        }

        const bool readOk = !ferror(cia);
        free(buffer);
        fclose(cia);
        if (!readOk || offset == 0) {
            AM_CancelCIAInstall(destination);
            return false;
        }
        result = AM_FinishCiaInstall(destination);
        return R_SUCCEEDED(result);
    }

    Result setHbldrTarget(Handle handle, const char* path)
    {
        const u32 pathLength = strlen(path) + 1;
        u32* command         = getThreadCommandBuffer();
        command[0]           = IPC_MakeHeader(2, 0, 2);
        command[1]           = IPC_Desc_StaticBuffer(pathLength, 0);
        command[2]           = reinterpret_cast<u32>(path);
        Result result        = svcSendSyncRequest(handle);
        return R_SUCCEEDED(result) ? command[1] : result;
    }
#endif
}

void AutoUpdater::init()
{
    static bool done = false;
    if (!done) {
        done = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        if (!done)
            Logging::warning("Auto-update could not initialise curl.");
    }
}

std::optional<AutoUpdater::Update> AutoUpdater::check(const std::string& executablePath)
{
    try {
#ifdef __SWITCH__
        if (executablePath.size() < 4 || executablePath.substr(executablePath.size() - 4) != ".nro") {
            Logging::warning("Auto-update skipped: current NRO path is unavailable.");
            return std::nullopt;
        }
        const char* assetName = "Checkpoint.nro";
#else
        const bool is3dsx     = executablePath.size() >= 5 && executablePath.substr(executablePath.size() - 5) == ".3dsx";
        const char* assetName = is3dsx ? "Checkpoint.3dsx" : "Checkpoint.cia";
#endif

        Logging::info("Checking for Checkpoint updates...");
        std::string body;
        if (!getLatestRelease(body)) {
            Logging::warning("Auto-update check failed.");
            return std::nullopt;
        }

        Update update;
        if (!findAsset(body, assetName, update.version, update.url, update.size)) {
            Logging::warning("Auto-update response did not contain {}.", assetName);
            return std::nullopt;
        }
        if (!isNewer(update.version)) {
            Logging::info("Checkpoint is up to date (latest release {}).", update.version);
            return std::nullopt;
        }

#ifdef __3DS__
        update.target = is3dsx ? executablePath : "/3ds/Checkpoint/Checkpoint.cia";
        update.kind   = is3dsx ? ArtifactKind::Executable : ArtifactKind::Cia;
#else
        update.target = executablePath;
        update.kind   = ArtifactKind::Executable;
#endif
        Logging::info("Checkpoint update {} is available.", update.version);
        return update;
    }
    catch (const std::exception& error) {
        Logging::warning("Auto-update check failed: {}", error.what());
        return std::nullopt;
    }
    catch (...) {
        Logging::warning("Auto-update check failed with an unknown error.");
        return std::nullopt;
    }
}

AutoUpdater::Progress AutoUpdater::progress()
{
    return {g_downloaded.load(std::memory_order_relaxed), g_total.load(std::memory_order_relaxed)};
}

bool AutoUpdater::busy()
{
    return g_busy.load(std::memory_order_acquire);
}

AutoUpdater::Outcome AutoUpdater::install(const Update& update)
{
    // Cleared on every exit path, including the throwing ones, so a failed
    // install can never leave the main loop refusing to quit.
    struct BusyFlag {
        BusyFlag() { g_busy.store(true, std::memory_order_release); }
        ~BusyFlag() { g_busy.store(false, std::memory_order_release); }
    } busyFlag;

    try {
        const std::string temporary = update.target + ".new";
        // Seed the bar from the release metadata so the first frames already
        // show a real total instead of an empty trough.
        g_downloaded.store(0, std::memory_order_relaxed);
        g_total.store(update.size, std::memory_order_relaxed);
        g_resumeBase.store(0, std::memory_order_relaxed);
        Logging::info("Downloading Checkpoint {}...", update.version);
        if (!fetch(update, temporary)) {
            Logging::warning("Failed to download Checkpoint {}.", update.version);
            return Outcome::Failed;
        }

#ifdef __3DS__
        bool installed = false;
        if (update.kind == ArtifactKind::Executable) {
            // The mounted RomFS keeps the 3DSX open. Release it for the rename, then
            // remount so normal atexit teardown still owns one live mount.
            romfsExit();
            installed = replaceExecutable(update.target, temporary);
            romfsInit();
        }
        else {
            installed = installCia(temporary);
            remove(temporary.c_str());
        }
#else
        romfsExit();
        const bool installed = replaceExecutable(update.target, temporary);
        romfsInit();
#endif

        if (!installed) {
            remove(temporary.c_str());
            Logging::warning("Checkpoint {} downloaded but installation failed.", update.version);
            return Outcome::Failed;
        }
        Logging::info("Checkpoint {} installed.", update.version);
        return Outcome::Installed;
    }
    catch (const std::exception& error) {
        Logging::warning("Auto-update installation failed: {}", error.what());
        return Outcome::Failed;
    }
    catch (...) {
        Logging::warning("Auto-update installation failed with an unknown error.");
        return Outcome::Failed;
    }
}

bool AutoUpdater::requestRelaunch(const std::string& executablePath)
{
#ifdef __SWITCH__
    return envHasNextLoad() && R_SUCCEEDED(envSetNextLoad(executablePath.c_str(), executablePath.c_str()));
#else
    if (executablePath.size() < 5 || executablePath.substr(executablePath.size() - 5) != ".3dsx") {
        aptSetChainloaderToSelf();
        return true;
    }

    Handle handle;
    if (R_FAILED(svcConnectToPort(&handle, "hb:ldr")))
        return false;
    std::string path   = executablePath;
    const size_t colon = path.find(':');
    if (colon != std::string::npos)
        path.erase(0, colon + 1);
    const Result result = setHbldrTarget(handle, path.c_str());
    svcCloseHandle(handle);
    return R_SUCCEEDED(result);
#endif
}

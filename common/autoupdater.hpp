/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 */

#ifndef AUTOUPDATER_HPP
#define AUTOUPDATER_HPP

#include <cstddef>
#include <optional>
#include <string>

namespace AutoUpdater {
    enum class ArtifactKind {
        Executable,
        Cia,
    };

    // Everything needed to install one release. `check()` builds this on the
    // network worker; the UI keeps it until the user accepts the prompt.
    struct Update {
        std::string version;
        std::string url;
        std::string target;
        size_t size = 0;
        ArtifactKind kind;
    };

    enum class Outcome {
        Installed,
        Failed,
    };

    // Byte counters of the transfer running inside install(). Written by the
    // worker, polled by the UI thread once per frame to draw the bar; `total`
    // is 0 until the server announces a length.
    struct Progress {
        size_t downloaded = 0;
        size_t total      = 0;
    };
    Progress progress();

    // True from the moment install() starts until it has finished. The main
    // loops ask before honouring a quit request, so the console cannot be torn
    // down while the artifact is half-written.
    bool busy();

    // Brings curl's global state up. Must run on the main thread before any
    // worker touches curl: the Switch portlib is libcurl 7.69.1, which predates
    // the thread-safe global init (curl >= 7.84), so the implicit init inside
    // curl_easy_init() would otherwise happen on the check worker while the
    // main loop is running. Idempotent; curl refcounts it.
    void init();

    // Queries GitHub for a newer artifact matching the current launch type.
    // This performs network I/O and must be called from a worker thread.
    std::optional<Update> check(const std::string& executablePath);

    // Downloads and installs an update previously returned by check(). Both
    // halves are slow (network transfer, then a CIA write of several MB on
    // 3DS), so this must run on a worker thread with the UI drawing progress()
    // meanwhile. Nothing may read a romfs asset while it runs: the executable
    // path unmounts romfs across the rename that replaces the running build.
    Outcome install(const Update& update);

    // Requests that the loader starts the freshly installed build after this
    // process exits. A failed request is non-fatal: the update is still installed.
    bool requestRelaunch(const std::string& executablePath);
}

#endif

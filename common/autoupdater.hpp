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

    // Queries GitHub for a newer artifact matching the current launch type.
    // This performs network I/O and must be called from a worker thread.
    std::optional<Update> check(const std::string& executablePath);

    // Downloads and installs an update previously returned by check().
    Outcome install(const Update& update);

    // Requests that the loader starts the freshly installed build after this
    // process exits. A failed request is non-fatal: the update is still installed.
    bool requestRelaunch(const std::string& executablePath);
}

#endif

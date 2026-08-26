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

#include <string>

namespace AutoUpdater {
    enum class Outcome {
        NoUpdate,
        Installed,
        Failed,
    };

    // Checks GitHub's latest release and installs the artifact matching the
    // current launch type (.3dsx/.cia on 3DS, .nro on Switch).
    Outcome checkAndInstall(const std::string& executablePath);

    // Requests that the loader starts the freshly installed build after this
    // process exits. A failed request is non-fatal: the update is still installed.
    bool requestRelaunch(const std::string& executablePath);
}

#endif

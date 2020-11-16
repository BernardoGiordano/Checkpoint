/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2020 Bernardo Giordano, FlagBrew
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

#include "io.hpp"
#include "platform.hpp"
#include "logger.hpp"

#include <filesystem>
#include <system_error>
namespace fs = std::filesystem;

io::ActionResult io::copyDirectory(const IODataHolder& data)
{
    // Logger::log(Logger::DEBUG, "Copying folder %s to %s", data.srcPath.c_str(), data.dstPath.c_str());
    static constexpr auto copy_options = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
    std::error_code ec;
    fs::copy(data.srcPath, data.dstPath, copy_options, ec);
    return ec ? io::ActionResult::Failure : io::ActionResult::Success;
}
io::ActionResult io::copyFile(const IODataHolder& data)
{
    // Logger::log(Logger::DEBUG, "Creating folder %s", data.srcPath.c_str());
    std::error_code ec;
    fs::copy(data.srcPath, data.dstPath, ec);
    return ec ? io::ActionResult::Failure : io::ActionResult::Success;
}
io::ActionResult io::createDirectory(const IODataHolder& data)
{
    // Logger::log(Logger::DEBUG, "Creating folder %s", data.srcPath.c_str());
    std::error_code ec;
    fs::create_directory(data.srcPath, ec);
    return ec ? io::ActionResult::Failure : io::ActionResult::Success;
}
io::ActionResult io::deleteFolderRecursively(const IODataHolder& data)
{
    // Logger::log(Logger::DEBUG, "Deleting folder %s", data.srcPath.c_str());
    std::error_code ec;
    fs::remove_all(data.srcPath, ec);
    return ec ? io::ActionResult::Failure : io::ActionResult::Success;
}
io::ActionResult io::deleteFile(const IODataHolder& data)
{
    // Logger::log(Logger::DEBUG, "Deleting file %s", data.srcPath.c_str());
    std::error_code ec;
    fs::remove(data.srcPath, ec);
    return ec ? io::ActionResult::Failure : io::ActionResult::Success;
}

bool io::directoryExists(const IODataHolder& data)
{
    // Logger::log(Logger::DEBUG, "Checking existence for folder %s", data.srcPath.c_str());
    std::error_code ec;
    return fs::is_directory(data.srcPath, ec);
}
bool io::fileExists(const IODataHolder& data)
{
    // Logger::log(Logger::DEBUG, "Checking existence for file %s", data.srcPath.c_str());
    std::error_code ec;
    return fs::is_regular_file(data.srcPath, ec);
}

// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// Copyright (C) 2024 Michael Theall
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Adapted for Checkpoint: the config lives in memory only. ftpd persists these
// settings to its own .cfg file; Checkpoint owns config.json and creates the
// server with the values it wants, so load() is gone and save() is a no-op that
// keeps the SITE command working for the lifetime of the process.
//
// The Switch access-point settings are gone with the AP feature itself.

#pragma once

#include "ftpPlatform.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

class FtpConfig;
using UniqueFtpConfig = std::unique_ptr<FtpConfig>;

/// \brief FTP config
class FtpConfig
{
public:
	~FtpConfig ();

	/// \brief Create config with default values
	static UniqueFtpConfig create ();

	std::scoped_lock<platform::Mutex> lockGuard ();

	/// \brief Save config
	/// \note No-op on Checkpoint; settings are not persisted
	bool save ();

	/// \brief Get user
	std::string const &user () const;

	/// \brief Get password
	std::string const &pass () const;

	/// \brief Get hostname
	std::string const &hostname () const;

	/// \brief Get port
	std::uint16_t port () const;

	/// \brief Get deflate level
	int deflateLevel () const;

#ifdef __3DS__
	/// \brief Whether to get mtime
	/// \note only effective on 3DS
	bool getMTime () const;
#endif

	/// \brief Set user
	/// \param user_ User
	void setUser (std::string user_);

	/// \brief Set password
	/// \param pass_ Password
	void setPass (std::string pass_);

	/// \brief Set hostname
	/// \param hostname_ Hostname
	void setHostname (std::string hostname_);

	/// \brief Set listen port
	/// \param port_ Listen port
	bool setPort (std::string_view port_);

	/// \brief Set listen port
	/// \param port_ Listen port
	bool setPort (std::uint16_t port_);

	/// \brief Set deflate level
	/// \param level_ Deflate level
	bool setDeflateLevel (std::string_view level_);

	/// \brief Set deflate level
	/// \param level_ Deflate level
	bool setDeflateLevel (int level_);

#ifdef __3DS__
	/// \brief Set whether to get mtime
	/// \param getMTime_ Whether to get mtime
	void setGetMTime (bool getMTime_);
#endif

private:
	FtpConfig ();

	/// \brief Mutex
	mutable platform::Mutex m_lock;

	/// \brief Username
	std::string m_user;

	/// \brief Password
	std::string m_pass;

	/// \brief Hostname
	std::string m_hostname;

	/// \brief Listen port
	std::uint16_t m_port;

	/// \brief Deflate level
	int m_deflateLevel;

#ifdef __3DS__
	/// \brief Whether to get mtime
	bool m_getMTime = true;
#endif
};

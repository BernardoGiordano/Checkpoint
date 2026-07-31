// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
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
// Adapted for Checkpoint: the settings are immutable and live in memory only.
// ftpd made every field runtime-mutable so its SITE commands and its settings
// menu could edit them, and persisted the result to a .cfg on the SD card. That
// mutability is what forced the mutex this class used to carry, and the mutex is
// what put a lock in the session constructor and inside the per-directory-entry
// listing loop. Checkpoint owns config.json, creates the server with the values
// it wants and never changes them, so this is a plain value passed by const
// reference: no lock, no setters, no load/save, and no SITE command driving them.
//
// The Switch access-point settings are gone with the AP feature itself.

#pragma once

#include <cstdint>
#include <string>

/// \brief FTP settings
class FtpConfig
{
public:
	/// \brief Parameterized constructor
	/// \param port_ Listen port
	/// \note A port_ this console cannot bind falls back to the default; see
	///       validPort()
	explicit FtpConfig (std::uint16_t port_);

	/// \brief Whether a port can be bound on this console
	/// \note The Switch refuses ports below 1024 (except 0, meaning "any"); the
	///       3DS allows those but refuses 0
	static bool validPort (std::uint16_t port_);

	/// \brief Get user
	/// \note Empty means no authentication, which is what Checkpoint ships
	std::string const &user () const;

	/// \brief Get password
	/// \note Empty means no authentication, which is what Checkpoint ships
	std::string const &pass () const;

	/// \brief Get port
	std::uint16_t port () const;

#ifdef __3DS__
	/// \brief Whether listings resolve mtimes
	/// \note Only meaningful on 3DS, where an mtime costs an extra FSUSER round
	///       trip per directory entry and is the dominant cost of a listing
	bool getMTime () const;
#endif

private:
	/// \brief Username
	std::string m_user;

	/// \brief Password
	std::string m_pass;

	/// \brief Listen port
	std::uint16_t m_port;

#ifdef __3DS__
	/// \brief Whether to resolve mtimes
	bool m_getMTime = true;
#endif
};

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
// Adapted for Checkpoint: immutable in-memory settings (see ftpConfig.h).

#include "ftpConfig.h"

#include <cstdint>
#include <string>

namespace
{
/// \brief Listen port
/// \note 50000 is the port Checkpoint has always used; ftpd's own default is 5000
constexpr std::uint16_t DEFAULT_PORT = 50000;
}

///////////////////////////////////////////////////////////////////////////
FtpConfig::FtpConfig (std::uint16_t const port_)
    : m_port (validPort (port_) ? port_ : DEFAULT_PORT)
{
}

bool FtpConfig::validPort (std::uint16_t const port_)
{
#ifdef __SWITCH__
	// Switch is not allowed < 1024, except 0
	return port_ >= 1024 || port_ == 0;
#elif defined(__3DS__)
	// 3DS is allowed < 1024, but not 0
	return port_ != 0;
#else
	(void)port_;
	return true;
#endif
}

std::string const &FtpConfig::user () const
{
	return m_user;
}

std::string const &FtpConfig::pass () const
{
	return m_pass;
}

std::uint16_t FtpConfig::port () const
{
	return m_port;
}

#ifdef __3DS__
bool FtpConfig::getMTime () const
{
	return m_getMTime;
}
#endif

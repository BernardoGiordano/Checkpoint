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
// Adapted for Checkpoint: in-memory config only (see ftpConfig.h).

#include "ftpConfig.h"

#include "ftpPlatform.h"

#include <zlib.h>

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
/// \brief Listen port
/// \note 50000 is the port Checkpoint has always used; ftpd's own default is 5000
constexpr std::uint16_t DEFAULT_PORT = 50000;
constexpr int DEFAULT_DEFLATE_LEVEL  = 6;

template <typename T>
bool parseInt (T &out_, std::string_view const val_)
{
	auto const rc = std::from_chars (val_.data (), val_.data () + val_.size (), out_);
	if (rc.ec != std::errc ())
	{
		errno = static_cast<int> (rc.ec);
		return false;
	}

	if (rc.ptr != val_.data () + val_.size ())
	{
		errno = EINVAL;
		return false;
	}

	return true;
}
}

///////////////////////////////////////////////////////////////////////////
FtpConfig::~FtpConfig () = default;

FtpConfig::FtpConfig () : m_port (DEFAULT_PORT), m_deflateLevel (DEFAULT_DEFLATE_LEVEL)
{
}

UniqueFtpConfig FtpConfig::create ()
{
	return UniqueFtpConfig (new FtpConfig ());
}

std::scoped_lock<platform::Mutex> FtpConfig::lockGuard ()
{
	return std::scoped_lock<platform::Mutex> (m_lock);
}

bool FtpConfig::save ()
{
	return true;
}

std::string const &FtpConfig::user () const
{
	return m_user;
}

std::string const &FtpConfig::pass () const
{
	return m_pass;
}

std::string const &FtpConfig::hostname () const
{
	return m_hostname;
}

std::uint16_t FtpConfig::port () const
{
	return m_port;
}

int FtpConfig::deflateLevel () const
{
	return m_deflateLevel;
}

#ifdef __3DS__
bool FtpConfig::getMTime () const
{
	return m_getMTime;
}
#endif

void FtpConfig::setUser (std::string user_)
{
	m_user = std::move (user_);
}

void FtpConfig::setPass (std::string pass_)
{
	m_pass = std::move (pass_);
}

void FtpConfig::setHostname (std::string hostname_)
{
	m_hostname = std::move (hostname_);
}

bool FtpConfig::setPort (std::string_view const port_)
{
	std::uint16_t parsed{};
	if (!parseInt (parsed, port_))
		return false;

	return setPort (parsed);
}

bool FtpConfig::setPort (std::uint16_t const port_)
{
#ifdef __SWITCH__
	// Switch is not allowed < 1024, except 0
	if (port_ < 1024 && port_ != 0)
	{
		errno = EPERM;
		return false;
	}
#elif defined(__3DS__)
	// 3DS is allowed < 1024, but not 0
	if (port_ == 0)
	{
		errno = EPERM;
		return false;
	}
#endif

	m_port = port_;
	return true;
}

bool FtpConfig::setDeflateLevel (std::string_view const level_)
{
	int parsed;
	if (!parseInt (parsed, level_))
		return false;

	return setDeflateLevel (parsed);
}

bool FtpConfig::setDeflateLevel (int const level_)
{
	if (level_ < Z_NO_COMPRESSION || level_ > Z_BEST_COMPRESSION)
		return false;

	m_deflateLevel = level_;
	return true;
}

#ifdef __3DS__
void FtpConfig::setGetMTime (bool const getMTime_)
{
	m_getMTime = getMTime_;
}
#endif

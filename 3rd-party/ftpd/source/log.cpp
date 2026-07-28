// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// Copyright (C) 2023 Michael Theall
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
// Adapted for Checkpoint: forwards to Checkpoint's logger instead of keeping a
// ring buffer of its own. FTP messages are prefixed "[ftp]" so they read the
// same way the old 3rd-party/ftp core's did.
//
// Command/response traffic lands at trace level: it is one line per FTP verb,
// which is fine for a debug transcript but would drown the file log at info.

#include "log.h"

#include "logging.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace
{
/// \brief Forward one message to Checkpoint's logger
/// \param level_ Log level
/// \param message_ Message to log (trailing newlines are stripped)
void forward (FtpLogLevel const level_, std::string_view message_)
{
	while (!message_.empty () && (message_.back () == '\n' || message_.back () == '\r'))
		message_.remove_suffix (1);

	if (message_.empty ())
		return;

	auto message = std::string ("[ftp] ");
	message.append (message_);

	switch (level_)
	{
	case FTP_DEBUG:
		Logging::debug (message);
		break;

	case FTP_INFO:
		Logging::info (message);
		break;

	case FTP_ERROR:
		Logging::error (message);
		break;

	case FTP_COMMAND:
	case FTP_RESPONSE:
		Logging::trace (message);
		break;
	}
}
}

void debug (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (FTP_DEBUG, fmt_, ap);
	va_end (ap);
}

void info (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (FTP_INFO, fmt_, ap);
	va_end (ap);
}

void error (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (FTP_ERROR, fmt_, ap);
	va_end (ap);
}

void command (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (FTP_COMMAND, fmt_, ap);
	va_end (ap);
}

void response (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (FTP_RESPONSE, fmt_, ap);
	va_end (ap);
}

void addLog (FtpLogLevel const level_, char const *const fmt_, va_list ap_)
{
	thread_local static char buffer[1024];

	auto const rc = std::vsnprintf (buffer, sizeof (buffer), fmt_, ap_);
	if (rc <= 0)
		return;

	auto const size = static_cast<std::size_t> (rc) < sizeof (buffer) ?
	                      static_cast<std::size_t> (rc) :
	                      sizeof (buffer) - 1;

	forward (level_, std::string_view (buffer, size));
}

void addLog (FtpLogLevel const level_, std::string_view const message_)
{
	auto msg = std::string (message_);
	for (auto &c : msg)
	{
		// replace nul-characters with ? to avoid truncation
		if (c == '\0')
			c = '?';
	}

	forward (level_, msg);
}

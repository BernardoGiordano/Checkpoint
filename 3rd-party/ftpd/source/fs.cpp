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

#include "fs.h"
#include "ioBuffer.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace
{
int openFlags (fs::File::Mode const mode_)
{
	switch (mode_)
	{
	case fs::File::Mode::Read:
		return O_RDONLY;

	case fs::File::Mode::Truncate:
		return O_WRONLY | O_CREAT | O_TRUNC;

	case fs::File::Mode::Append:
		return O_WRONLY | O_CREAT | O_APPEND;

	case fs::File::Mode::Modify:
		// O_RDWR without O_CREAT is what fopen("r+b") asked for: this is the
		// REST case, where the file the client is resuming into must already
		// exist. Kept read-write rather than narrowed to O_WRONLY so the console
		// devoptabs see exactly the flags they saw before.
		return O_RDWR;
	}

	return O_RDONLY;
}
}

///////////////////////////////////////////////////////////////////////////
fs::File::~File ()
{
	close ();
}

fs::File::File () = default;

fs::File::File (File &&that_) noexcept : m_fd (that_.m_fd)
{
	that_.m_fd = -1;
}

fs::File &fs::File::operator= (File &&that_) noexcept
{
	if (&that_ != this)
	{
		close ();
		m_fd       = that_.m_fd;
		that_.m_fd = -1;
	}

	return *this;
}

bool fs::File::open (char const *const path_, Mode const mode_)
{
	close ();

	// 0666 is what fopen("wb") asked for; the umask still applies
	auto const fd = ::open (path_, openFlags (mode_), 0666);
	if (fd < 0)
		return false;

	m_fd = fd;
	return true;
}

void fs::File::close ()
{
	if (m_fd < 0)
		return;

	(void)::close (m_fd);
	m_fd = -1;
}

bool fs::File::seek (std::uint64_t const pos_)
{
	return ::lseek (m_fd, static_cast<off_t> (pos_), SEEK_SET) >= 0;
}

std::make_signed_t<std::size_t> fs::File::read (IOBuffer &buffer_)
{
	assert (buffer_.freeSize () > 0);

	auto const rc = ::read (m_fd, buffer_.freeArea (), buffer_.freeSize ());
	if (rc > 0)
		buffer_.markUsed (rc);

	return rc;
}

std::make_signed_t<std::size_t> fs::File::write (IOBuffer &buffer_)
{
	assert (buffer_.usedSize () > 0);

	auto const rc = ::write (m_fd, buffer_.usedArea (), buffer_.usedSize ());
	if (rc > 0)
		buffer_.markFree (rc);

	return rc;
}

///////////////////////////////////////////////////////////////////////////
fs::Dir::~Dir () = default;

fs::Dir::Dir () = default;

fs::Dir::Dir (Dir &&that_) = default;

fs::Dir &fs::Dir::operator= (Dir &&that_) = default;

fs::Dir::operator bool () const
{
	return static_cast<bool> (m_dp);
}

fs::Dir::operator DIR * () const
{
	return m_dp.get ();
}

bool fs::Dir::open (char const *const path_)
{
	auto const dp = ::opendir (path_);
	if (!dp)
		return false;

	m_dp = std::unique_ptr<DIR, int (*) (DIR *)> (dp, &::closedir);
	return true;
}

void fs::Dir::close ()
{
	m_dp.reset ();
}

dirent *fs::Dir::read ()
{
	errno = 0;
	return ::readdir (m_dp.get ());
}

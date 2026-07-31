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

#include "fs.h"
#include "ioBuffer.h"
#include "log.h"

#ifdef __3DS__
#include <3ds.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#if defined(__3DS__) || defined(__SWITCH__)
#define lstat stat
#endif

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

std::string joinPath (std::string const &dir_, char const *const name_)
{
	if (!dir_.empty () && dir_.back () == '/')
		return dir_ + name_;

	return dir_ + '/' + name_;
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

bool fs::Dir::open (char const *const path_, Detail const detail_, std::time_t const tzOffset_)
{
	auto const dp = ::opendir (path_);
	if (!dp)
		return false;

	m_dp       = std::unique_ptr<DIR, int (*) (DIR *)> (dp, &::closedir);
	m_path     = path_;
	m_detail   = detail_;
	m_tzOffset = tzOffset_;
	m_entry    = {};

	return true;
}

void fs::Dir::close ()
{
	m_dp.reset ();
	m_path.clear ();
	m_entry = {};
}

fs::Dir::Entry const *fs::Dir::read ()
{
	while (true)
	{
		errno           = 0;
		auto const dent = ::readdir (m_dp.get ());
		if (!dent)
			return nullptr;

		// the listing never reports . and ..
		if (std::strcmp (dent->d_name, ".") == 0 || std::strcmp (dent->d_name, "..") == 0)
			continue;

		m_entry = {};

		if (m_detail == Detail::Name)
		{
			m_entry.name = dent->d_name;
			return &m_entry;
		}

		if (!resolve (dent->d_name))
			continue;

		m_entry.name = dent->d_name;
		return &m_entry;
	}
}

bool fs::Dir::resolve (char const *const name_)
{
#ifdef __3DS__
	// The sdmc directory iterator already carries this entry's type and size, so
	// on that archive there is no reason to pay for a stat. Anything else (romfs,
	// a mounted archive that is not sdmc) falls through to the lstat below.
	auto const dp    = m_dp.get ();
	auto const magic = *reinterpret_cast<u32 *> (dp->dirData->dirStruct);

	if (magic == ARCHIVE_DIRITER_MAGIC)
	{
		auto const dir   = reinterpret_cast<archive_dir_t const *> (dp->dirData->dirStruct);
		auto const entry = &dir->entry_data[dir->index];

		if (entry->attributes & FS_ATTRIBUTE_DIRECTORY)
			m_entry.status.st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
		else
			m_entry.status.st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;

		if (!(entry->attributes & FS_ATTRIBUTE_READ_ONLY))
			m_entry.status.st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;

		m_entry.status.st_size  = entry->fileSize;
		m_entry.status.st_mtime = 0;

		// the one call the fast path cannot avoid, and the reason Detail exists
		if (m_detail == Detail::StatusMTime)
		{
			auto const path = joinPath (m_path, name_);

			std::uint64_t mtime = 0;
			auto const rc       = archive_getmtime (path.c_str (), &mtime);
			if (rc != 0)
				error ("sdmc_getmtime %s 0x%lx\n", path.c_str (), rc);
			else
				m_entry.status.st_mtime = mtime - m_tzOffset;
		}

		return true;
	}
#endif

	auto const path = joinPath (m_path, name_);

	if (::lstat (path.c_str (), &m_entry.status) != 0)
	{
		error ("Skipping %s: %s\n", path.c_str (), std::strerror (errno));
		return false;
	}

#ifdef __3DS__
	if (m_detail == Detail::StatusMTime)
	{
		std::uint64_t mtime = 0;
		auto const rc       = archive_getmtime (path.c_str (), &mtime);
		if (rc != 0)
			error ("sdmc_getmtime %s 0x%lx\n", path.c_str (), rc);
		else
			m_entry.status.st_mtime = mtime - m_tzOffset;
	}
#endif

	return true;
}

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

#pragma once

#include "ioBuffer.h"

#include <gsl/gsl>

#include <dirent.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

namespace fs
{
/// \brief Print size in human-readable format (KiB, MiB, etc)
/// \param size_ Size to print
std::string printSize (std::uint64_t size_);

/// \brief File I/O object
class File
{
public:
	~File ();

	File ();

	File (File const &that_) = delete;

	/// \brief Move constructor
	/// \param that_ Object to move from
	File (File &&that_);

	File &operator= (File const &that_) = delete;

	/// \brief Move assignment
	/// \param that_ Object to move from
	File &operator= (File &&that_);

	/// \brief bool cast operator
	explicit operator bool () const;

	/// \brief std::FILE* cast operator
	operator std::FILE * () const;

	/// \brief Set buffer size
	/// \param size_ Buffer size
	void setBufferSize (std::size_t size_);

	/// \brief Open file
	/// \param path_ Path to open
	/// \param mode_ Access mode (\sa std::fopen)
	bool open (gsl::not_null<gsl::czstring> path_, gsl::not_null<gsl::czstring> mode_ = "rb");

	/// \brief Close file
	void close ();

	/// \brief Seek to file position
	/// \param pos_ File position
	/// \param origin_ Reference position (\sa std::fseek)
	std::make_signed_t<std::size_t> seek (std::make_signed_t<std::size_t> pos_, int origin_);

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \param size_ Size to read
	/// \note Can return partial reads
	std::make_signed_t<std::size_t> read (gsl::not_null<void *> buffer_, std::size_t size_);

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \note Can return partial reads
	std::make_signed_t<std::size_t> read (IOBuffer &buffer_);

	/// \brief Read line
	std::string_view readLine ();

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \param size_ Size to read
	/// \note Fails on partial reads and errors
	bool readAll (gsl::not_null<void *> buffer_, std::size_t size_);

	/// \brief Write data
	/// \param buffer_ Input data
	/// \param size_ Size to write
	/// \note Can return partial writes
	std::make_signed_t<std::size_t> write (gsl::not_null<void const *> buffer_, std::size_t size_);

	/// \brief Write data
	/// \param buffer_ Input data
	/// \note Can return partial writes
	std::make_signed_t<std::size_t> write (IOBuffer &buffer_);

	/// \brief Write data
	/// \param buffer_ Input data
	/// \param size_ Size to write
	/// \note Fails on partials writes and errors
	bool writeAll (gsl::not_null<void const *> buffer_, std::size_t size_);

private:
	/// \brief Underlying std::FILE*
	std::unique_ptr<std::FILE, int (*) (std::FILE *)> m_fp{nullptr, nullptr};

	/// \brief Buffer
	std::vector<char> m_buffer;

	/// \brief Line buffer
	gsl::owner<char *> m_lineBuffer = nullptr;

	/// \brief Line buffer size
	std::size_t m_lineBufferSize = 0;
};

/// Directory object
class Dir
{
public:
	~Dir ();

	Dir ();

	Dir (Dir const &that_) = delete;

	/// \brief Move constructor
	/// \param that_ Object to move from
	Dir (Dir &&that_);

	Dir &operator= (Dir const &that_) = delete;

	/// \brief Move assignment
	/// \param that_ Object to move from
	Dir &operator= (Dir &&that_);

	/// \brief bool cast operator
	explicit operator bool () const;

	/// \brief DIR* cast operator
	operator DIR * () const;

	/// \brief Open directory
	/// \param path_ Path to open
	bool open (gsl::not_null<gsl::czstring> path_);

	/// \brief Close directory
	void close ();

	/// \brief Read a directory entry
	/// \note Returns nullptr on end-of-directory or error; check errno
	dirent *read ();

private:
	/// \brief Underlying DIR*
	std::unique_ptr<DIR, int (*) (DIR *)> m_dp{nullptr, nullptr};
};
}

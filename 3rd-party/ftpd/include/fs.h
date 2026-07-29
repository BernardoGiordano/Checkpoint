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

#include <dirent.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace fs
{
/// \brief File I/O object
class File
{
public:
	/// \brief Access mode
	enum class Mode
	{
		Read,     ///< Open an existing file for reading
		Truncate, ///< Create, or truncate an existing file, for writing
		Append,   ///< Create or open a file for writing at the end
		Modify,   ///< Open an existing file for writing without truncating it
	};

	~File ();

	File ();

	File (File const &that_) = delete;

	/// \brief Move constructor
	/// \param that_ Object to move from
	File (File &&that_) noexcept;

	File &operator= (File const &that_) = delete;

	/// \brief Move assignment
	/// \param that_ Object to move from
	File &operator= (File &&that_) noexcept;

	/// \brief Open file, closing whatever was open before
	/// \param path_ Path to open
	/// \param mode_ Access mode
	bool open (char const *path_, Mode mode_);

	/// \brief Close file
	void close ();

	/// \brief Seek to an absolute file position
	/// \param pos_ File position
	bool seek (std::uint64_t pos_);

	/// \brief Read into the buffer's free area
	/// \param buffer_ Output buffer
	/// \note Can return partial reads; 0 is end of file, negative is an error
	std::make_signed_t<std::size_t> read (IOBuffer &buffer_);

	/// \brief Write out the buffer's used area
	/// \param buffer_ Input buffer
	/// \note Can return partial writes; negative is an error
	std::make_signed_t<std::size_t> write (IOBuffer &buffer_);

private:
	/// \brief Underlying file descriptor
	int m_fd = -1;
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
	bool open (char const *path_);

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

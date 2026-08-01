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

#pragma once

#include "ioBuffer.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <type_traits>

using stat_t = struct stat;

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

/// \brief Directory listing reader
class Dir
{
public:
	/// \brief How much of each entry the caller needs
	enum class Detail
	{
		Name,        ///< Name only (NLST)
		Status,      ///< Name, mode and size
		StatusMTime, ///< Name, mode, size and mtime
	};

	/// \brief A directory entry
	struct Entry
	{
		/// \brief Entry name, with no path attached
		/// \note Only valid until the next read()
		char const *name = nullptr;

		/// \brief Entry status
		/// \note Zeroed at Detail::Name. st_mtime is only meaningful at
		///       Detail::StatusMTime.
		stat_t status{};
	};

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

	/// \brief Open directory
	/// \param path_ Path to open
	/// \param detail_ How much of each entry to resolve
	/// \param tzOffset_ Seconds to subtract from a resolved mtime (3DS only;
	///        elsewhere the mtime comes from lstat already in local terms)
	bool open (char const *path_, Detail detail_ = Detail::StatusMTime, std::time_t tzOffset_ = 0);

	/// \brief Close directory
	void close ();

	/// \brief Read the next entry, resolving it to the requested detail
	/// \note Returns nullptr at end of directory. "." and ".." are skipped, and
	///       so is any entry whose status cannot be resolved (logged, not
	///       reported, matching what the listing loop used to do inline).
	Entry const *read ();

private:
	/// \brief Fill m_entry's status for the entry readdir() is sitting on
	/// \param name_ Entry name
	/// \returns false if the entry should be skipped
	bool resolve (char const *name_);

	/// \brief Underlying DIR*
	std::unique_ptr<DIR, int (*) (DIR *)> m_dp{nullptr, nullptr};

	/// \brief Path this directory was opened with, for building entry paths
	std::string m_path;

	/// \brief Entry handed back by read()
	Entry m_entry;

	/// \brief How much of each entry to resolve
	Detail m_detail = Detail::StatusMTime;

	/// \brief Seconds to subtract from a resolved mtime
	std::time_t m_tzOffset = 0;
};
}

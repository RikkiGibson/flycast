// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// Portions Copyright 2026 The Hollycast Authors
//
// This file is part of Hollycast.
//
//     Hollycast is free software: you can redistribute it and/or modify
//     it under the terms of the GNU General Public License as published by
//     the Free Software Foundation, either version 2 of the License, or
//     (at your option) any later version.
//
//     Hollycast is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU General Public License for more details.
//
//     You should have received a copy of the GNU General Public License
//     along with Hollycast.  If not, see <https://www.gnu.org/licenses/>.
//============================================================
//
//  stdfile.cpp - Minimal core file access functions
//
//============================================================

#include "osdcore.h"
#include "osdfile.h"
#include "log/Log.h"
#include "oslib/storage.h"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace {

static bool seek_file(hostfs::File *file, std::uint64_t offset) noexcept
{
	return file->seek(offset, SEEK_SET) == 0;
}

static bool seek_file_end(hostfs::File *file) noexcept
{
	return file->seek(0, SEEK_END) == 0;
}

static std::int64_t tell_file(hostfs::File *file) noexcept
{
	return file->tell();
}

class std_osd_file : public osd_file
{
public:

	std_osd_file(hostfs::File *f, std::string path) noexcept : m_file(f), m_path(std::move(path))
	{
		assert(m_file);
	}

	//============================================================
	//  osd_close
	//============================================================

	virtual ~std_osd_file() override
	{
		// close the file handle
		if (m_file)
			delete m_file;
	}

	//============================================================
	//  osd_read
	//============================================================

	virtual std::error_condition read(void *buffer, std::uint64_t offset, std::uint32_t length, std::uint32_t &actual) noexcept override
	{
		if (!seek_file(m_file, offset))
		{
			ERROR_LOG(COMMON, "CHD stdfile seek failed: path='%s' offset=%llu errno=%d",
				m_path.c_str(), (unsigned long long)offset, errno);
			return std::error_condition(errno, std::generic_category());
		}

		// perform the read
		std::size_t const count = m_file->read(buffer, 1, length);
		if ((count < length) && m_file->error())
		{
			ERROR_LOG(COMMON, "CHD stdfile read failed: path='%s' offset=%llu length=%u errno=%d",
				m_path.c_str(), (unsigned long long)offset, length, errno);
			return std::error_condition(errno, std::generic_category());
		}
		actual = count;

		return std::error_condition();
	}

	//============================================================
	//  osd_write
	//============================================================

	virtual std::error_condition write(const void *buffer, std::uint64_t offset, std::uint32_t length, std::uint32_t &actual) noexcept override
	{
		if (!seek_file(m_file, offset))
		{
			ERROR_LOG(COMMON, "CHD stdfile seek failed: path='%s' offset=%llu errno=%d",
				m_path.c_str(), (unsigned long long)offset, errno);
			return std::error_condition(errno, std::generic_category());
		}

		// perform the write
		std::size_t const count = m_file->write(buffer, 1, length);
		if (count < length)
		{
			ERROR_LOG(COMMON, "CHD stdfile write failed: path='%s' offset=%llu length=%u errno=%d",
				m_path.c_str(), (unsigned long long)offset, length, errno);
			return std::error_condition(errno, std::generic_category());
		}
		actual = count;

		return std::error_condition();
	}

	//============================================================
	//  osd_truncate
	//============================================================

	virtual std::error_condition truncate(std::uint64_t offset) noexcept override
	{
		if (m_file->truncate(offset) < 0)
		{
			ERROR_LOG(COMMON, "CHD stdfile truncate failed: path='%s' offset=%llu errno=%d",
				m_path.c_str(), (unsigned long long)offset, errno);
			return std::error_condition(errno, std::generic_category());
		}
		return std::error_condition();
	}

	//============================================================
	//  osd_fflush
	//============================================================

	virtual std::error_condition flush() noexcept override
	{
		if (!m_file->flush())
			return std::error_condition();
		else
			return std::error_condition(errno, std::generic_category());
	}

private:
	hostfs::File *m_file;
	std::string m_path;
};

} // anonymous namespace


//============================================================
//  osd_open
//============================================================

std::error_condition osd_file::open(std::string const &path, std::uint32_t openflags, ptr &file, std::uint64_t &filesize) noexcept
{
	// based on the flags, choose a mode
	const char *mode;
	if (openflags & OPEN_FLAG_WRITE)
	{
		if (openflags & OPEN_FLAG_READ)
			mode = (openflags & OPEN_FLAG_CREATE) ? "w+b" : "r+b";
		else
			mode = "wb";
	}
	else if (openflags & OPEN_FLAG_READ)
		mode = "rb";
	else
		return std::errc::invalid_argument;

	// open the file
	hostfs::File *const fileptr = hostfs::storage().openFile(path, mode);
	if (!fileptr)
	{
		ERROR_LOG(COMMON, "CHD stdfile open failed: path='%s' mode='%s' errno=%d", path.c_str(), mode, errno);
		return std::error_condition(errno, std::generic_category());
	}

	const std::int64_t length = seek_file_end(fileptr) ? tell_file(fileptr) : -1;
	if ((length < 0) || !seek_file(fileptr, 0))
	{
		std::error_condition err(errno, std::generic_category());
		ERROR_LOG(COMMON, "CHD stdfile size probe failed: path='%s' mode='%s' errno=%d", path.c_str(), mode, errno);
		delete fileptr;
		return err;
	}

	osd_file::ptr result(new (std::nothrow) std_osd_file(fileptr, path));
	if (!result)
	{
		delete fileptr;
		return std::errc::not_enough_memory;
	}
	file = std::move(result);
	filesize = std::int64_t(length);
	return std::error_condition();
}


//============================================================
//  osd_openpty
//============================================================

std::error_condition osd_file::openpty(ptr &file, std::string &name) noexcept
{
	return std::errc::not_supported;
}


//============================================================
//  osd_rmfile
//============================================================

std::error_condition osd_file::remove(std::string const &filename) noexcept
{
	if (!std::remove(filename.c_str()))
		return std::error_condition();
	else
	{
		ERROR_LOG(COMMON, "CHD stdfile remove failed: path='%s' errno=%d", filename.c_str(), errno);
		return std::error_condition(errno, std::generic_category());
	}
}


//============================================================
//  osd_get_physical_drive_geometry
//============================================================

bool osd_get_physical_drive_geometry(const char *filename, uint32_t *cylinders, uint32_t *heads, uint32_t *sectors, uint32_t *bps) noexcept
{
	// there is no standard way of doing this, so we always return false, indicating
	// that a given path is not a physical drive
	return false;
}


//============================================================
//  osd_stat
//============================================================

osd::directory::entry::ptr osd_stat(const std::string &path)
{
	// create an osd_directory_entry; be sure to make sure that the caller can
	// free all resources by just freeing the resulting osd_directory_entry
	auto const result = reinterpret_cast<osd::directory::entry *>(
			::operator new(
				sizeof(osd::directory::entry) + path.length() + 1,
				std::align_val_t(alignof(osd::directory::entry)),
				std::nothrow));
	if (!result) return nullptr;
	new (result) osd::directory::entry;

	auto const resultname = reinterpret_cast<char *>(result) + sizeof(*result);
	std::strcpy(resultname, path.c_str());
	result->name = resultname;
	result->type = osd::directory::entry::entry_type::NONE;
	result->size = 0;

	hostfs::File *const f = hostfs::storage().openFile(path, "rb");
	if (f)
	{
		seek_file_end(f);
		result->type = osd::directory::entry::entry_type::FILE;
		result->size = tell_file(f);
		delete f;
	}

	return osd::directory::entry::ptr(result);
}


//============================================================
//  osd_get_full_path
//============================================================

std::error_condition osd_get_full_path(std::string &dst, std::string const &path) noexcept
{
	// derive the full path of the file in an allocated string
	// for now just fake it since we don't presume any underlying file system
	try { dst = path; }
	catch (...) { return std::errc::not_enough_memory; }

	return std::error_condition();
}


//============================================================
//  osd_is_absolute_path
//============================================================

bool osd_is_absolute_path(std::string const &path) noexcept
{
	// assume no for everything
	return false;
}


//============================================================
//  osd_get_volume_name
//============================================================

std::string osd_get_volume_name(int idx)
{
	// we don't expose volumes
	return std::string();
}


//============================================================
//  osd_get_volume_names
//============================================================

std::vector<std::string> osd_get_volume_names()
{
	// we don't expose volumes
	return std::vector<std::string>();
}

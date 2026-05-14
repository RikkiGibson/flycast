// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// Portions Copyright 2026 The Hollycast Authors
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

#include <stdio.h>  // for fileno
#ifdef _WIN32
#include <io.h> // for _chsize_s
#else
#include <unistd.h> // for ftruncate
#endif


namespace {

#ifdef _WIN32
static bool seek_file(FILE *file, std::uint64_t offset) noexcept
{
	return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
}

static bool seek_file_end(FILE *file) noexcept
{
	return _fseeki64(file, 0, SEEK_END) == 0;
}

static std::int64_t tell_file(FILE *file) noexcept
{
	return _ftelli64(file);
}
#else
static bool seek_file(FILE *file, std::uint64_t offset) noexcept
{
	if (std::numeric_limits<long>::max() < offset)
		return false;
	return std::fseek(file, offset, SEEK_SET) == 0;
}

static bool seek_file_end(FILE *file) noexcept
{
	return std::fseek(file, 0, SEEK_END) == 0;
}

static std::int64_t tell_file(FILE *file) noexcept
{
	return std::ftell(file);
}
#endif

class std_osd_file : public osd_file
{
public:

	std_osd_file(FILE *f, std::string path) noexcept : m_file(f), m_path(std::move(path))
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
			std::fclose(m_file);
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
		std::size_t const count = std::fread(buffer, 1, length, m_file);
		if ((count < length) && std::ferror(m_file))
		{
			std::clearerr(m_file);
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
		std::size_t const count = std::fwrite(buffer, 1, length, m_file);
		if (count < length)
		{
			std::clearerr(m_file);
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
#ifdef _WIN32
		if (_chsize_s(_fileno(m_file), offset) < 0)
		{
			ERROR_LOG(COMMON, "CHD stdfile truncate failed: path='%s' offset=%llu errno=%d",
				m_path.c_str(), (unsigned long long)offset, errno);
			return std::error_condition(errno, std::generic_category());
		}
#else
		if (::ftruncate(::fileno(m_file), offset) < 0)
		{
			ERROR_LOG(COMMON, "CHD stdfile truncate failed: path='%s' offset=%llu errno=%d",
				m_path.c_str(), (unsigned long long)offset, errno);
			return std::error_condition(errno, std::generic_category());
		}
#endif
		return std::error_condition();
	}

	//============================================================
	//  osd_fflush
	//============================================================

	virtual std::error_condition flush() noexcept override
	{
		if (!std::fflush(m_file))
			return std::error_condition();
		else
			return std::error_condition(errno, std::generic_category());
	}

private:
	FILE *m_file;
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
	FILE *const fileptr = hostfs::storage().openFile(path, mode);
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
		std::fclose(fileptr);
		return err;
	}

	osd_file::ptr result(new (std::nothrow) std_osd_file(fileptr, path));
	if (!result)
	{
		std::fclose(fileptr);
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

	FILE *const f = hostfs::storage().openFile(path, "rb");
	if (f)
	{
		seek_file_end(f);
		result->type = osd::directory::entry::entry_type::FILE;
		result->size = tell_file(f);
		std::fclose(f);
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

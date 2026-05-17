/*
	Copyright 2024 flyinghead
	Portions Copyright 2026 The Hollycast Authors

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "chdwriter_bridge.h"

#include "log/Log.h"
#include "upstream/lib_util/cdrom.h"
#include "upstream/lib_util/chd.h"
#include "upstream/lib_util/corefile.h"
#include "upstream/osd/osdfile.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include <memory>
#include <system_error>
#include <tuple>

namespace chdwriter_mame {
namespace {

class rawfile_compressor : public chd_file_compressor
{
public:
	rawfile_compressor(util::random_read &file, std::uint64_t offset = 0, std::uint64_t maxoffset = std::numeric_limits<std::uint64_t>::max())
		: m_file(file)
		, m_offset(offset)
	{
		std::uint64_t filelen = 0;
		if (!file.length(filelen))
			m_maxoffset = (std::min)(maxoffset, filelen);
		else
			m_maxoffset = maxoffset;
	}

	virtual std::uint32_t read_data(void *dest, std::uint64_t offset, std::uint32_t length) override
	{
		offset += m_offset;
		if (offset >= m_maxoffset)
			return 0;
		if (offset + length > m_maxoffset)
			length = m_maxoffset - offset;
		if (m_file.seek(offset, SEEK_SET))
		{
			ERROR_LOG(COMMON, "CHD raw read seek failed: offset=%llu length=%u maxoffset=%llu",
				(unsigned long long)offset, length, (unsigned long long)m_maxoffset);
			throw std::make_error_condition(std::errc::io_error);
		}
		auto const [err, actual] = read(m_file, dest, length);
		if (err)
		{
			ERROR_LOG(COMMON, "CHD raw read failed: offset=%llu length=%u message='%s'",
				(unsigned long long)offset, length, err.message().c_str());
			throw err;
		}
		if (actual != length)
		{
			WARN_LOG(COMMON, "CHD raw short read: offset=%llu requested=%u actual=%u",
				(unsigned long long)offset, length, (unsigned)actual);
			throw std::make_error_condition(std::errc::io_error);
		}
		return actual;
	}

private:
	util::random_read& m_file;
	std::uint64_t m_offset;
	std::uint64_t m_maxoffset;
};

class cd_compressor : public chd_file_compressor
{
public:
	cd_compressor(cdrom_file::toc &toc, cdrom_file::track_input_info &info)
		: m_toc(toc)
		, m_info(info)
	{
	}

	virtual uint32_t read_data(void *_dest, uint64_t offset, uint32_t length) override
	{
		if (!m_loggedFirstRead)
		{
			m_loggedFirstRead = true;
			NOTICE_LOG(COMMON, "CHD CD read begin: offset=%llu length=%u tracks=%u",
				(unsigned long long)offset, length, m_toc.numtrks);
		}
		uint8_t *dest = reinterpret_cast<uint8_t *>(_dest);
		memset(dest, 0, length);
		uint32_t length_remaining = length;
		uint64_t startoffs = 0;

		for (int tracknum = 0; tracknum < m_toc.numtrks; tracknum++)
		{
			const cdrom_file::track_info& trackinfo = m_toc.tracks[tracknum];
			const uint64_t endoffs = startoffs + (uint64_t)(trackinfo.frames + trackinfo.extraframes) * cdrom_file::FRAME_SIZE;
			if (!(offset >= startoffs && offset < endoffs))
			{
				startoffs = endoffs;
				continue;
			}

			if (!m_file || m_lastfile != m_info.track[tracknum].fname)
			{
				m_file.reset();
				m_lastfile = m_info.track[tracknum].fname;
				NOTICE_LOG(COMMON, "CHD CD opening track file: track=%d path='%s'", tracknum, m_lastfile.c_str());
				const std::error_condition openErr = util::core_file::open(m_lastfile, OPEN_FLAG_READ, m_file);
				if (openErr)
				{
					ERROR_LOG(COMMON, "CHD CD track open failed: track=%d path='%s' message='%s'",
						tracknum, m_lastfile.c_str(), openErr.message().c_str());
					throw openErr;
				}
			}

			const uint64_t bytesperframe = trackinfo.datasize + trackinfo.subsize;
			const uint64_t src_track_start = m_info.track[tracknum].offset;
			const uint64_t src_track_end = src_track_start + bytesperframe * (uint64_t)trackinfo.frames;
			const uint64_t split_track_start = src_track_end - ((uint64_t)trackinfo.splitframes * bytesperframe);
			const uint64_t pad_track_start = split_track_start - ((uint64_t)trackinfo.padframes * bytesperframe);
			const uint64_t split_or_max = (uint64_t)trackinfo.splitframes == 0 ? UINT64_MAX : split_track_start;

			while (length_remaining != 0 && offset < endoffs)
			{
				const uint64_t src_frame_start = src_track_start + ((offset - startoffs) / cdrom_file::FRAME_SIZE) * bytesperframe;
				if (src_frame_start >= split_or_max
					&& src_frame_start < src_track_end
					&& (tracknum + 1) < m_toc.numtrks
					&& m_lastfile != m_info.track[tracknum + 1].fname)
				{
					m_file.reset();
					m_lastfile = m_info.track[tracknum + 1].fname;
					NOTICE_LOG(COMMON, "CHD CD switching split track file: fromTrack=%d toTrack=%d path='%s'",
						tracknum, tracknum + 1, m_lastfile.c_str());
					const std::error_condition openErr = util::core_file::open(m_lastfile, OPEN_FLAG_READ, m_file);
					if (openErr)
					{
						ERROR_LOG(COMMON, "CHD CD split track open failed: track=%d path='%s' message='%s'",
							tracknum + 1, m_lastfile.c_str(), openErr.message().c_str());
						throw openErr;
					}
				}

				if (src_frame_start < src_track_end)
				{
					if (src_frame_start >= pad_track_start && src_frame_start < split_or_max)
					{
						memset(dest, 0, bytesperframe);
					}
					else
					{
						const std::uint64_t seekTo = src_frame_start >= split_or_max ? src_frame_start - split_or_max : src_frame_start;
						std::error_condition err = m_file->seek(seekTo, SEEK_SET);
						std::size_t count = 0;
						if (!err)
							std::tie(err, count) = read(*m_file, dest, bytesperframe);
						if (err || count != bytesperframe)
						{
							ERROR_LOG(COMMON, "CHD CD track read failed: track=%d file='%s' seek=%llu requested=%u actual=%u message='%s'",
								tracknum, m_lastfile.c_str(), (unsigned long long)seekTo, (unsigned)bytesperframe, (unsigned)count,
								err ? err.message().c_str() : "short read");
							throw err ? err : std::make_error_condition(std::errc::io_error);
						}
					}

					if (m_info.track[tracknum].swap)
					{
						for (uint32_t swapindex = 0; swapindex < 2352; swapindex += 2)
							std::swap(dest[swapindex], dest[swapindex + 1]);
					}
				}

				offset += cdrom_file::FRAME_SIZE;
				dest += cdrom_file::FRAME_SIZE;
				length_remaining -= cdrom_file::FRAME_SIZE;
			}

			startoffs = endoffs;
		}

		return length - length_remaining;
	}

private:
	bool m_loggedFirstRead = false;
	std::string m_lastfile;
	util::core_file::ptr m_file;
	cdrom_file::toc& m_toc;
	cdrom_file::track_input_info& m_info;
};

static std::error_condition create_output_chd(
	chd_file_compressor &compressor,
	std::string_view path,
	uint64_t logical_size,
	uint32_t hunk_size,
	uint32_t unit_size,
	const chd_codec_type (&compression)[4])
{
	return compressor.create(path, logical_size, hunk_size, unit_size, compression);
}

static std::error_condition run_compression(chd_file_compressor &chd,
	const std::function<void(double complete, double ratio, const std::string& phase)>& progressCallback)
{
	NOTICE_LOG(COMMON, "CHD compression begin");
	chd.compress_begin();
	double complete = 0.0;
	double ratio = 0.0;
	std::error_condition err;
	unsigned iteration = 0;
	if (progressCallback)
		progressCallback(complete, ratio, "Starting compression...");
	while ((err = chd.compress_continue(complete, ratio)) == chd_file::error::WALKING_PARENT || err == chd_file::error::COMPRESSING)
	{
		++iteration;
		if (iteration <= 4 || (iteration % 128) == 0)
		{
			NOTICE_LOG(COMMON, "CHD compression progress: iteration=%u complete=%.4f ratio=%.4f state='%s'",
				iteration, complete, ratio, err.message().c_str());
		}
		if (progressCallback)
			progressCallback(complete, ratio, err.message());
	}
	if (err)
		ERROR_LOG(COMMON, "CHD compression end: iteration=%u complete=%.4f ratio=%.4f message='%s'",
			iteration, complete, ratio, err.message().c_str());
	else
		NOTICE_LOG(COMMON, "CHD compression end: iteration=%u complete=%.4f ratio=%.4f",
			iteration, complete, ratio);
	return err;
}

static std::error_condition write_cd_metadata(chd_file *chd, const cdrom_file::toc &toc)
{
	for (int i = 0; i < toc.numtrks; i++)
	{
		const cdrom_file::track_info &track = toc.tracks[i];
		char metadata[256];
		chd_metadata_tag tag = CDROM_TRACK_METADATA2_TAG;
		if (toc.flags & cdrom_file::CD_FLAG_GDROM)
		{
			std::snprintf(metadata, sizeof(metadata), GDROM_TRACK_METADATA_FORMAT, i + 1,
				cdrom_file::get_type_string(track.trktype), cdrom_file::get_subtype_string(track.subtype),
				track.frames, track.padframes, track.pregap, cdrom_file::get_type_string(track.pgtype),
				cdrom_file::get_subtype_string(track.pgsub), track.postgap);
			tag = GDROM_TRACK_METADATA_TAG;
		}
		else
		{
			const char *pgtype = track.pregap != 0 ? cdrom_file::get_type_string(track.pgtype) : "NONE";
			const char *pgsub = track.pregap != 0 ? cdrom_file::get_subtype_string(track.pgsub) : "NONE";
			std::snprintf(metadata, sizeof(metadata), CDROM_TRACK_METADATA2_FORMAT, i + 1,
				cdrom_file::get_type_string(track.trktype), cdrom_file::get_subtype_string(track.subtype),
				track.frames, track.pregap, pgtype, pgsub, track.postgap);
		}
		std::error_condition err = chd->write_metadata(tag, i, metadata);
		if (err)
			return err;
	}
	return std::error_condition();
}

static bool runCdConversion(const std::string& inputPath, const std::string& outputPath, std::string& errorMessage,
	const std::function<void(double complete, double ratio, const std::string& phase)>& progressCallback)
{
	NOTICE_LOG(COMMON, "CHD CD conversion begin: input='%s' output='%s'", inputPath.c_str(), outputPath.c_str());
	cdrom_file::track_input_info track_info;
	cdrom_file::toc toc = { 0 };
	std::error_condition err = cdrom_file::parse_toc(inputPath, toc, track_info);
	if (err)
	{
		errorMessage = err.message();
		ERROR_LOG(COMMON, "CHD CD conversion parse failed: input='%s' message='%s'", inputPath.c_str(), errorMessage.c_str());
		return false;
	}

	uint32_t totalSectors = 0;
	for (int tracknum = 0; tracknum < toc.numtrks; tracknum++)
	{
		cdrom_file::track_info &trackinfo = toc.tracks[tracknum];
		int padded = (trackinfo.frames + cdrom_file::TRACK_PADDING - 1) / cdrom_file::TRACK_PADDING;
		trackinfo.extraframes = padded * cdrom_file::TRACK_PADDING - trackinfo.frames;
		totalSectors += trackinfo.frames + trackinfo.extraframes;
		NOTICE_LOG(COMMON, "CHD CD track info: track=%d frames=%u extra=%u data=%u sub=%u file='%s'",
			tracknum, trackinfo.frames, trackinfo.extraframes, trackinfo.datasize, trackinfo.subsize,
			track_info.track[tracknum].fname.c_str());
	}

	const uint32_t hunk_size = cdrom_file::FRAMES_PER_HUNK * cdrom_file::FRAME_SIZE;
	const chd_codec_type compression[4] = { CHD_CODEC_CD_ZLIB, CHD_CODEC_ZLIB, CHD_CODEC_HUFFMAN, CHD_CODEC_NONE };
	auto chd = std::make_unique<cd_compressor>(toc, track_info);
	err = create_output_chd(*chd, outputPath, (uint64_t)totalSectors * cdrom_file::FRAME_SIZE, hunk_size, cdrom_file::FRAME_SIZE, compression);
	if (err)
	{
		errorMessage = err.message();
		ERROR_LOG(COMMON, "CHD CD conversion create failed: output='%s' message='%s'", outputPath.c_str(), errorMessage.c_str());
		return false;
	}

	err = write_cd_metadata(chd.get(), toc);
	if (err)
	{
		errorMessage = err.message();
		osd_file::remove(outputPath);
		ERROR_LOG(COMMON, "CHD CD conversion metadata failed: output='%s' message='%s'", outputPath.c_str(), errorMessage.c_str());
		return false;
	}

	err = run_compression(*chd, progressCallback);
	if (err)
	{
		errorMessage = err.message();
		osd_file::remove(outputPath);
		ERROR_LOG(COMMON, "CHD CD conversion compression failed: output='%s' message='%s'", outputPath.c_str(), errorMessage.c_str());
		return false;
	}

	NOTICE_LOG(COMMON, "CHD CD conversion success: output='%s' tracks=%u sectors=%u", outputPath.c_str(), toc.numtrks, totalSectors);
	return true;
}

static bool runDvdConversion(const std::string& inputPath, const std::string& outputPath, std::string& errorMessage,
	const std::function<void(double complete, double ratio, const std::string& phase)>& progressCallback)
{
	NOTICE_LOG(COMMON, "CHD DVD conversion begin: input='%s' output='%s'", inputPath.c_str(), outputPath.c_str());
	util::core_file::ptr inputFile;
	std::error_condition err = util::core_file::open(inputPath, OPEN_FLAG_READ, inputFile);
	if (err)
	{
		errorMessage = err.message();
		ERROR_LOG(COMMON, "CHD DVD conversion open failed: input='%s' message='%s'", inputPath.c_str(), errorMessage.c_str());
		return false;
	}

	std::uint64_t inputSize = 0;
	if (inputFile->length(inputSize))
	{
		errorMessage = "Unable to read input size.";
		ERROR_LOG(COMMON, "CHD DVD conversion length failed: input='%s' message='%s'", inputPath.c_str(), errorMessage.c_str());
		return false;
	}
	if (inputSize % 2048)
	{
		errorMessage = "Input size is not divisible by DVD sector size (2048).";
		ERROR_LOG(COMMON, "CHD DVD conversion rejected: input='%s' size=%llu message='%s'",
			inputPath.c_str(), (unsigned long long)inputSize, errorMessage.c_str());
		return false;
	}

	const uint32_t hunk_size = 2 * 2048;
	const chd_codec_type compression[4] = { CHD_CODEC_ZLIB, CHD_CODEC_HUFFMAN, CHD_CODEC_NONE, CHD_CODEC_NONE };
	auto chd = std::make_unique<rawfile_compressor>(*inputFile, 0, inputSize);
	err = create_output_chd(*chd, outputPath, inputSize, hunk_size, 2048, compression);
	if (err)
	{
		errorMessage = err.message();
		ERROR_LOG(COMMON, "CHD DVD conversion create failed: output='%s' message='%s'", outputPath.c_str(), errorMessage.c_str());
		return false;
	}

	err = chd->write_metadata(DVD_METADATA_TAG, 0, "");
	if (err)
	{
		errorMessage = err.message();
		osd_file::remove(outputPath);
		ERROR_LOG(COMMON, "CHD DVD conversion metadata failed: output='%s' message='%s'", outputPath.c_str(), errorMessage.c_str());
		return false;
	}

	err = run_compression(*chd, progressCallback);
	if (err)
	{
		errorMessage = err.message();
		osd_file::remove(outputPath);
		ERROR_LOG(COMMON, "CHD DVD conversion compression failed: output='%s' message='%s'", outputPath.c_str(), errorMessage.c_str());
		return false;
	}

	NOTICE_LOG(COMMON, "CHD DVD conversion success: output='%s' size=%llu", outputPath.c_str(), (unsigned long long)inputSize);
	return true;
}

} // namespace

bool runConversion(Mode mode, const std::string& inputPath, const std::string& outputPath, std::string& errorMessage,
	const std::function<void(double complete, double ratio, const std::string& phase)>& progressCallback)
{
	try
	{
		NOTICE_LOG(COMMON, "CHD runConversion dispatch: mode=%s input='%s' output='%s'",
			mode == Mode::CreateCd ? "cd" : "dvd", inputPath.c_str(), outputPath.c_str());
		if (mode == Mode::CreateCd)
			return runCdConversion(inputPath, outputPath, errorMessage, progressCallback);
		return runDvdConversion(inputPath, outputPath, errorMessage, progressCallback);
	}
	catch (const std::error_condition& e)
	{
		errorMessage = e.message();
		return false;
	}
	catch (const std::exception& e)
	{
		errorMessage = e.what();
		return false;
	}
	catch (...)
	{
		errorMessage = "Unknown conversion error.";
		return false;
	}
}

} // namespace chdwriter_mame

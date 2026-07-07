/*
	Portions Copyright 2026 The Hollycast Authors

	This file is part of Hollycast.

    Hollycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Hollycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Hollycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <functional>
#include <string>

namespace chdwriter_mame {

enum class Mode
{
	CreateCd,
	CreateDvd,
};

enum class CompressionProfile
{
	Fast,
	Balanced,
	HighCompression,
	MaxArchive,
};

#ifdef __ANDROID__
static constexpr CompressionProfile kDefaultCompressionProfile = CompressionProfile::Fast;
#else
static constexpr CompressionProfile kDefaultCompressionProfile = CompressionProfile::Balanced;
#endif

const char *describeCompressionProfile(CompressionProfile profile);
const char *describeCompressionStack(Mode mode, CompressionProfile profile);

bool runConversion(Mode mode, const std::string& inputPath, const std::string& outputPath, std::string& errorMessage,
	const std::function<void(double complete, double ratio, const std::string& phase)>& progressCallback = {},
	CompressionProfile compressionProfile = kDefaultCompressionProfile,
	const std::function<bool()>& cancelCallback = {});

} // namespace chdwriter_mame

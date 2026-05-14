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
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace chdconvert {

enum class SourceKind
{
	Unknown,
	DreamcastGdi,
	CueBin,
	Iso,
	Directory,
};

struct SourceProbe
{
	std::string sourcePath;
	std::string primaryFile;
	std::string summary;
	SourceKind kind = SourceKind::Unknown;
	bool isDirectory = false;
	bool supportedByPlan = false;
	bool isDreamcastCandidate = false;
	bool isGenericCdCandidate = false;
	std::vector<std::string> notes;
};

struct ConversionPlan
{
	SourceProbe probe;
	std::string mode;
	bool canRunNow = false;
};

struct ConversionOptions
{
	std::string outputDirectory;
	std::function<void(double complete, double ratio, const std::string& phase)> progressCallback;
};

struct ConversionResult
{
	bool success = false;
	int exitCode = -1;
	std::string sourcePath;
	std::string outputPath;
	std::string commandLine;
	std::string message;
};

SourceProbe probeSource(const std::string& path);
ConversionPlan planConversion(const std::string& path);
ConversionResult runSingleConversion(const std::string& path, const ConversionOptions& options);
std::vector<std::string> collectConvertibleSources(const std::string& path, bool scanSubdirectories);
std::vector<std::string> collectSourceSetPaths(const std::string& path);
const char *describeSourceKind(SourceKind kind);

} // namespace chdconvert

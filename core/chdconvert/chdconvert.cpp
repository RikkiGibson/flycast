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
#include "chdconvert.h"

#include "chdwriter_mame/chdwriter_bridge.h"
#include "chdwriter_mame/upstream/lib_util/cdrom.h"
#include "log/Log.h"
#include "oslib/storage.h"
#include "stdclass.h"

#include <algorithm>
#include <array>
#include <exception>
#include <set>

namespace chdconvert {
namespace {

static std::string extensionFromNameOrPath(const std::string& name, const std::string& path)
{
	std::string ext = get_file_extension(name);
	if (ext.empty())
		ext = get_file_extension(path);
	return ext;
}

static int candidateRank(const std::string& ext)
{
	if (ext == "gdi")
		return 0;
	if (ext == "cue")
		return 1;
	if (ext == "iso")
		return 2;
	if (ext == "bin")
		return 3;
	return 99;
}

static std::string normalizeInputPath(const std::string& raw)
{
	std::string path = trim_ws(raw);
	if (path.size() >= 2)
	{
		const bool quoted = (path.front() == '"' && path.back() == '"') || (path.front() == '\'' && path.back() == '\'');
		if (quoted)
			path = path.substr(1, path.size() - 2);
	}
	return trim_ws(path);
}

static SourceKind kindForExtension(const std::string& ext)
{
	if (ext == "gdi")
		return SourceKind::DreamcastGdi;
	if (ext == "cue")
		return SourceKind::CueBin;
	if (ext == "iso")
		return SourceKind::Iso;
	return SourceKind::Unknown;
}

static std::string firstCandidateFile(const std::string& root)
{
	std::string bestPath;
	int bestRank = 99;
	try
	{
		const std::vector<hostfs::FileInfo> entries = hostfs::storage().listContent(root);
		int supportedFiles = 0;
		for (const auto& entry : entries)
		{
			if (entry.isDirectory)
				continue;
			const std::string ext = extensionFromNameOrPath(entry.name, entry.path);
			const int rank = candidateRank(ext);
			if (rank == 99)
				continue;
			supportedFiles++;
			if (rank < bestRank)
			{
				bestRank = rank;
				bestPath = entry.path;
				if (bestRank == 0)
					break;
			}
		}
		NOTICE_LOG(COMMON, "CHD probe directory listing: path='%s' entries=%d supportedFiles=%d bestRank=%d",
			root.c_str(), (int)entries.size(), supportedFiles, bestRank);
		if (!bestPath.empty())
			return bestPath;
	}
	catch (const std::exception& e)
	{
		WARN_LOG(COMMON, "CHD probe directory listing failed: path='%s' message='%s'", root.c_str(), e.what());
	}
	catch (...)
	{
		WARN_LOG(COMMON, "CHD probe directory listing failed: path='%s' message=unknown", root.c_str());
	}

	for (const auto& entry : hostfs::DirectoryTree(root))
	{
		if (entry.isDirectory)
			continue;
		const std::string ext = extensionFromNameOrPath(entry.name, entry.path);
		const int rank = candidateRank(ext);
		if (rank < bestRank)
		{
			bestRank = rank;
			bestPath = entry.path;
			if (bestRank == 0)
				break;
		}
	}
	return bestPath;
}

static bool isPreferredRootImage(const std::string& ext)
{
	return ext == "gdi" || ext == "cue" || ext == "iso";
}

static void addWriterNote(SourceProbe& probe)
{
	probe.notes.push_back("Internal CHD conversion backend mode is enabled for this pass.");
	probe.notes.push_back("Selectable compression profiles are available for this source.");
}

static chdwriter_mame::Mode writerModeForKind(SourceKind kind)
{
	return kind == SourceKind::Iso ? chdwriter_mame::Mode::CreateDvd : chdwriter_mame::Mode::CreateCd;
}

static chdwriter_mame::CompressionProfile writerCompressionProfile(CompressionProfile profile)
{
	switch (profile)
	{
	case CompressionProfile::Fast:
		return chdwriter_mame::CompressionProfile::Fast;
	case CompressionProfile::Balanced:
		return chdwriter_mame::CompressionProfile::Balanced;
	case CompressionProfile::HighCompression:
		return chdwriter_mame::CompressionProfile::HighCompression;
	case CompressionProfile::MaxArchive:
		return chdwriter_mame::CompressionProfile::MaxArchive;
	}
	return chdwriter_mame::CompressionProfile::Balanced;
}

static std::string normalizeDirectoryPath(const std::string& path)
{
	std::string normalized = normalizeInputPath(path);
	while (normalized.size() > 3 && (normalized.back() == '/' || normalized.back() == '\\'))
		normalized.pop_back();
	return normalized;
}

static bool resolveOutputPathForSource(const std::string& sourcePath, const std::string& outputDirectory, const std::string& outputFileSuffix,
	std::string& resolvedPath, std::string& errorMessage)
{
	const std::string baseName = get_file_basename(hostfs::storage().getFileInfo(sourcePath).name) + outputFileSuffix + ".chd";
	const std::string normalizedOutputDirectory = normalizeDirectoryPath(outputDirectory);
	if (normalizedOutputDirectory.empty())
	{
		errorMessage = "Select an output folder before converting.";
		return false;
	}

	try
	{
		const hostfs::FileInfo outputInfo = hostfs::storage().getFileInfo(normalizedOutputDirectory);
		if (!outputInfo.isDirectory)
		{
			errorMessage = "Selected output path is not a folder.";
			return false;
		}
		if (!outputInfo.isWritable)
		{
			errorMessage = "Selected output folder is not writable.";
			return false;
		}
	}
	catch (const std::exception& e)
	{
		errorMessage = strprintf("Unable to use the selected output folder: %s", e.what());
		return false;
	}
	catch (...)
	{
		errorMessage = "Unable to use the selected output folder.";
		return false;
	}

	resolvedPath = hostfs::storage().getSubPath(normalizedOutputDirectory, baseName);
	NOTICE_LOG(COMMON, "CHD output path resolved: source='%s' outputDirectory='%s' resolved='%s'",
		sourcePath.c_str(), normalizedOutputDirectory.c_str(), resolvedPath.c_str());
	return true;
}

static SourceProbe probeDirectory(const std::string& path)
{
	SourceProbe probe;
	probe.sourcePath = path;
	probe.isDirectory = true;
	probe.kind = SourceKind::Directory;
	probe.summary = "Directory scan queued for disc-image detection.";
	probe.notes.push_back("This pass only identifies likely disc-image entry points.");

	const std::string candidate = firstCandidateFile(path);
	if (candidate.empty())
	{
		NOTICE_LOG(COMMON, "CHD probe directory: path='%s' candidate=none", path.c_str());
		probe.summary = "No supported disc-image file found in the directory yet.";
		probe.notes.push_back("No .gdi, .cue, .bin, or .iso entry found.");
		probe.notes.push_back("If this is a library root, open a game folder or enable subfolder scanning before running.");
		return probe;
	}

	probe.primaryFile = candidate;
	const hostfs::FileInfo candidateInfo = hostfs::storage().getFileInfo(candidate);
	probe.kind = kindForExtension(extensionFromNameOrPath(candidateInfo.name, candidate));
	if (probe.kind == SourceKind::Unknown && extensionFromNameOrPath(candidateInfo.name, candidate) == "bin")
	{
		const std::string cuePath = hostfs::storage().getSubPath(hostfs::storage().getParentPath(candidate),
			get_file_basename(candidateInfo.name) + ".cue");
		if (hostfs::storage().exists(cuePath))
		{
			probe.primaryFile = cuePath;
			probe.kind = SourceKind::CueBin;
			probe.notes.push_back("Matched a sibling CUE sheet for the BIN candidate.");
		}
	}
	probe.isDreamcastCandidate = probe.kind == SourceKind::DreamcastGdi;
	probe.isGenericCdCandidate = probe.kind == SourceKind::CueBin || probe.kind == SourceKind::Iso;
	probe.supportedByPlan = probe.kind != SourceKind::Unknown;
	probe.summary = strprintf("Detected %s candidate: %s", describeSourceKind(probe.kind), candidate.c_str());
	addWriterNote(probe);
	NOTICE_LOG(COMMON, "CHD probe directory: path='%s' candidate='%s' kind='%s'",
		path.c_str(), candidate.c_str(), describeSourceKind(probe.kind));
	return probe;
}

} // namespace

const char *describeSourceKind(SourceKind kind)
{
	switch (kind)
	{
	case SourceKind::DreamcastGdi:
		return "GDI";
	case SourceKind::CueBin:
		return "CUE/BIN";
	case SourceKind::Iso:
		return "ISO";
	case SourceKind::Directory:
		return "Directory";
	case SourceKind::Unknown:
	default:
		return "Unknown";
	}
}

const char *describeCompressionProfile(CompressionProfile profile)
{
	return chdwriter_mame::describeCompressionProfile(writerCompressionProfile(profile));
}

const char *describeCompressionStack(SourceKind kind, CompressionProfile profile)
{
	return chdwriter_mame::describeCompressionStack(writerModeForKind(kind), writerCompressionProfile(profile));
}

SourceProbe probeSource(const std::string& path)
{
	SourceProbe probe;
	probe.sourcePath = normalizeInputPath(path);
	probe.summary = "Select a source path to begin the preconvert check.";
	probe.notes.push_back("Use Preconvert check, then Run conversion now to start internal conversion flow.");

	if (probe.sourcePath.empty())
	{
		NOTICE_LOG(COMMON, "CHD probe source: empty input");
		return probe;
	}

	try
	{
		const hostfs::FileInfo info = hostfs::storage().getFileInfo(probe.sourcePath);
		NOTICE_LOG(COMMON, "CHD probe source: path='%s' isDirectory=%d", probe.sourcePath.c_str(), info.isDirectory ? 1 : 0);
		if (info.isDirectory)
			return probeDirectory(probe.sourcePath);

		const std::string ext = extensionFromNameOrPath(info.name, probe.sourcePath);
		probe.kind = kindForExtension(ext);
		probe.primaryFile = probe.sourcePath;
		probe.isDreamcastCandidate = probe.kind == SourceKind::DreamcastGdi;
		probe.isGenericCdCandidate = probe.kind == SourceKind::CueBin || probe.kind == SourceKind::Iso;
		probe.supportedByPlan = probe.kind != SourceKind::Unknown;

		if (probe.kind == SourceKind::Unknown && ext == "bin")
		{
			const std::string parent = hostfs::storage().getParentPath(probe.sourcePath);
			const std::string cuePath = hostfs::storage().getSubPath(parent, get_file_basename(info.name) + ".cue");
			if (hostfs::storage().exists(cuePath))
			{
				probe.kind = SourceKind::CueBin;
				probe.primaryFile = cuePath;
				probe.supportedByPlan = true;
				probe.notes.push_back("Matched a sibling CUE sheet for the BIN source.");
				NOTICE_LOG(COMMON, "CHD probe source: bin matched cue='%s'", cuePath.c_str());
			}
			else
			{
				probe.notes.push_back("BIN files need a companion CUE sheet before the conversion path can be trusted.");
				WARN_LOG(COMMON, "CHD probe source: bin missing cue for path='%s'", probe.sourcePath.c_str());
			}
		}

		if (probe.kind == SourceKind::Unknown)
		{
			probe.summary = strprintf("No CHD plan ready yet for %s.", probe.sourcePath.c_str());
			if (ext.empty())
				probe.notes.push_back("No file extension was detected.");
			else
				probe.notes.push_back(strprintf("Detected extension: .%s", ext.c_str()));
			probe.notes.push_back("Supported extensions in this pass: .gdi, .cue, .bin (with .cue), .iso.");
			probe.notes.push_back("This source type is not in the first-pass plan.");
			return probe;
		}

		probe.summary = strprintf("Detected %s source: %s", describeSourceKind(probe.kind),
			probe.primaryFile.empty() ? probe.sourcePath.c_str() : probe.primaryFile.c_str());
		addWriterNote(probe);
		NOTICE_LOG(COMMON, "CHD probe source: resolved kind='%s' primary='%s'",
			describeSourceKind(probe.kind), probe.primaryFile.c_str());
		if (probe.kind == SourceKind::Iso)
			probe.notes.push_back("ISO is mapped to the createdvd mode.");
	}
	catch (const std::exception& e)
	{
		probe.summary = strprintf("Source preconvert check failed: %s", e.what());
		probe.notes.push_back("This is usually a path or storage-access issue.");
	}
	catch (...)
	{
		probe.summary = "Source preconvert check failed with an unknown error.";
		probe.notes.push_back("This needs a follow-up verification pass.");
	}

	return probe;
}

ConversionPlan planConversion(const std::string& path)
{
	ConversionPlan plan;
	plan.probe = probeSource(path);
	plan.mode = plan.probe.isDreamcastCandidate ? "Dreamcast disc" : (plan.probe.kind == SourceKind::Iso ? "DVD/ISO" : "Unknown");
	plan.canRunNow = plan.probe.supportedByPlan && !plan.probe.primaryFile.empty();

	if (plan.probe.kind == SourceKind::Unknown)
		plan.probe.notes.push_back("No first-pass conversion route is available for this source.");
	else if (!plan.canRunNow)
		plan.probe.notes.push_back("A concrete source file is still required before conversion can run.");

	NOTICE_LOG(COMMON, "CHD plan: path='%s' mode='%s' supported=%d canRunNow=%d primary='%s'",
		path.c_str(), plan.mode.c_str(), plan.probe.supportedByPlan ? 1 : 0, plan.canRunNow ? 1 : 0, plan.probe.primaryFile.c_str());
	return plan;
}

ConversionResult runSingleConversion(const std::string& path, const ConversionOptions& options)
{
	ConversionResult result;
	try
	{
		result.sourcePath = normalizeInputPath(path);
		NOTICE_LOG(COMMON, "CHD convert start: requested='%s' outputDirectory='%s'",
			result.sourcePath.c_str(), options.outputDirectory.c_str());

		ConversionPlan plan = planConversion(result.sourcePath);
		if (!plan.canRunNow)
		{
			result.message = "Source is not convertible by the current plan.";
			WARN_LOG(COMMON, "CHD convert rejected: requested='%s' summary='%s'",
				result.sourcePath.c_str(), plan.probe.summary.c_str());
			return result;
		}

		if (!resolveOutputPathForSource(plan.probe.primaryFile.empty() ? plan.probe.sourcePath : plan.probe.primaryFile,
			options.outputDirectory, options.outputFileSuffix, result.outputPath, result.message))
		{
			result.exitCode = -2;
			WARN_LOG(COMMON, "CHD convert rejected: requested='%s' message='%s'",
				result.sourcePath.c_str(), result.message.c_str());
			return result;
		}
		NOTICE_LOG(COMMON, "CHD convert output resolved: source='%s' primary='%s' output='%s'",
			result.sourcePath.c_str(), plan.probe.primaryFile.c_str(), result.outputPath.c_str());

		const chdwriter_mame::Mode mode = writerModeForKind(plan.probe.kind);
		const chdwriter_mame::CompressionProfile compressionProfile = writerCompressionProfile(options.compressionProfile);
		result.commandLine = strprintf("internal_chd_convert mode=%s compression=\"%s\" input=\"%s\" output=\"%s\"",
			mode == chdwriter_mame::Mode::CreateDvd ? "dvd" : "cd",
			chdwriter_mame::describeCompressionStack(mode, compressionProfile),
			plan.probe.primaryFile.c_str(), result.outputPath.c_str());
		NOTICE_LOG(COMMON, "CHD convert resolved: mode=%s compression='%s' input='%s' output='%s'",
			mode == chdwriter_mame::Mode::CreateDvd ? "dvd" : "cd",
			chdwriter_mame::describeCompressionStack(mode, compressionProfile),
			plan.probe.primaryFile.c_str(), result.outputPath.c_str());

		std::string error;
		result.success = chdwriter_mame::runConversion(mode, plan.probe.primaryFile, result.outputPath, error,
			options.progressCallback, compressionProfile);
		result.exitCode = result.success ? 0 : -2;
		result.message = result.success
			? strprintf("Conversion complete: %s", result.outputPath.c_str())
			: (error.empty() ? "Internal CHD conversion failed." : error);
		if (result.success)
			NOTICE_LOG(COMMON, "CHD convert finished: input='%s' output='%s'", plan.probe.primaryFile.c_str(), result.outputPath.c_str());
		else
			ERROR_LOG(COMMON, "CHD convert failed: input='%s' output='%s' message='%s'",
				plan.probe.primaryFile.c_str(), result.outputPath.c_str(), result.message.c_str());
	}
	catch (const std::exception& e)
	{
		result.exitCode = -3;
		result.message = strprintf("Conversion setup failed: %s", e.what());
		ERROR_LOG(COMMON, "CHD convert setup exception: requested='%s' message='%s'",
			result.sourcePath.c_str(), result.message.c_str());
	}
	catch (...)
	{
		result.exitCode = -3;
		result.message = "Conversion setup failed with an unknown error.";
		ERROR_LOG(COMMON, "CHD convert setup exception: requested='%s' message='%s'",
			result.sourcePath.c_str(), result.message.c_str());
	}
	return result;
}

std::vector<std::string> collectConvertibleSources(const std::string& path, bool scanSubdirectories)
{
	std::vector<std::string> result;
	const std::string normalizedPath = normalizeInputPath(path);
	if (normalizedPath.empty())
		return result;

	try
	{
		const hostfs::FileInfo info = hostfs::storage().getFileInfo(normalizedPath);
		if (!info.isDirectory)
		{
			ConversionPlan plan = planConversion(normalizedPath);
			if (plan.canRunNow)
				result.push_back(normalizedPath);
			NOTICE_LOG(COMMON, "CHD convert source collection: path='%s' directory=false collected=%d",
				normalizedPath.c_str(), result.empty() ? 0 : 1);
			return result;
		}

		std::set<std::string> uniquePaths;
		if (scanSubdirectories)
		{
			for (const auto& entry : hostfs::DirectoryTree(normalizedPath))
			{
				if (entry.isDirectory)
					continue;
				const std::string ext = extensionFromNameOrPath(entry.name, entry.path);
				if (!isPreferredRootImage(ext))
					continue;
				ConversionPlan plan = planConversion(entry.path);
				if (plan.canRunNow)
					uniquePaths.insert(entry.path);
			}
		}
		else
		{
			const std::vector<hostfs::FileInfo> entries = hostfs::storage().listContent(normalizedPath);
			for (const auto& entry : entries)
			{
				if (entry.isDirectory)
					continue;
				const std::string ext = extensionFromNameOrPath(entry.name, entry.path);
				if (!isPreferredRootImage(ext))
					continue;
				ConversionPlan plan = planConversion(entry.path);
				if (plan.canRunNow)
					uniquePaths.insert(entry.path);
			}
		}

		result.assign(uniquePaths.begin(), uniquePaths.end());
		NOTICE_LOG(COMMON, "CHD convert source collection: path='%s' directory=true scanSubdirectories=%d collected=%d",
			normalizedPath.c_str(), scanSubdirectories ? 1 : 0, (int)result.size());
	}
	catch (...)
	{
		ERROR_LOG(COMMON, "CHD convert source collection crashed for path='%s'", normalizedPath.c_str());
	}

	return result;
}

std::vector<std::string> collectSourceSetPaths(const std::string& path)
{
	std::vector<std::string> result;
	std::set<std::string> uniquePaths;
	auto addUniquePath = [&result, &uniquePaths](const std::string& candidatePath) {
		const std::string normalizedPath = normalizeInputPath(candidatePath);
		if (!normalizedPath.empty() && uniquePaths.insert(normalizedPath).second)
			result.push_back(normalizedPath);
	};

	try
	{
		const ConversionPlan plan = planConversion(path);
		if (!plan.canRunNow || plan.probe.primaryFile.empty())
			return result;
		if (plan.probe.kind == SourceKind::Iso)
		{
			addUniquePath(plan.probe.primaryFile);
			return result;
		}

		cdrom_file::toc toc;
		cdrom_file::track_input_info trackInfo;
		std::error_condition err = cdrom_file::parse_toc(plan.probe.primaryFile, toc, trackInfo);
		if (err)
		{
			WARN_LOG(COMMON, "CHD source-set path parse failed: source='%s' message='%s'",
				plan.probe.primaryFile.c_str(), err.message().c_str());
			return result;
		}

		for (uint32_t tracknum = 0; tracknum < toc.numtrks; tracknum++)
			addUniquePath(trackInfo.track[tracknum].fname);
		addUniquePath(plan.probe.primaryFile);
	}
	catch (const std::exception& e)
	{
		WARN_LOG(COMMON, "CHD source-set path collection failed: source='%s' message='%s'",
			path.c_str(), e.what());
	}
	catch (...)
	{
		WARN_LOG(COMMON, "CHD source-set path collection failed: source='%s' message=unknown", path.c_str());
	}

	return result;
}

} // namespace chdconvert

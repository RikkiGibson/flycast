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
#include "settings_new.h"
#include "settings.h"
#include "gui.h"
#include "gui_util.h"
#include "chdconvert/chdconvert.h"
#include "log/Log.h"
#include "nowide/cstdio.hpp"
#include "oslib/storage.h"
#include "imgui_stdlib.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern ImFont *largeFont;
extern ImFont *settingsTitleFont;
extern ImFont *settingsRightValueFont;

namespace SettingsNew {
namespace {

enum class ChdPage
{
	Overview,
	Converter,
};

#ifdef __ANDROID__
static constexpr std::array<const char*, 3> kScopes = {{
	"Single ISO",
	"Single ROM Folder",
	"Whole Folder",
}};
#else
static constexpr std::array<const char*, 3> kScopes = {{
	"Single ROM",
	"Single ROM Folder",
	"Whole Folder",
}};
#endif

static ChdPage s_page = ChdPage::Overview;
static int s_scope = 0;
static std::string s_sourcePath;
static std::string s_outputPath;
static std::string s_sourcePathText;
static std::string s_outputPathText;
static std::string s_statusText = "Ready to stage CHD jobs.";
static chdconvert::ConversionPlan s_lastPlan;
static bool s_hasPlan = false;
static std::string s_lastCommand;
static std::atomic<bool> s_conversionRunning(false);
static std::atomic<int> s_conversionTotal(0);
static std::atomic<int> s_conversionDone(0);
static std::atomic<int> s_conversionOk(0);
static std::atomic<int> s_conversionSkipped(0);
static std::mutex s_conversionMutex;
static std::future<void> s_conversionFuture;
static std::string s_conversionPhase;
static std::string s_pendingStatusUpdate;
static std::string s_pendingCommandUpdate;
static std::vector<std::string> s_traceLines;
static bool s_openSourcePicker = false;
static bool s_openOutputPicker = false;
static bool s_sourcePickerSelectFile = false;
static std::string s_sourcePickerMimeType;
struct ConversionRunStats
{
	uint64_t inputBytes = 0;
	uint64_t outputBytes = 0;
	uint64_t elapsedMs = 0;
	uint64_t etaMs = 0;
	double progress = 0.0;
	double ratio = 0.0;
	int total = 0;
	int ok = 0;
	int skipped = 0;
	bool active = false;
	bool complete = false;
	std::string phase;
};

static ConversionRunStats s_lastRunStats;

static const char* pageName(ChdPage page)
{
	switch (page)
	{
	case ChdPage::Overview:
		return "Overview";
	case ChdPage::Converter:
		return "Converter";
	}
	return "Overview";
}

static void setStatus(const std::string& text)
{
	s_statusText = text;
}

static void flushAsyncStatus()
{
	std::lock_guard<std::mutex> lock(s_conversionMutex);
	if (!s_pendingStatusUpdate.empty())
	{
		s_statusText = s_pendingStatusUpdate;
		s_pendingStatusUpdate.clear();
	}
	if (!s_pendingCommandUpdate.empty())
	{
		s_lastCommand = s_pendingCommandUpdate;
		s_pendingCommandUpdate.clear();
	}
}

static void pollConversionWorker()
{
	if (!s_conversionFuture.valid())
		return;
	if (s_conversionFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;
	s_conversionFuture.get();
}

static std::string traceTimestamp()
{
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
	const std::time_t t = system_clock::to_time_t(now);
	std::tm tmBuf {};
#if defined(_WIN32)
	localtime_s(&tmBuf, &t);
#else
	localtime_r(&t, &tmBuf);
#endif
	return strprintf("%02d:%02d:%02d.%03d", tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec, (int)ms.count());
}

static std::string formatBytes(uint64_t bytes)
{
	static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
	double value = (double)bytes;
	int unit = 0;
	while (value >= 1024.0 && unit < 4)
	{
		value /= 1024.0;
		unit++;
	}
	if (unit == 0)
		return strprintf("%llu %s", (unsigned long long)bytes, units[unit]);
	return strprintf("%.2f %s", value, units[unit]);
}

static std::string formatDuration(uint64_t ms)
{
	const uint64_t minutes = ms / 60000;
	const uint64_t seconds = (ms / 1000) % 60;
	const uint64_t millis = ms % 1000;
	if (minutes > 0)
		return strprintf("%llum %02llus", (unsigned long long)minutes, (unsigned long long)seconds);
	return strprintf("%llus %03llums", (unsigned long long)seconds, (unsigned long long)millis);
}

static void addTrace(const std::string& stage, const std::string& message)
{
	std::lock_guard<std::mutex> lock(s_conversionMutex);
	s_traceLines.push_back(strprintf("[%s] [%s] %s", traceTimestamp().c_str(), stage.c_str(), message.c_str()));
	if (s_traceLines.size() > 200)
		s_traceLines.erase(s_traceLines.begin(), s_traceLines.begin() + (s_traceLines.size() - 200));
}

static std::string pathTextForUi(const std::string& path)
{
	if (path.rfind("content://", 0) != 0)
		return path;
	try
	{
		const hostfs::FileInfo info = hostfs::storage().getFileInfo(path);
		if (!info.name.empty())
			return strprintf("[SAF] %s", info.name.c_str());
	}
	catch (...)
	{
	}
	return path;
}

static bool isContentUriPath(const std::string& path)
{
	return path.rfind("content://", 0) == 0;
}

static std::string outputPathForSource(const std::string& sourcePath, const std::string& outputDirectory)
{
	if (sourcePath.empty() || outputDirectory.empty())
		return {};
	try
	{
		const hostfs::FileInfo sourceInfo = hostfs::storage().getFileInfo(sourcePath);
		const std::string baseName = get_file_basename(sourceInfo.name) + ".chd";
		return hostfs::storage().getSubPath(outputDirectory, baseName);
	}
	catch (...)
	{
		return {};
	}
}

static void assignSourcePath(const std::string& path)
{
	s_sourcePath = path;
	s_sourcePathText = pathTextForUi(path);
	s_hasPlan = false;
}

static void assignOutputPath(const std::string& path)
{
	s_outputPath = path;
	s_outputPathText = pathTextForUi(path);
}

static void requestSourcePicker()
{
#ifdef __ANDROID__
	s_sourcePickerSelectFile = s_scope == 0;
#else
	s_sourcePickerSelectFile = s_scope == 0;
#endif
	s_sourcePickerMimeType = s_sourcePickerSelectFile ? "*/*" : "";
	s_openSourcePicker = true;
}

static void requestOutputPicker()
{
	s_openOutputPicker = true;
}

static void renderStoragePickers()
{
	static const char *sourcePopupTitle = "Select source ROM or library folder";
	static const char *outputPopupTitle = "Select CHD output folder";

	select_file_popup(sourcePopupTitle, [](bool cancelled, const std::string& selection) {
		if (!cancelled)
		{
			NOTICE_LOG(COMMON, "CHD UI source selected: '%s'", selection.c_str());
			assignSourcePath(selection);
			setStatus("Source selected.");
		}
		else
		{
			NOTICE_LOG(COMMON, "CHD UI source picker cancelled");
			setStatus("Source picker cancelled.");
		}
		return true;
	}, s_sourcePickerSelectFile, "");

	select_file_popup(outputPopupTitle, [](bool cancelled, const std::string& selection) {
		if (!cancelled)
		{
			NOTICE_LOG(COMMON, "CHD UI output selected: '%s'", selection.c_str());
			assignOutputPath(selection);
			setStatus("Output folder selected.");
		}
		else
		{
			NOTICE_LOG(COMMON, "CHD UI output picker cancelled");
			setStatus("Output picker cancelled.");
		}
		return true;
	}, false, "");

	if (s_openSourcePicker)
	{
#ifdef __ANDROID__
		const StoragePopupResult result = select_storage_popup(s_sourcePickerSelectFile ? false : true, false, sourcePopupTitle,
			[](bool cancelled, const std::string& selection) {
				if (!cancelled)
				{
					NOTICE_LOG(COMMON, "CHD UI source selected: '%s'", selection.c_str());
					assignSourcePath(selection);
					setStatus("Source selected.");
				}
				else
				{
					NOTICE_LOG(COMMON, "CHD UI source picker cancelled");
					setStatus("Source picker cancelled.");
				}
				return true;
			}, s_sourcePickerMimeType);
		if (result == StoragePopupResult::Unsupported)
			ImGui::OpenPopup(sourcePopupTitle);
		else if (result == StoragePopupResult::CallbackAlreadySet)
			setStatus("Another storage picker is already open.");
#else
		ImGui::OpenPopup(sourcePopupTitle);
#endif
		s_openSourcePicker = false;
	}

	if (s_openOutputPicker)
	{
#ifdef __ANDROID__
		const StoragePopupResult result = select_storage_popup(true, true, outputPopupTitle,
			[](bool cancelled, const std::string& selection) {
				if (!cancelled)
				{
					NOTICE_LOG(COMMON, "CHD UI output selected: '%s'", selection.c_str());
					assignOutputPath(selection);
					setStatus("Output folder selected.");
				}
				else
				{
					NOTICE_LOG(COMMON, "CHD UI output picker cancelled");
					setStatus("Output picker cancelled.");
				}
				return true;
			});
		if (result == StoragePopupResult::Unsupported)
			ImGui::OpenPopup(outputPopupTitle);
		else if (result == StoragePopupResult::CallbackAlreadySet)
			setStatus("Another storage picker is already open.");
#else
		ImGui::OpenPopup(outputPopupTitle);
#endif
		s_openOutputPicker = false;
	}
}

static void refreshPlan()
{
	NOTICE_LOG(COMMON, "CHD UI preconvert check start: sourcePath='%s'", s_sourcePath.c_str());
	s_lastPlan = chdconvert::planConversion(s_sourcePath);
	s_hasPlan = true;
	s_statusText = s_lastPlan.probe.summary;
	NOTICE_LOG(COMMON, "CHD UI preconvert check result: summary='%s' supported=%d canRunNow=%d primary='%s'",
		s_lastPlan.probe.summary.c_str(), s_lastPlan.probe.supportedByPlan ? 1 : 0, s_lastPlan.canRunNow ? 1 : 0,
		s_lastPlan.probe.primaryFile.c_str());
	addTrace("PRECHECK", s_lastPlan.probe.summary);
	for (const std::string& note : s_lastPlan.probe.notes)
		addTrace("PRECHECK", note);
}

static int normalizedScope()
{
	return std::clamp(s_scope, 0, (int)kScopes.size() - 1);
}

static const char* scopeLabel(int scope)
{
	return kScopes[std::clamp(scope, 0, (int)kScopes.size() - 1)];
}

static bool isWholeFolderScope(int scope)
{
	return std::clamp(scope, 0, (int)kScopes.size() - 1) == 2;
}

static const char* scopeDescription(int scope)
{
	switch (std::clamp(scope, 0, (int)kScopes.size() - 1))
	{
	case 0:
#ifdef __ANDROID__
		return "Pick one .iso file. Use Single ROM Folder for .cue/.bin and .gdi sets on Android.";
#else
		return "Pick one .iso, .cue, or .gdi file. Companion track files are resolved from the same folder.";
#endif
	case 1:
		return "Pick one game folder and convert the first supported ROM set found inside it.";
	case 2:
		return "Pick one folder and convert every supported ROM set found directly inside it.";
	default:
		return "";
	}
}

static std::vector<std::string> collectSourcesForScope(const std::string& path, int scope)
{
	const int normalizedScope = std::clamp(scope, 0, (int)kScopes.size() - 1);
	const chdconvert::ConversionPlan plan = chdconvert::planConversion(path);
	if (normalizedScope == 0)
	{
#ifdef __ANDROID__
		if (plan.probe.kind != chdconvert::SourceKind::Iso)
			return {};
#endif
		if (plan.canRunNow)
			return { plan.probe.primaryFile };
		return {};
	}
	if (normalizedScope == 1)
	{
		if (plan.canRunNow)
			return { plan.probe.primaryFile };
		return {};
	}
	return chdconvert::collectConvertibleSources(path, false);
}

static const char* currentScopeLabel()
{
	return scopeLabel(normalizedScope());
}

static void startAsyncConversion(const std::vector<std::string>& sources, const std::string& outputDirectory)
{
	pollConversionWorker();
	if (outputDirectory.empty())
	{
		setStatus("Select an output folder before converting.");
		addTrace("RUN", "Conversion rejected: output folder is required.");
		WARN_LOG(COMMON, "CHD UI run rejected: missing output directory");
		return;
	}
	if (sources.empty())
	{
		setStatus("No supported conversion source found.");
		addTrace("RUN", "No supported conversion source found.");
		WARN_LOG(COMMON, "CHD UI run rejected: no supported sources for sourcePath='%s'", s_sourcePath.c_str());
		return;
	}
	if (s_conversionRunning.load())
	{
		setStatus("A conversion job is already running.");
		addTrace("RUN", "Requested start while another conversion job is running.");
		WARN_LOG(COMMON, "CHD UI run rejected: conversion already running");
		return;
	}
	s_conversionRunning.store(true);
	s_conversionTotal.store((int)sources.size());
	s_conversionDone.store(0);
	s_conversionOk.store(0);
	s_conversionSkipped.store(0);
	{
		std::lock_guard<std::mutex> lock(s_conversionMutex);
		s_conversionPhase = "Preparing conversion...";
		s_pendingStatusUpdate.clear();
		s_pendingCommandUpdate.clear();
		s_lastRunStats = {};
	}
	addTrace("RUN", strprintf("Starting conversion batch with %d source(s).", (int)sources.size()));
	addTrace("RUN", strprintf("Output directory: %s", outputDirectory.c_str()));
	NOTICE_LOG(COMMON, "CHD UI run start: sourcePath='%s' outputDirectory='%s' sources=%d",
		s_sourcePath.c_str(), outputDirectory.c_str(), (int)sources.size());

	s_conversionFuture = std::async(std::launch::async, [sources, outputDirectory]() {
		try
		{
			chdconvert::ConversionOptions options;
			options.outputDirectory = outputDirectory;
			const auto runStart = std::chrono::steady_clock::now();
			uint64_t inputBytesTotal = 0;
			uint64_t outputBytesTotal = 0;
			auto updateProgress = [runStart](double complete, double ratio, const std::string& phase) {
				const uint64_t elapsedMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - runStart).count();
				const uint64_t etaMs = complete > 0.0
					? (uint64_t)((double)elapsedMs * (1.0 - complete) / complete)
					: 0;
				std::lock_guard<std::mutex> lock(s_conversionMutex);
				s_lastRunStats.active = true;
				s_lastRunStats.complete = false;
				s_lastRunStats.phase = phase;
				s_lastRunStats.progress = std::clamp(complete, 0.0, 1.0);
				s_lastRunStats.ratio = ratio;
				s_lastRunStats.elapsedMs = elapsedMs;
				s_lastRunStats.etaMs = etaMs;
			};
			options.progressCallback = updateProgress;

			for (size_t i = 0; i < sources.size(); i++)
			{
				const std::string phase = strprintf("Running %d/%d: %s", (int)i + 1, (int)sources.size(), sources[i].c_str());
				{
					std::lock_guard<std::mutex> lock(s_conversionMutex);
					s_conversionPhase = phase;
					s_lastRunStats.active = false;
					s_lastRunStats.phase = phase;
				}
				addTrace("SOURCE", phase);
				NOTICE_LOG(COMMON, "CHD worker source begin: index=%d total=%d source='%s'",
					(int)i + 1, (int)sources.size(), sources[i].c_str());
				const std::string existingOutputPath = outputPathForSource(sources[i], outputDirectory);
				if (!existingOutputPath.empty() && hostfs::storage().exists(existingOutputPath))
				{
					const std::string message = strprintf("Skipped ROM because the output CHD already exists: %s", existingOutputPath.c_str());
					s_conversionSkipped.fetch_add(1);
					addTrace("SKIP", message);
					s_conversionDone.fetch_add(1);
					std::lock_guard<std::mutex> lock(s_conversionMutex);
					s_lastRunStats.active = false;
					s_pendingStatusUpdate = message;
					continue;
				}
				const hostfs::FileInfo sourceInfo = hostfs::storage().getFileInfo(sources[i]);
				uint64_t sourceBytes = sourceInfo.size;
				const std::vector<std::string> sourcePaths = chdconvert::collectSourceSetPaths(sources[i]);
				if (!sourcePaths.empty())
				{
					uint64_t measuredBytes = 0;
					bool measuredSuccessfully = true;
					for (const std::string& sourcePath : sourcePaths)
					{
						try
						{
							const hostfs::FileInfo sourceFileInfo = hostfs::storage().getFileInfo(sourcePath);
							if (!sourceFileInfo.isDirectory)
								measuredBytes += sourceFileInfo.size;
						}
						catch (...)
						{
							measuredSuccessfully = false;
							break;
						}
					}
					if (measuredSuccessfully && measuredBytes > 0)
						sourceBytes = measuredBytes;
				}
				NOTICE_LOG(COMMON, "CHD worker source info: source='%s' size=%llu isDirectory=%d writable=%d",
					sources[i].c_str(), (unsigned long long)sourceInfo.size, sourceInfo.isDirectory ? 1 : 0, sourceInfo.isWritable ? 1 : 0);
				const chdconvert::ConversionResult result = chdconvert::runSingleConversion(sources[i], options);
				std::string finalStatusMessage = result.message;
				inputBytesTotal += sourceBytes;
				if (!result.commandLine.empty())
					addTrace("CMD", result.commandLine);
				if (result.success)
				{
					s_conversionOk.fetch_add(1);
					try
					{
						if (!result.outputPath.empty())
						{
							const hostfs::FileInfo outInfo = hostfs::storage().getFileInfo(result.outputPath);
							if (!outInfo.isDirectory)
								outputBytesTotal += outInfo.size;
						}
					}
					catch (...)
					{
					}
					addTrace("OK", result.message);
				}
				else
				{
					addTrace("FAIL", result.message);
				}
				s_conversionDone.fetch_add(1);
				std::lock_guard<std::mutex> lock(s_conversionMutex);
				s_lastRunStats.active = false;
				if (!result.commandLine.empty())
					s_pendingCommandUpdate = result.commandLine;
				s_pendingStatusUpdate = finalStatusMessage;
			}

			const int ok = s_conversionOk.load();
			const int skipped = s_conversionSkipped.load();
			const int total = s_conversionTotal.load();
			const int failed = std::max(0, total - ok - skipped);
			const uint64_t elapsedMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - runStart).count();
			{
				std::lock_guard<std::mutex> lock(s_conversionMutex);
				s_conversionPhase = "Conversion complete.";
				s_pendingStatusUpdate = strprintf("Conversion finished: %d succeeded, %d skipped, %d failed.", ok, skipped, failed);
				s_lastRunStats.inputBytes = inputBytesTotal;
				s_lastRunStats.outputBytes = outputBytesTotal;
				s_lastRunStats.elapsedMs = elapsedMs;
				s_lastRunStats.etaMs = 0;
				s_lastRunStats.progress = 1.0;
				s_lastRunStats.ratio = inputBytesTotal > 0 ? (double)outputBytesTotal / (double)inputBytesTotal : 0.0;
				s_lastRunStats.total = total;
				s_lastRunStats.ok = ok;
				s_lastRunStats.skipped = skipped;
				s_lastRunStats.active = false;
				s_lastRunStats.complete = true;
			}
			addTrace("DONE", strprintf("Conversion finished: %d succeeded, %d skipped, %d failed in %s.", ok, skipped, failed, formatDuration(elapsedMs).c_str()));
		}
		catch (const std::exception& e)
		{
			const std::string message = strprintf("Conversion worker crashed: %s", e.what());
			{
				std::lock_guard<std::mutex> lock(s_conversionMutex);
				s_conversionPhase = "Conversion aborted.";
				s_pendingStatusUpdate = message;
			}
			addTrace("CRASH", message);
		}
		catch (...)
		{
			const std::string message = "Conversion worker crashed with an unknown error.";
			{
				std::lock_guard<std::mutex> lock(s_conversionMutex);
				s_conversionPhase = "Conversion aborted.";
				s_pendingStatusUpdate = message;
			}
			addTrace("CRASH", message);
		}
		s_conversionRunning.store(false);
	});
}

static void renderHeroCard()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, uiScaled(14.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(uiScaled(16.0f), uiScaled(14.0f)));
	ImGui::BeginChild("CHDHero", ImVec2(0, uiScaled(176.0f)), true,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_DragScrolling);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 min = ImGui::GetWindowPos();
	const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
	const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
	const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
	ImVec4 tint = ImVec4(accent.x, accent.y, accent.z, 0.12f);
	ImVec4 base = ImVec4(bg.x, bg.y, bg.z, 1.0f);
	drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(base), uiScaled(14.0f));
	drawList->AddRectFilled(min, ImVec2(max.x, min.y + uiScaled(4.0f)), ImGui::ColorConvertFloat4ToU32(tint),
		uiScaled(14.0f), ImDrawFlags_RoundCornersTop);

	ImGui::PushFont(settingsRightValueFont != nullptr ? settingsRightValueFont : largeFont);
	ImGui::TextUnformatted("CHD System");
	ImGui::PopFont();

	ImGui::PushFont(settingsTitleFont != nullptr ? settingsTitleFont : ImGui::GetFont());
	ImGui::TextWrapped("A polished ROM management workspace for GDI, ISO, and CUE/BIN imports that now feeds a local CHD conversion flow.");
	ImGui::PopFont();
	ImGui::Spacing();
	ImGui::TextDisabled("%s", s_statusText.c_str());

	ImGui::Spacing();
	if (ImGui::BeginTable("CHDHeroCards", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableNextColumn();
		ImGui::BeginGroup();
		header("Input Support");
		ImGui::TextWrapped(".gdi, .cue/.bin, and .iso.");
		ImGui::EndGroup();

		ImGui::TableNextColumn();
		ImGui::BeginGroup();
		header("Compression");
		ImGui::TextWrapped("Balanced backend defaults for this first release.");
		ImGui::EndGroup();

		ImGui::TableNextColumn();
		ImGui::BeginGroup();
		header("Workflows");
		ImGui::TextWrapped("Single ROM, single ROM folder, and whole-folder conversion.");
		ImGui::EndGroup();
		ImGui::EndTable();
	}

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

static void renderOverviewTab()
{
	renderHeroCard();
	ImGui::Spacing();

	if (ImGui::BeginTable("CHDOverviewDetails", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableNextColumn();
		header("What this screen will do");
		ImGui::BulletText("Convert one selected ROM, one ROM folder, or every supported ROM found in a folder.");
		ImGui::BulletText("Require extracted ROM dumps for this first release.");
		ImGui::BulletText("Preserve original source files after successful output creation.");
		ImGui::BulletText("Keep the visual language aligned with the existing settings pages.");

		ImGui::TableNextColumn();
		header("Backend plan");
		ImGui::BulletText("The new probe layer identifies Dreamcast GDI, CUE/BIN, and ISO candidates.");
		ImGui::BulletText("CDI support has been removed from this pass.");
		ImGui::BulletText("Single-run conversion now runs through the internal CHD backend flow.");
		ImGui::EndTable();
	}
}

static void renderConverterTab()
{
	pollConversionWorker();
	flushAsyncStatus();
	renderStoragePickers();

	if (ImGui::BeginTable("CHDConverterLayout", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
	{
		ImGui::TableSetupColumn("CHDConverterLeft", ImGuiTableColumnFlags_WidthStretch, 0.58f);
		ImGui::TableSetupColumn("CHDConverterRight", ImGuiTableColumnFlags_WidthStretch, 0.42f);
		ImGui::TableNextColumn();
		header("Source and Output");

		ImGui::TextUnformatted("Conversion scope");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::Combo("##chd_scope", &s_scope, kScopes.data(), (int)kScopes.size()))
			setStatus("Scope updated.");
		ImGui::TextWrapped("%s", scopeDescription(s_scope));

		ImGui::Spacing();
		ImGui::TextUnformatted("Source path");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##chd_source", &s_sourcePathText))
			assignSourcePath(s_sourcePathText);
		if (ImGui::Button("Browse source", ImVec2(-1, 0)))
		{
			NOTICE_LOG(COMMON, "CHD UI action: Browse source");
			setStatus("Opening source picker...");
			requestSourcePicker();
		}
		if (ImGui::Button("Preconvert check", ImVec2(-1, 0)))
		{
			NOTICE_LOG(COMMON, "CHD UI action: Preconvert check");
			refreshPlan();
			setStatus(s_lastPlan.probe.summary);
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Output folder");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##chd_output", &s_outputPathText))
			assignOutputPath(s_outputPathText);
		if (ImGui::Button("Browse output", ImVec2(-1, 0)))
		{
			NOTICE_LOG(COMMON, "CHD UI action: Browse output");
			setStatus("Opening output picker...");
			requestOutputPicker();
		}

		ImGui::Spacing();
		header("Compression");
		ImGui::TextUnformatted("Balanced");
		ImGui::TextWrapped("Uses the internal CHD backend defaults for this release.");

		ImGui::Spacing();
		header("File Handling");
		ImGui::TextWrapped("Original files are preserved in this release.");
		ImGui::TextWrapped("Archive extraction is not enabled in this first release. Please extract .zip or .7z ROMs before converting.");

		ImGui::TableNextColumn();
		header("Live Summary");

		ImGui::BeginChild("CHDLiveSummary", ImVec2(0, uiScaled(214.0f)), true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_DragScrolling);
		ImGui::TextUnformatted("Quick guide");
		ImGui::Separator();
		ImGui::BulletText("Single ISO: use for one .iso file.");
		ImGui::BulletText("Single ROM Folder: use for one .cue/.bin or .gdi game folder.");
		ImGui::BulletText("Whole Folder: scan a folder and convert every supported ROM set inside it.");
		ImGui::BulletText("Preconvert check: preview what will run before you start the job.");
		ImGui::BulletText("Browse output: required before conversion starts.");
		ImGui::Spacing();
		ImGui::TextUnformatted("Current configuration");
		ImGui::Separator();
		ImGui::Text("Scope: %s", currentScopeLabel());
		ImGui::TextUnformatted("Compression: Balanced");
		ImGui::TextUnformatted("Original files: preserved");
		ImGui::Text("Whole folder scan: %s", isWholeFolderScope(s_scope) ? "Yes" : "No");
		ImGui::TextWrapped("Source: %s", s_sourcePathText.empty() ? "not selected" : s_sourcePathText.c_str());
		ImGui::TextWrapped("Output: %s", s_outputPathText.empty() ? "not selected" : s_outputPathText.c_str());
		if (!s_hasPlan)
			refreshPlan();
		ImGui::Spacing();
		ImGui::TextUnformatted("Probe result");
		ImGui::Text("Plan mode: %s", s_lastPlan.mode.c_str());
		ImGui::Text("Supported by plan: %s", s_lastPlan.probe.supportedByPlan ? "Yes" : "No");
		ImGui::Text("Runnable now: %s", s_lastPlan.canRunNow ? "Yes" : "No");
		ImGui::TextWrapped("%s", s_lastPlan.probe.summary.c_str());
		for (const std::string& note : s_lastPlan.probe.notes)
			ImGui::BulletText("%s", note.c_str());
		if (!s_lastCommand.empty())
			ImGui::TextWrapped("Last command: %s", s_lastCommand.c_str());
		ImGui::EndChild();

		ImGui::Spacing();
		if (ImGui::Button("Run conversion now", ImVec2(-1, uiScaled(34.0f))))
		{
			NOTICE_LOG(COMMON, "CHD UI action: Run conversion now");
			if (!s_hasPlan || s_lastPlan.probe.sourcePath != s_sourcePath)
				refreshPlan();
			const std::vector<std::string> sources = collectSourcesForScope(s_sourcePath, s_scope);
			addTrace("RUN", strprintf("Collected %d source(s) for immediate run.", (int)sources.size()));
			startAsyncConversion(sources, s_outputPath);
			if (sources.empty())
			{
#ifdef __ANDROID__
				if (normalizedScope() == 0 && s_lastPlan.probe.kind != chdconvert::SourceKind::Iso)
					setStatus("Single ISO mode only supports .iso on Android. Use Single ROM Folder for .cue/.bin or .gdi sets.");
				else
#endif
					setStatus(s_lastPlan.probe.summary);
			}
		}

		ImGui::TextWrapped("Run conversion now executes the internal CHD backend flow for supported sources.");
		if (s_conversionRunning.load() || s_conversionTotal.load() > 0)
		{
			const int total = std::max(1, s_conversionTotal.load());
			const int done = std::min(total, s_conversionDone.load());
			float progress = (float)done / (float)total;
			std::string phase;
			ConversionRunStats stats;
			{
				std::lock_guard<std::mutex> lock(s_conversionMutex);
				phase = s_conversionPhase;
				stats = s_lastRunStats;
			}
			if (stats.active)
				progress = (float)std::clamp(stats.progress, 0.0, 1.0);
			ImGui::Spacing();
			ImGui::TextUnformatted("Conversion Progress");
			if (stats.active)
			{
				const float progressPct = progress * 100.0f;
				const std::string etaText = stats.etaMs > 0 ? formatDuration(stats.etaMs) : "--";
				ImGui::ProgressBar(progress, ImVec2(-1, uiScaled(18.0f)),
					strprintf("%.1f%%  ETA %s  ratio %.3f", progressPct, etaText.c_str(), stats.ratio).c_str());
				ImGui::Text("ETA: %s", etaText.c_str());
				ImGui::Text("Compression ratio: %.3f", stats.ratio);
			}
			else
			{
				const float progressPct = progress * 100.0f;
				ImGui::ProgressBar(progress, ImVec2(-1, uiScaled(18.0f)), strprintf("%.1f%%  (%d / %d)", progressPct, done, total).c_str());
			}
			if (!phase.empty())
				ImGui::TextWrapped("%s", phase.c_str());
			if (stats.complete)
			{
				const uint64_t savedBytes = stats.inputBytes > stats.outputBytes ? (stats.inputBytes - stats.outputBytes) : 0;
				const double savedPct = stats.inputBytes > 0 ? (double)savedBytes * 100.0 / (double)stats.inputBytes : 0.0;
				ImGui::Spacing();
				ImGui::TextUnformatted("Run Results");
				if (ImGui::BeginTable("CHDRunStats", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
				{
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Elapsed");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(formatDuration(stats.elapsedMs).c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Input Size");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(formatBytes(stats.inputBytes).c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Output Size");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(formatBytes(stats.outputBytes).c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Space Saved");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(strprintf("%s (%.2f%%)", formatBytes(savedBytes).c_str(), savedPct).c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Succeeded");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(strprintf("%d", stats.ok).c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Skipped");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(strprintf("%d", stats.skipped).c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted("Failed");
					ImGui::TableNextColumn(); ImGui::TextUnformatted(strprintf("%d", std::max(0, stats.total - stats.ok - stats.skipped)).c_str());
					ImGui::EndTable();
				}
			}
		}
		ImGui::Spacing();
		ImGui::TextUnformatted("Trace");
		if (ImGui::Button("Clear trace", ImVec2(-1, 0)))
		{
			std::lock_guard<std::mutex> lock(s_conversionMutex);
			s_traceLines.clear();
		}
		ImGui::BeginChild("CHDTrace", ImVec2(0, uiScaled(140.0f)), true, ImGuiWindowFlags_DragScrolling);
		{
			std::lock_guard<std::mutex> lock(s_conversionMutex);
			for (const std::string& line : s_traceLines)
				ImGui::TextWrapped("%s", line.c_str());
		}
		ImGui::EndChild();
		ImGui::Spacing();
		ImGui::TextDisabled("Status: %s", s_statusText.c_str());
		ImGui::EndTable();
	}
}

} // namespace

void renderChdSystemTab()
{
	pollConversionWorker();
	static bool loggedOpen = false;
	if (!loggedOpen)
	{
		NOTICE_LOG(COMMON, "CHD UI render entered");
		loggedOpen = true;
	}
	if (ImGui::BeginTabBar("CHDSystemTabs", ImGuiTabBarFlags_NoTooltip | ImGuiTabBarFlags_FittingPolicyScroll))
	{
		if (ImGui::BeginTabItem(pageName(ChdPage::Overview)))
		{
			s_page = ChdPage::Overview;
			renderOverviewTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem(pageName(ChdPage::Converter)))
		{
			s_page = ChdPage::Converter;
			renderConverterTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::Spacing();
	ImGui::TextDisabled("Last active page: %s", pageName(s_page));
}

} // namespace SettingsNew

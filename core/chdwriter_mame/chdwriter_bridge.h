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

namespace chdwriter_mame {

enum class Mode
{
	CreateCd,
	CreateDvd,
};

bool runConversion(Mode mode, const std::string& inputPath, const std::string& outputPath, std::string& errorMessage,
	const std::function<void(double complete, double ratio, const std::string& phase)>& progressCallback = {});

} // namespace chdwriter_mame

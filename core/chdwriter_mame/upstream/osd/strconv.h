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
//  strconv.h - String conversion
//
//============================================================

#ifndef MAME_OSD_STRCONV_H
#define MAME_OSD_STRCONV_H

#include "osdcore.h"



//============================================================
//  FUNCTION PROTOTYPES
//============================================================

#if defined(_WIN32)

#include <string_view>

#include <windows.h>

namespace osd::text {

std::string to_astring(std::string_view s);
std::string to_astring(const char *s);
std::string &to_astring(std::string &dst, std::string_view s);
std::string &to_astring(std::string &dst, const char *s);
std::string from_astring(const std::string_view s);
std::string from_astring(const CHAR *s);
std::string &from_astring(std::string &dst, std::string_view s);
std::string &from_astring(std::string &dst, const CHAR *s);

std::wstring to_wstring(std::string_view s);
std::wstring to_wstring(const char *s);
std::wstring &to_wstring(std::wstring &dst, std::string_view s);
std::wstring &to_wstring(std::wstring &dst, const char *s);
std::string from_wstring(const std::wstring_view s);
std::string from_wstring(const WCHAR *s);
std::string &from_wstring(std::string &dst, std::wstring_view s);
std::string &from_wstring(std::string &dst, const WCHAR *s);

#ifdef UNICODE
typedef std::wstring tstring;
#define to_tstring   to_wstring
#define from_tstring   from_wstring
#else // !UNICODE
typedef std::string tstring;
#define to_tstring   to_astring
#define from_tstring   from_astring
#endif // UNICODE

} // namespace osd::text

#endif // defined(_WIN32)


#endif // MAME_OSD_STRCONV_H

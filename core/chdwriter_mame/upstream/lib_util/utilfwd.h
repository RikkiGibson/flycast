// license:BSD-3-Clause
// copyright-holders:Vas Crabb
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
/**********************************************************************

    utilfwd.h

    Forward declarations of types

**********************************************************************/
#ifndef MAME_LIB_UTIL_UTILFWD_H
#define MAME_LIB_UTIL_UTILFWD_H

// aviio.h
class avi_file;

// chd.h
class chd_file;


namespace util {

// corefile.h
class core_file;

// ioprocs.h
class read_stream;
class write_stream;
class read_write_stream;
class random_access;
class random_read;
class random_write;
class random_read_write;

// opresolv.h
class option_guide;
class option_resolution;

// unzip.h
class archive_file;

} // namespace util


namespace util::xml {

// xmlfile.h
class data_node;
class file;

} // namespace util::xml

#endif // MAME_LIB_UTIL_UTILFWD_H

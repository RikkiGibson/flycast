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
/***************************************************************************

    vecstream.cpp

    streams with vector storage

***************************************************************************/

#include "vecstream.h"

namespace util {

template class basic_ivectorstream<char>;
template class basic_ivectorstream<wchar_t>;
template class basic_ovectorstream<char>;
template class basic_ovectorstream<wchar_t>;
template class basic_vectorstream<char>;
template class basic_vectorstream<wchar_t>;

} // namespace util

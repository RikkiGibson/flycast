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
/***************************************************************************

    corealloc.h

    Memory allocation helpers for the helper library.

***************************************************************************/

#ifndef MAME_LIB_UTIL_COREALLOC_H
#define MAME_LIB_UTIL_COREALLOC_H

#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>


// global allocation helpers

namespace util {

namespace detail {

template <typename Tp> struct make_unique_clear_traits { };
template <typename Tp> struct make_unique_clear_traits<Tp []> { using unbounded_array_ptr = std::unique_ptr<Tp []>; };
template <typename Tp, size_t Bound> struct make_unique_clear_traits<Tp [Bound]> { };

} // namespace detail

/// make_unique_clear for arrays of unknown bound
template <typename Tp>
inline typename detail::make_unique_clear_traits<Tp>::unbounded_array_ptr make_unique_clear(size_t num)
{
	static_assert(std::is_trivially_constructible_v<std::remove_extent_t<Tp> >, "make_unique_clear is only suitable for trivially constructible types");
	auto const size = sizeof(std::remove_extent_t<Tp>) * num;
	unsigned char* ptr = new unsigned char [size]; // allocate memory - this assumes new expression overhead is the same for all array types
	std::memset(ptr, 0, size);
	return std::unique_ptr<Tp>(new (ptr) std::remove_extent_t<Tp> [num]);
}

} // namespace util

#endif  // MAME_LIB_UTIL_COREALLOC_H

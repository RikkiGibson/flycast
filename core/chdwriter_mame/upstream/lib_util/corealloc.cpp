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
#include "corealloc.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>


#ifdef MAME_DEBUG

std::uint8_t g_mame_new_prefill_byte(0xcd);

void *operator new(std::size_t n)
{
	void *const result(std::malloc(n));
	if (result)
	{
		std::fill_n(reinterpret_cast<std::uint8_t *>(result), n, g_mame_new_prefill_byte);
		return result;
	}
	else
	{
		throw std::bad_alloc();
	}
}

void operator delete(void *ptr) noexcept
{
	std::free(ptr);
}

#endif // MAME_DEBUG

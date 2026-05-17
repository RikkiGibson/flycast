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
//============================================================
//
//  eivcarm.h
//
//  ARM/AArch64 inline implementations for MSVC compiler.
//
//============================================================

#ifndef MAME_OSD_EIVCARM_H
#define MAME_OSD_EIVCARM_H

#pragma once

#include <intrin.h>

#pragma intrinsic(_CountLeadingZeros)
#pragma intrinsic(_CountLeadingZeros64)
#pragma intrinsic(_CountLeadingOnes)
#pragma intrinsic(_CountLeadingOnes64)


/***************************************************************************
    INLINE BIT MANIPULATION FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    count_leading_zeros_32 - return the number of
    leading zero bits in a 32-bit value
-------------------------------------------------*/

#define count_leading_zeros_32 _count_leading_zeros_32
__forceinline uint8_t _count_leading_zeros_32(uint32_t value)
{
	return uint8_t(_CountLeadingZeros(value));
}


/*-------------------------------------------------
    count_leading_ones_32 - return the number of
    leading one bits in a 32-bit value
-------------------------------------------------*/

#define count_leading_ones_32 _count_leading_ones_32
__forceinline uint8_t _count_leading_ones_32(uint32_t value)
{
	return uint8_t(_CountLeadingOnes(value));
}


/*-------------------------------------------------
    count_leading_zeros_64 - return the number of
    leading zero bits in a 64-bit value
-------------------------------------------------*/

#define count_leading_zeros_64 _count_leading_zeros_64
__forceinline uint8_t _count_leading_zeros_64(uint64_t value)
{
	return uint8_t(_CountLeadingZeros64(value));
}


/*-------------------------------------------------
    count_leading_ones_64 - return the number of
    leading one bits in a 64-bit value
-------------------------------------------------*/

#define count_leading_ones_64 _count_leading_ones_64
__forceinline uint8_t _count_leading_ones_64(uint64_t value)
{
	return uint8_t(_CountLeadingOnes64(value));
}

#endif // MAME_OSD_EIVCARM_H

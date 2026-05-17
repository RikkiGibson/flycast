/* 7zAlloc.h -- Allocation functions
2023-03-04 : Igor Pavlov : Public domain */
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

#ifndef ZIP7_INC_7Z_ALLOC_H
#define ZIP7_INC_7Z_ALLOC_H

#include "7zTypes.h"

EXTERN_C_BEGIN

void *SzAlloc(ISzAllocPtr p, size_t size);
void SzFree(ISzAllocPtr p, void *address);

void *SzAllocTemp(ISzAllocPtr p, size_t size);
void SzFreeTemp(ISzAllocPtr p, void *address);

EXTERN_C_END

#endif

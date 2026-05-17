/* 7zCrc.h -- CRC32 calculation
2023-04-02 : Igor Pavlov : Public domain */
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

#ifndef ZIP7_INC_7Z_CRC_H
#define ZIP7_INC_7Z_CRC_H

#include "7zTypes.h"

EXTERN_C_BEGIN

extern UInt32 g_CrcTable[];

/* Call CrcGenerateTable one time before other CRC functions */
void Z7_FASTCALL CrcGenerateTable(void);

#define CRC_INIT_VAL 0xFFFFFFFF
#define CRC_GET_DIGEST(crc) ((crc) ^ CRC_INIT_VAL)
#define CRC_UPDATE_BYTE(crc, b) (g_CrcTable[((crc) ^ (b)) & 0xFF] ^ ((crc) >> 8))

UInt32 Z7_FASTCALL CrcUpdate(UInt32 crc, const void *data, size_t size);
UInt32 Z7_FASTCALL CrcCalc(const void *data, size_t size);

typedef UInt32 (Z7_FASTCALL *CRC_FUNC)(UInt32 v, const void *data, size_t size, const UInt32 *table);

EXTERN_C_END

#endif

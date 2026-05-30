#
#	Portions Copyright 2026 The Hollycast Authors
#
#	This file is part of Hollycast.
#
#     Hollycast is free software: you can redistribute it and/or modify
#     it under the terms of the GNU General Public License as published by
#     the Free Software Foundation, either version 2 of the License, or
#     (at your option) any later version.
#
#     Hollycast is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#     GNU General Public License for more details.
#
#     You should have received a copy of the GNU General Public License
#     along with Hollycast.  If not, see <https://www.gnu.org/licenses/>.
#

set(HOLLYCAST_CHDWRITER_SOURCES
	chdwriter_bridge.cpp
	osd_work_queue_stub.cpp
	upstream/lib_util/cdrom.cpp
	upstream/lib_util/chd.cpp
	upstream/lib_util/chdcodec.cpp
	upstream/lib_util/corealloc.cpp
	upstream/lib_util/corefile.cpp
	upstream/lib_util/flac.cpp
	upstream/lib_util/corestr.cpp
	upstream/lib_util/hashing.cpp
	upstream/lib_util/huffman.cpp
	upstream/lib_util/ioprocs.cpp
	upstream/lib_util/md5.cpp
	upstream/lib_util/path.cpp
	upstream/lib_util/strformat.cpp
	upstream/lib_util/unicode.cpp
	upstream/lib_util/vecstream.cpp
	upstream/osd/osdcore.cpp
	upstream/osd/modules/file/stdfile.cpp
	upstream/osd/strconv.cpp
	upstream/thirdparty/utf8proc/utf8proc.c
)

if(NOT TARGET FLAC::FLAC)
	set(BUILD_CXXLIBS OFF)
	set(BUILD_PROGRAMS OFF)
	set(BUILD_EXAMPLES OFF)
	set(BUILD_TESTING OFF)
	set(BUILD_DOCS OFF)
	set(WITH_OGG OFF)
	set(WITH_ASM OFF)
	set(WITH_AVX OFF)
	set(WITH_FORTIFY_SOURCE OFF)
	set(WITH_STACK_PROTECTOR OFF)
	set(INSTALL_MANPAGES OFF)
	set(INSTALL_PKGCONFIG_MODULES OFF)
	set(INSTALL_CMAKE_CONFIG_MODULE OFF)
	set(ENABLE_MULTITHREADING OFF)
	add_subdirectory(${CMAKE_SOURCE_DIR}/core/deps/flac ${CMAKE_BINARY_DIR}/core/deps/flac EXCLUDE_FROM_ALL)
endif()

target_sources(${PROJECT_NAME} PRIVATE ${HOLLYCAST_CHDWRITER_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE FLAC::FLAC)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	CRLF=2
	UTF8PROC_STATIC=1
	HOLLYCAST_CHD_NO_AVHUFF=1
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/upstream/lib_util
	${CMAKE_CURRENT_SOURCE_DIR}/upstream/osd
	${CMAKE_CURRENT_SOURCE_DIR}/upstream/osd/modules/file
	${CMAKE_CURRENT_SOURCE_DIR}/upstream/thirdparty/utf8proc
	${CMAKE_SOURCE_DIR}/core/deps/libchdr/deps
	${CMAKE_SOURCE_DIR}/core/deps/libchdr/deps/lzma-24.05/include
	${CMAKE_SOURCE_DIR}/core/deps/libchdr/deps/zstd-1.5.6/lib
)

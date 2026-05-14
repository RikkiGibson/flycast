#
#	Copyright 2024 flyinghead
#	Portions Copyright 2026 The Hollycast Authors
#

set(HOLLYCAST_CHDWRITER_SOURCES
	chdwriter_bridge.cpp
	osd_work_queue_stub.cpp
	upstream/lib_util/cdrom.cpp
	upstream/lib_util/chd.cpp
	upstream/lib_util/chdcodec.cpp
	upstream/lib_util/corealloc.cpp
	upstream/lib_util/corefile.cpp
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

target_sources(${PROJECT_NAME} PRIVATE ${HOLLYCAST_CHDWRITER_SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
	CRLF=2
	UTF8PROC_STATIC=1
	HOLLYCAST_CHD_NO_FLAC=1
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

# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# This is inspired by
# https://github.com/WebKit/webkit/blob/b47c86e5d039967b845a9fe57ebb18bd6d3ce114/Source/cmake/OptionsCommon.cmake#L65

EXECUTE_PROCESS(COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
IF("${LD_VERSION}" MATCHES "GNU gold")
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
  SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")
ELSE()
  MESSAGE(WARNING "GNU gold linker isn't available, using the default system linker.")
ENDIF()

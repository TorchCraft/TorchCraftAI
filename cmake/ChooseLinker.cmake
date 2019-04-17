# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# This is inspired by
# https://github.com/WebKit/webkit/blob/b47c86e5d039967b845a9fe57ebb18bd6d3ce114/Source/cmake/OptionsCommon.cmake#L65

# Use LDD with clang if available. It is faster but resulted in issues with
# static variable initialization, in particular with multi-threading
# when building with GCC
EXECUTE_PROCESS(COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=lld -Wl,--version ERROR_QUIET OUTPUT_VARIABLE LLD_VERSION)
IF (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND "${LLD_VERSION}" MATCHES "LLD ")
  # using regular Clang or AppleClang
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
  SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
ELSE()
  EXECUTE_PROCESS(COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=gold -Wl,--version ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
  IF("${LD_VERSION}" MATCHES "GNU gold")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")
    SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=gold")
  ELSE()
    MESSAGE(WARNING "Neither GNU gold nor lld available, using the default system linker.")
  ENDIF()
ENDIF()

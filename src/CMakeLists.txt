# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

FIND_PACKAGE(Threads REQUIRED)
FIND_PACKAGE(gflags REQUIRED)

INCLUDE(cotire)
INCLUDE(BackwardConfig)

IF(MSVC)
  # Extra setup for MSVC
  INCLUDE_DIRECTORIES("${GLOG_ROOT_DIR}/include")
  INCLUDE_DIRECTORIES("${GLOG_ROOT_DIR}/src/windows")
  INCLUDE_DIRECTORIES("${GFLAGS_INCLUDE_DIR}")
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
  # Warning level 1 is enough
  IF(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    STRING(REGEX REPLACE "/W[0-4]" "/W1" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  ELSE()
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W1")
  ENDIF()
ENDIF(MSVC)


FILE(GLOB_RECURSE SRCS *.cpp)

# On Linux/MacOS, build a library plus executable.
# On Windows, build a monotholitic executable so we can run on SSCAIT,
# which doesn't provide the ability to upload DLLs to a place to which they'll be
# discovered for dynamic linking.
IF(MSVC)
  LINK_DIRECTORIES("${PROJECT_SOURCE_DIR}/deps")
  ADD_EXECUTABLE(cherrypi ${SRCS})
  # ws2_32 wsock32 Iphlpapi are required by libzmq
  TARGET_LINK_LIBRARIES(cherrypi Torch bwapilib bwem tcbwapi torchcraft common
    gflags_static glog shlwapi ws2_32 wsock32 Iphlpapi fmt)
ELSE(MSVC)
  LIST(REMOVE_ITEM SRCS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
  ADD_LIBRARY(cherpi ${SRCS})
  # Set warnings for everything that includes cherrypi
  TARGET_COMPILE_OPTIONS(cherpi PUBLIC "${CHERRYPI_WARNINGS}")
  SET_PROPERTY(TARGET cherpi PROPERTY POSITION_INDEPENDENT_CODE ON)
  SET_PROPERTY(TARGET cherpi PROPERTY
    COMPILE_DEFINITIONS ${BACKWARD_DEFINITIONS})
  TARGET_LINK_LIBRARIES(cherpi bwapilib bwreplib bwem tcbwapi torchcraft visdom
    Torch gflags glog pthread cnpy fmt common
    ${BACKWARD_LIBRARIES})
  IF(WITH_CPIDLIB)
    TARGET_LINK_LIBRARIES(cherpi cpid)
  ENDIF(WITH_CPIDLIB)
  ADD_EXECUTABLE(cherrypi main.cpp)
  TARGET_LINK_CHERPI(cherrypi)
ENDIF(MSVC)

IF(MSVC)
  INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}")
  INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}")
  INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/bwapilib")
  INCLUDE_DIRECTORIES("${PROJECT_SOURCE_DIR}/3rdparty/include")
  INCLUDE_DIRECTORIES("${CMAKE_BINARY_DIR}/tc/include")
ELSE(MSVC)
  TARGET_INCLUDE_DIRECTORIES(cherpi PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/3rdparty/bwapilib"
    "${PROJECT_SOURCE_DIR}/3rdparty/bwreplib"
    "${PROJECT_SOURCE_DIR}/3rdparty/include"
    "${BACKWARD_INCLUDE_DIRS}")
  # Use cotire to automatically generated a precompiled header. We don't want to
  # use the unity build support, though
  # SET_TARGET_PROPERTIES(cherpi PROPERTIES
  #   COTIRE_ADD_UNITY_BUILD FALSE)
  # SET_TARGET_PROPERTIES(cherpi PROPERTIES
  #   COTIRE_PREFIX_HEADER_IGNORE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
  # COTIRE(cherpi)
ENDIF(MSVC)

# Store binary in top-level dir
SET_TARGET_PROPERTIES(cherrypi PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

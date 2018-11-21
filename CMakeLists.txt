# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

CMAKE_MINIMUM_REQUIRED(VERSION 3.0 FATAL_ERROR)
CMAKE_POLICY(VERSION 3.0)
PROJECT(CherryPi)
ENABLE_TESTING()

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
INCLUDE(TargetLinkCherpi)

SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
OPTION(ONLY_BENCH "Only build cpid and distributed_bench w/o SC environments (requires linux)" OFF)
OPTION(ASAN "ASAN-enabled build" OFF)

IF(MSVC)
  ADD_DEFINITIONS(-DWITHOUT_POSIX)
  ADD_DEFINITIONS(-DBETTER_ENUMS_HAVE_CONSTEXPR -DNOMINMAX -D_USE_MATH_DEFINES)
  # On Windows, glog functions are annotated for DLLS but we want to link
  # statically.
  ADD_DEFINITIONS(-DGOOGLE_GLOG_DLL_DECL=)
  # Denote error re functions with no return statement (with have a few in
  # cbwapi) to warning level 3
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /w34716")
  SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
  SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
  SET(CHERRYPI_WARNINGS "")
  OPTION(BUILD_SHARED_LIBS "Build shared libraries" OFF)

  # ZSTD
  SET(ZSTD_USE_STATIC_RUNTIME ON)
  SET(ZSTD_BUILD_PROGRAMS OFF)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/3rdparty/pytorch/third_party/zstd/build/cmake)
  set_property(TARGET libzstd_static PROPERTY POSITION_INDEPENDENT_CODE ON)
  SET(ZSTD_LIBRARY libzstd_static)
  SET(ZSTD_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/3rdparty/pytorch/third_party/zstd/lib)
  INCLUDE_DIRECTORIES("${ZSTD_INCLUDE_DIR}")
ELSE(MSVC)
  ADD_COMPILE_OPTIONS("$<$<CONFIG:RELASE>:-O3 -march=native -DNDEBUG>")
  ADD_COMPILE_OPTIONS("-fdiagnostics-color")
  SET(CHERRYPI_WARNINGS
    -Wall
    -Wno-undefined-var-template
    -Wno-missing-braces
    -Wno-unknown-warning-option)
  OPTION(BUILD_SHARED_LIBS "Build shared libraries" ON)
  INCLUDE(UseGold)
ENDIF(MSVC)

# Static version required for advanced, experimental functionality
FIND_PACKAGE(ZSTD 1.0 REQUIRED static)

# Pytorch hack... Otherwise it redefines LOG
ADD_DEFINITIONS(-DC10_USE_GLOG)

ADD_DEFINITIONS(-DCEREAL_THREAD_SAFE)
INCLUDE_DIRECTORIES(3rdparty)
ADD_SUBDIRECTORY(3rdparty/fmt EXCLUDE_FROM_ALL)

OPTION(WITH_CPIDLIB "Include training library" ON)
# Gloo supports Linux only, and the training library depends on it
IF(APPLE OR MSVC)
  SET(WITH_CPIDLIB OFF)
ENDIF(APPLE OR MSVC)

IF(ONLY_BENCH)
  FIND_PACKAGE(Torch REQUIRED)
  ADD_DEFINITIONS(-DHAVE_TORCH)
  ADD_SUBDIRECTORY(cpid)
  ADD_SUBDIRECTORY(common)
  ADD_SUBDIRECTORY(./scripts/distributed_bench)
  TARGET_INCLUDE_DIRECTORIES(distributed_bench PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
ELSE(ONLY_BENCH)
  IF(NOT MSVC)
    ADD_SUBDIRECTORY(3rdparty/bwreplib EXCLUDE_FROM_ALL)
    ADD_SUBDIRECTORY(3rdparty/cnpy EXCLUDE_FROM_ALL)
    ADD_SUBDIRECTORY(3rdparty/torchcraft/BWEnv)
    TARGET_COMPILE_OPTIONS(BWEnv PRIVATE "${CHERRYPI_WARNINGS}")
  ENDIF(NOT MSVC)
  
  FIND_PACKAGE(Torch REQUIRED)
  ADD_DEFINITIONS(-DHAVE_TORCH)

  ADD_SUBDIRECTORY(3rdparty/bwapilib EXCLUDE_FROM_ALL)
  IF(MSVC)
    LINK_DIRECTORIES("${PROJECT_SOURCE_DIR}/deps")
  ENDIF(MSVC)

  IF(ASAN)
    # Enable ASAN for the following sub-projects only. We can't load an
    # ASAN-enabled BWEnv.dll in a non-ASAN OpenBW.
    SET(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
  ENDIF(ASAN)

  ADD_SUBDIRECTORY(3rdparty/torchcraft EXCLUDE_FROM_ALL)
  ADD_SUBDIRECTORY(tcbwapi)
  ADD_SUBDIRECTORY(3rdparty/bwem EXCLUDE_FROM_ALL)
  ADD_SUBDIRECTORY(common)
  IF(NOT MSVC)
    IF(WITH_CPIDLIB)
      ADD_SUBDIRECTORY(cpid)
      ADD_DEFINITIONS(-DHAVE_CPID)
    ENDIF(WITH_CPIDLIB)
  ENDIF(NOT MSVC)
  ADD_SUBDIRECTORY(src)
  IF(NOT MSVC)
    ADD_SUBDIRECTORY(scripts)
    ADD_SUBDIRECTORY(test)
    ADD_SUBDIRECTORY(tutorials)
    ADD_SUBDIRECTORY(visdom)
    IF(EXISTS "${PROJECT_SOURCE_DIR}/experimental")
      ADD_SUBDIRECTORY(experimental)
    ENDIF(EXISTS "${PROJECT_SOURCE_DIR}/experimental")
  ENDIF(NOT MSVC)

ENDIF(ONLY_BENCH)

# add a target to generate API documentation with Doxygen
FIND_PACKAGE(Doxygen)
IF(DOXYGEN_FOUND)
  CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  ADD_CUSTOM_TARGET(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen" VERBATIM)
ENDIF(DOXYGEN_FOUND)

# Copy test maps to build directory
FILE(COPY
  "${CMAKE_CURRENT_SOURCE_DIR}/maps"
  DESTINATION "${CMAKE_BINARY_DIR}"
 FILES_MATCHING PATTERN "*.sc[mx]")

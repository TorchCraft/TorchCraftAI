# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

CMAKE_MINIMUM_REQUIRED(VERSION 3.10 FATAL_ERROR)
CMAKE_POLICY(VERSION 3.0)
PROJECT(CherryPi)
ENABLE_TESTING()
if(POLICY CMP0058) 
    cmake_policy(SET CMP0058 NEW) 
endif() 

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
INCLUDE(TargetLinkCherpi)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
IF(APPLE)
  # Using C++17 on macOS requires a recent enough XCode version
  IF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    IF(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "10")
      MESSAGE(FATAL_ERROR
        "Minimum required XCode version is 10.0, ${CMAKE_CXX_COMPILER_VERSION} detected")
    ENDIF()
    ADD_DEFINITIONS(-D_LIBCPP_DISABLE_AVAILABILITY)
  ENDIF(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
ENDIF(APPLE)

OPTION(ONLY_BENCH "Only build cpid and distributed_bench w/o SC environments (requires linux)" OFF)
OPTION(ASAN "ASAN-enabled build" OFF)
OPTION(TSAN "TSAN-enabled build" OFF)
OPTION(BUILD_OPENBW "Build BWAPI/OpenBW with CherryPi" OFF)

IF(BUILD_OPENBW)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/3rdparty/openbw)
ENDIF(BUILD_OPENBW)

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
  FILE(RELATIVE_PATH build_to_source_rel ${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR})
  ADD_COMPILE_OPTIONS("-fdebug-prefix-map=${build_to_source_rel}=${PROJECT_SOURCE_DIR}/")
  SET(CHERRYPI_WARNINGS
    -Wall
    -Wno-undefined-var-template
    -Wno-missing-braces
    -Wno-unknown-warning-option)
  OPTION(BUILD_SHARED_LIBS "Build shared libraries" ON)
  INCLUDE(ChooseLinker)

  # ALE for Atari emulator
  ADD_SUBDIRECTORY("${PROJECT_SOURCE_DIR}/3rdparty/ale" EXCLUDE_FROM_ALL)
  SET_TARGET_PROPERTIES(ale-lib PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  TARGET_INCLUDE_DIRECTORIES(ale-lib SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/3rdparty/ale/src")
ENDIF(MSVC)

# Static version required for advanced, experimental functionality
FIND_PACKAGE(ZSTD 1.0 REQUIRED static)

# GLOG < 0.3.5 (the one in ubuntu packets) doesn't support cmake :(
FIND_PACKAGE(glog CONFIG NAMES google-glog glog)
IF(NOT TARGET glog::glog)
  FIND_LIBRARY(LIB_glog NAMES glog)
ELSE()
  SET(LIB_glog glog::glog)
ENDIF(NOT TARGET glog::glog)

# Pytorch hack... Otherwise it redefines LOG
ADD_DEFINITIONS(-DC10_USE_GLOG)

ADD_DEFINITIONS(-DCEREAL_THREAD_SAFE)
INCLUDE_DIRECTORIES(3rdparty)
ADD_SUBDIRECTORY(3rdparty/fmt EXCLUDE_FROM_ALL)
ADD_SUBDIRECTORY(3rdparty/glop)

# Gloo supports Linux only, and the training library depends on it
IF (MSVC OR APPLE)
  OPTION(WITH_CPIDLIB "Include training library (!UNSUPPORTED, DOESNT BUILD!)" OFF)
  SET(CPID_DISTRIBUTED 0)
ELSE()
  OPTION(WITH_CPIDLIB "Include training library" ON)
  SET(CPID_DISTRIBUTED 1)
  ADD_DEFINITIONS(-DHAVE_C10D)
ENDIF()

FIND_PACKAGE(Torch REQUIRED)
ADD_SUBDIRECTORY(common)

IF(ONLY_BENCH)
  ADD_SUBDIRECTORY(cpid)
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

  ADD_SUBDIRECTORY(3rdparty/bwapilib EXCLUDE_FROM_ALL)
  IF(MSVC)
    LINK_DIRECTORIES("${PROJECT_SOURCE_DIR}/deps")
  ENDIF(MSVC)

  IF(ASAN AND TSAN)
    message( FATAL_ERROR "You can not build with both ASAN and TSAN" )
  ENDIF(ASAN AND TSAN)

  IF(ASAN)
    # Enable ASAN for the following sub-projects only. We can't load an
    # ASAN-enabled BWEnv.dll in a non-ASAN OpenBW.
    SET(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
  ELSE(ASAN)
    IF(TSAN)
      SET(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
    ENDIF(TSAN)
  ENDIF(ASAN)

  ADD_SUBDIRECTORY(3rdparty/torchcraft EXCLUDE_FROM_ALL)
  ADD_SUBDIRECTORY(tcbwapi)
  ADD_SUBDIRECTORY(3rdparty/bwem EXCLUDE_FROM_ALL)
  IF(NOT MSVC)
    IF(WITH_CPIDLIB)
      ADD_SUBDIRECTORY(cpid)
      ADD_DEFINITIONS(-DHAVE_CPID)
      ADD_SUBDIRECTORY(atari)
    ENDIF(WITH_CPIDLIB)
  ENDIF(NOT MSVC)
  ADD_SUBDIRECTORY(src)
  IF(NOT MSVC)
    ADD_SUBDIRECTORY(scripts)
    ADD_SUBDIRECTORY(test)
    ADD_SUBDIRECTORY(tutorials)
    ADD_SUBDIRECTORY(visdom)
    ADD_SUBDIRECTORY(targeting)
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

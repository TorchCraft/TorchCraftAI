# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# - Try to find Hiredis
# Once done this will define
# HIREDIS_FOUND - System has HIREDIS
# HIREDIS_INCLUDE_DIRS - The HIREDIS include directories
# HIREDIS_LIBRARIES - The libraries needed to use HIREDIS
# HIREDIS_DEFINITIONS - Compiler switches required for using HIREDIS

FIND_PATH(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS ENV CPLUS_INCLUDE_PATH)
FIND_LIBRARY(HIREDIS_LIBRARY NAMES hiredis HINTS ENV LIBRARY_PATH ENV LD_LIBRARY_PATH)

SET(HIREDIS_LIBRARIES ${HIREDIS_LIBRARY})
SET(HIREDIS_INCLUDE_DIRS ${HIREDIS_INCLUDE_DIR})

INCLUDE(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set HIREDIS_FOUND to TRUE
# if all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(HIREDIS DEFAULT_MSG HIREDIS_LIBRARY HIREDIS_INCLUDE_DIR)

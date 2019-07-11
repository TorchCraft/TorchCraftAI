# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# - Try to find Mujoco
# Once done this will define
# MUJOCO_FOUND - System has Mujoco
# MUJOCO_INCLUDE_DIRS - The Mujoco include directories
# MUJOCO_LIBRARIES - The libraries needed to use Mujoco

FIND_PATH(MUJOCO_INCLUDE_DIR mujoco.h)
FIND_LIBRARY(MUJOCO_LIBRARY libmujoco150nogl.so)

INCLUDE(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set MUJOCO_FOUND to TRUE
# if all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(MUJOCO DEFAULT_MSG MUJOCO_LIBRARY MUJOCO_INCLUDE_DIR)

IF(MUJOCO_FOUND)
  ADD_DEFINITIONS(-DHAVE_MUJOCO)
  ADD_LIBRARY(Mujoco SHARED IMPORTED)
  SET_TARGET_PROPERTIES(Mujoco PROPERTIES
    IMPORTED_LOCATION "${MUJOCO_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MUJOCO_INCLUDE_DIR}"
  )
ENDIF(MUJOCO_FOUND)

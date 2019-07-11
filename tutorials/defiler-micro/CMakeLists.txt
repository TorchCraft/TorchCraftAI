# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

FILE(GLOB COMMON
  common.cpp
  model.cpp
  flags.cpp
  micromodule.cpp
  defilermicromodule.cpp
  defilersnapshotter.cpp
  modeldummy.cpp
  modelpf.cpp
  modelsimplecnn.cpp
  modeldefiler.cpp
  trainingsetup.cpp
  trainingstate.cpp
  )

FOREACH(BIN defiler_train_micro defiler_snapshotjob)
  ADD_EXECUTABLE(${BIN} "${BIN}.cpp" ${COMMON})
  TARGET_LINK_CHERPI(${BIN})
ENDFOREACH(BIN)

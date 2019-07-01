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
  gasmicromodule.cpp
  gas_trainer.cpp
  gas_trainer_impala.cpp
  modeldummy.cpp	
  modelpf.cpp	
  modelsimplecnn.cpp	
  gasmodelpf.cpp
  gasmodelcnn.cpp
  gasmodel_global.cpp
  gasmodel_globalcnn.cpp
  reward.cpp
  scenarios.cpp
  trainingsetup.cpp
  trainingstate.cpp
  )

FOREACH(BIN train_micro)
  ADD_EXECUTABLE(${BIN} "${BIN}.cpp" ${COMMON})
  TARGET_LINK_CHERPI(${BIN})
ENDFOREACH(BIN)

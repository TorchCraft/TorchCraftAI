# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

IF(WITH_CPIDLIB)
  ADD_SUBDIRECTORY(building-placer)
  ADD_SUBDIRECTORY(micro)
  ADD_SUBDIRECTORY(defiler-micro)
ENDIF(WITH_CPIDLIB)

# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

ADD_SUBDIRECTORY(botplay)
IF(WITH_CPIDLIB)
  ADD_SUBDIRECTORY(bo-switch)
  ADD_SUBDIRECTORY(building-placer)
ENDIF(WITH_CPIDLIB)

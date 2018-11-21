# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

FOREACH(BIN botplay)
  ADD_EXECUTABLE(${BIN} "${BIN}.cpp")
  TARGET_LINK_CHERPI(${BIN})
ENDFOREACH(BIN)

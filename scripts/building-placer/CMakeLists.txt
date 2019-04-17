# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

FOREACH(BIN collect-replay-samples train-supervised knn-baseline)
  ADD_EXECUTABLE("bp-${BIN}" "${BIN}.cpp")
  TARGET_LINK_CHERPI("bp-${BIN}")
ENDFOREACH(BIN)

# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

ADD_LIBRARY(atari "atari_env.cpp")
TARGET_LINK_LIBRARIES(atari ale-lib common)

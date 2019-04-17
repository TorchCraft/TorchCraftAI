# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

FIND_PACKAGE(CURL REQUIRED)
FIND_PACKAGE(Threads)

ADD_DEFINITIONS(-DCEREAL_RAPIDJSON_HAS_STDSTRING)

ADD_LIBRARY(visdom visdom.cpp)
SET_PROPERTY(TARGET visdom PROPERTY POSITION_INDEPENDENT_CODE ON)
TARGET_INCLUDE_DIRECTORIES(visdom
  PUBLIC
    "${PROJECT_SOURCE_DIR}"
    "${CURL_INCLUDE_DIR}"
    # XXX Pick up rapidjson from cereal for now
    "${PROJECT_SOURCE_DIR}/3rdparty/cereal/include/cereal/external/"
    "${PROJECT_SOURCE_DIR}/3rdparty/include"
    "${PROJECT_SOURCE_DIR}/3rdparty/cereal/include"
)

# Torch is here as a hack to find the right CUDA Paths. FindCUDA will work.
TARGET_LINK_LIBRARIES(visdom ${CURL_LIBRARIES} ${LIB_glog} Torch ${CMAKE_THREAD_LIBS_INIT})

ADD_EXECUTABLE(visdom_test test.cpp)
TARGET_LINK_LIBRARIES(visdom_test visdom ${LIB_glog} gflags)

# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

FUNCTION(TARGET_LINK_WHOLE TARGET LIBS)
  IF(MSVC OR BUILD_SHARED_LIBS)
    TARGET_LINK_LIBRARIES(${TARGET} ${LIBS})
  ELSE(MSVC OR BUILD_SHARED_LIBS)
    IF(APPLE)
      TARGET_LINK_LIBRARIES(${TARGET} -Wl,-all_load $${LIBS})
    ELSE()
      TARGET_LINK_LIBRARIES(${TARGET} -Wl,--whole-archive ${LIBS} -Wl,--no-whole-archive)
    ENDIF()
  ENDIF(MSVC OR BUILD_SHARED_LIBS)
ENDFUNCTION(TARGET_LINK_WHOLE)

# Always use this function to link against the main library (cherpi). It will
# set up the right linker flags depending on the current build mode
FUNCTION(TARGET_LINK_CHERPI TARGET)
  TARGET_LINK_WHOLE(${TARGET} cherpi)
ENDFUNCTION(TARGET_LINK_CHERPI)

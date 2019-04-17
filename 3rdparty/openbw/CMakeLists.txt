cmake_minimum_required(VERSION 3.1)

if(NOT OPENBW_DIR)
  set(OPENBW_DIR "${CMAKE_CURRENT_LIST_DIR}/openbw" CACHE STRING "" FORCE)
endif()

add_subdirectory(bwapi)

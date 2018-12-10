cmake_minimum_required(VERSION 3.1)

if(NOT DEFINED OPENBW_DIR_old OR (OPENBW_DIR EQUAL OPENBW_DIR_old))
  set(OPENBW_DIR ${CMAKE_CURRENT_LIST_DIR}/openbw)
endif()
set(OPENBW_DIR_old ${OPENBW_DIR} CACHE INTERNAL "Copy of OPENBW_DIR")

add_subdirectory(bwapi)

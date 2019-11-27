# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

 configure_file(
	 protobuf.CMakeLists.txt
	 ${CMAKE_BINARY_DIR}/protobuf-download/CMakeLists.txt)
 execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
	 RESULT_VARIABLE result
	 WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/protobuf-download )
 if(result)
	 message(FATAL_ERROR "CMake step for protobuf failed: ${result}")
 endif()
 execute_process(COMMAND ${CMAKE_COMMAND} --build .
	 RESULT_VARIABLE result
	 WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/protobuf-download )
 if(result)
	 message(FATAL_ERROR "Build step for protobuf failed: ${result}")
 endif()
 
 set(protobuf_BUILD_TESTS OFF CACHE INTERNAL "Disable Protobuf tests")
 add_subdirectory(
	 ${CMAKE_BINARY_DIR}/protobuf-src/cmake
	 ${CMAKE_BINARY_DIR}/protobuf-build)
 message(STATUS "Adding CMake Subproject: Protobuf...DONE")
 
 find_package(Protobuf REQUIRED)
# 
# set(PROTO_HDRS)
# set(PROTO_SRCS)
# FILE(GLOB proto_files *.proto)
# # Get Protobuf include dir
# get_target_property(protobuf_dirs protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)
# foreach(dir ${protobuf_dirs})
	# if ("${dir}" MATCHES "BUILD_INTERFACE")
		# list(APPEND PROTO_DIRS "--proto_path=${dir}")
  # endif()
# endforeach()
# foreach (PROTO_FILE ${proto_files})
	# #message(STATUS "protoc proto(cc): ${PROTO_FILE}")
	# get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
	# set(PROTO_HDR ${PROJECT_BINARY_DIR}/3rdparty/glop/${PROTO_NAME}.pb.h)
	# set(PROTO_SRC ${PROJECT_BINARY_DIR}/3rdparty/glop/${PROTO_NAME}.pb.cc)
	# message(STATUS "protoc hdr: ${PROTO_HDR}")
	# message(STATUS "protoc src: ${PROTO_SRC}")
	# add_custom_command(
		# OUTPUT ${PROTO_SRC} ${PROTO_HDR}
		# COMMAND protobuf::protoc
		# "--proto_path=${PROJECT_SOURCE_DIR}"
		# "${PROTO_DIRS}"
		# "--cpp_out=${PROJECT_SOURCE_DIR}/3rdparty/glop/"
		# ${PROTO_FILE}
		# DEPENDS ${PROTO_FILE} protobuf::protoc
		# COMMENT "Running C++ protocol buffer compiler on ${PROTO_FILE}"
		# VERBATIM)
	# list(APPEND PROTO_HDRS ${PROTO_HDR})
	# list(APPEND PROTO_SRCS ${PROTO_SRC})
# endforeach()
#PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS linear_solver.proto)

FILE(GLOB SRCS *.cc base/*.cc util/*.cc glop/*.cc lp_data/*.cc port/*.cc)

ADD_LIBRARY(glop ${SRCS} )#${PROTO_SRCS})
target_include_directories(glop PRIVATE
	$<TARGET_PROPERTY:protobuf::libprotobuf,INTERFACE_INCLUDE_DIRECTORIES>)

TARGET_LINK_LIBRARIES(glop glog protobuf::libprotobuf )

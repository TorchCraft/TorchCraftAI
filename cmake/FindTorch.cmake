# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# FindTorch
# -------
#
# Finds the Torch library
#
# This will define the following variables:
#
#   TORCH_FOUND    	       			- True if the system has the Torch library
#   TORCH_INCLUDE_DIRECTORIES  	- The include directories for torch
#   TORCH_LIBRARIES 						- Libraries to link to
#
# and the following imported targets::
#
#   Torch

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/FindCUDA")

FIND_PATH(PYTORCH_DIR torch/csrc/api/include/torch/torch.h 
  PATHS
    "${PYTORCH_DIRECTORY}"
    "${PROJECT_SOURCE_DIR}/3rdparty/pytorch"
    NO_DEFAULT_PATH
  )
IF(MSVC)
  SET(TORCH_LIBDIR "${PYTORCH_DIR}/torch/lib/")
ELSE(MSVC)
  SET(TORCH_LIBDIR "${PYTORCH_DIR}/torch/lib/tmp_install/lib")
ENDIF(MSVC)

FIND_LIBRARY(TORCH_LIBRARY torch
  PATHS "${TORCH_LIBDIR}"
    NO_DEFAULT_PATH
  )
FIND_LIBRARY(CAFFE2_LIB caffe2
  PATHS "${TORCH_LIBDIR}"
    NO_DEFAULT_PATH
  )
FIND_LIBRARY(C10_LIB c10
  PATHS "${TORCH_LIBDIR}"
    NO_DEFAULT_PATH
  )
SET(TORCH_INCLUDE_DIRECTORIES
  ${PYTORCH_DIR}
  "${PYTORCH_DIR}/torch/csrc/api/include"
  "${PYTORCH_DIR}/aten/src"
  "${PYTORCH_DIR}/aten/src/TH"
  "${PYTORCH_DIR}/c10"
  # These are hard coded ATEN_BUILDPATHs
  "${PYTORCH_DIR}/torch/lib/tmp_install/include"
  "${PYTORCH_DIR}/torch/lib/tmp_install/include/ATen"
  "${PYTORCH_DIR}/torch/lib/tmp_install/include/TH"
  )

INCLUDE(FindPackageHandleStandardArgs)
MARK_AS_ADVANCED(TORCH_LIBRARY TORCH_INCLUDE_DIRECTORIES)

### Optionally, link CUDA
FIND_PACKAGE(CUDA)
IF(CUDA_FOUND)
  ADD_DEFINITIONS(-DCUDA_FOUND)
  FIND_LIBRARY(CAFFE2_CUDA_LIB caffe2_gpu
    PATHS "${TORCH_LIBDIR}"
    NO_DEFAULT_PATH
  )
  FIND_LIBRARY(C10_CUDA_LIB c10_cuda
    PATHS "${TORCH_LIBDIR}"
    NO_DEFAULT_PATH
  )
  LIST(APPEND TORCH_INCLUDE_DIRECTORIES ${CUDA_TOOLKIT_INCLUDE})
  IF(MSVC)
	LIST(APPEND CAFFE2_LIB 
		${CUDA_TOOLKIT_ROOT_DIR}/lib/x64/cuda.lib
		${CUDA_TOOLKIT_ROOT_DIR}/lib/x64/cudart.lib 
		${CUDA_TOOLKIT_ROOT_DIR}/lib/x64/nvrtc.lib)
  ELSE(MSVC)
	LIST(APPEND CAFFE2_LIB -L"${CUDA_TOOLKIT_ROOT_DIR}/lib64" cuda cudart nvrtc nvToolsExt)
  ENDIF(MSVC)
  LIST(APPEND TORCH_INCLUDE_DIRECTORIES "${PYTORCH_DIR}/aten/src/THC")
  LIST(APPEND TORCH_INCLUDE_DIRECTORIES "${PYTORCH_DIR}/torch/lib/tmp_install/include/THC")
ENDIF(CUDA_FOUND)

ADD_DEFINITIONS(-DNO_PYTHON)
ADD_LIBRARY(Torch SHARED IMPORTED)
IF(MSVC)
  SET_TARGET_PROPERTIES(Torch PROPERTIES IMPORTED_IMPLIB "${TORCH_LIBRARY}")
ELSE(MSVC)
  SET_TARGET_PROPERTIES(Torch PROPERTIES IMPORTED_LOCATION "${TORCH_LIBRARY}")
ENDIF(MSVC)
SET_TARGET_PROPERTIES(Torch PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${TORCH_INCLUDE_DIRECTORIES}"
  INTERFACE_LINK_LIBRARIES "${CAFFE2_LIB};${CAFFE2_CUDA_LIB};${C10_LIB};${C10_CUDA_LIB}"
)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(TORCH DEFAULT_MSG TORCH_LIBRARY TORCH_INCLUDE_DIRECTORIES)

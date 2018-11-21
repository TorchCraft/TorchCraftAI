---
id: install-windows
title: Installation (Windows)
---

TorchCraftAI's modular framework and its CherryPi bot work on Windows.
For training machine learning models, you may need to install Linux on a virtual machine and refer to [the Linux instructions](./install-linux.md)

## Prerequisites
### Install Required Packages

* StarCraft: Brood War 1.16.1 (newer versions like 1.18 and Remastered are incompatible with the Brood War API)
* [Visual Studio 2017](https://www.visualstudio.com/downloads/) (the Community edition is free)
* [BWAPI (Brood War API) 4.2.0](https://github.com/bwapi/bwapi/releases/tag/v4.2.0)
* [Anaconda](https://www.anaconda.com/download/#windows), the Python 3 version.
* [Git for Windows, for Git Bash](https://gitforwindows.org/)
* [CUDA](https://developer.nvidia.com/cuda-downloads?target_os=Windows&target_arch=x86_64&target_version=10&target_type=exenetwork) if you have a GPU

### Clone the TorchCraftAI Repository

Git bash is probably easiest for this step:
```bash
git clone https://github.com/TorchCraft/TorchCraftAI --recursive
cd TorchCraftAI
git submodule update --init --recursive
```
### Build PyTorch Backend Libraries

**Do everything in an Anaconda prompt.** 

#### Initialize Environment
Note: This assumes you're using the Community Edition of VS, change the path below accordingly
```bash
set "VS150COMNTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build"
set CMAKE_GENERATOR=Visual Studio 15 2017 Win64
set DISTUTILS_USE_SDK=1
call "%VS150COMNTOOLS%\vcvarsall.bat" x64 -vcvars_ver=14.11
```

#### Build PyTorch

```bash
$WORKDIR is where we launch cherrypi.exe, most commonly the repository root directory. It is also where bwapi-data/read and bwapi-data/write is.

conda install numpy pyyaml mkl mkl-include setuptools cmake cffi typing
python setup.py build

xcopy 3rdparty\pytorch\torch\lib\c10.dll $WORKDIR
xcopy 3rdparty\pytorch\torch\lib\torch.dll $WORKDIR
xcopy 3rdparty\pytorch\torch\lib\caffe2.dll $WORKDIR

#### If you have an NVIDIA GPU
xcopy 3rdparty\pytorch\torch\lib\caffe2_gpu.dll $WORKDIR

This step is needed to find nvtools correctly:
- xcopy "C:\Program Files\NVIDIA Corporation\NvToolsExt\bin\x64\nvToolsExt64_1.dll" $WORKDIR
```
Note: You may have to [disable TDR](https://docs.nvidia.com/gameworks/content/developertools/desktop/timeout_detection_recovery.htm) somehow, but don't do that unless the code won't run.


### Install StarCraft

See [Play Games with CherryPi](play-games.md).

## Compilation and Usage

### Build TorchCraftAI and CherryPi

**Do everything in an Anaconda prompt.** 
Anaconda is only needed for pytorch, but we use the same environment to reduce build issues.
Additionally, make sure the prompt has the commands run in [Initialize Environment](#initialize-environment)

#### Build gflags
```bash
cd 3rdparty\gflags
del /s /q build
mkdir build
cd build
cmake .. -DCMAKE_CXX_FLAGS_RELEASE="/MD /MP" -G "Visual Studio 15 2017 Win64"
msbuild gflags.sln /property:Configuration=Release /m
xcopy /y lib\Release\gflags_static.lib ..\..
cd ..\..\..
```

#### Build glog
```bash
cd 3rdparty\glog
rd /s build && mkdir build && cd build
set gflags_DIR=../../gflags/build/
cmake .. -DCMAKE_CXX_FLAGS_RELEASE="/MD /MP" -G "Visual Studio 15 2017 Win64"
msbuild glog.sln /property:Configuration=Release /m
xcopy /y Release\glog.lib ..\..
cd ..\..\..
```

#### Build ZeroMQ, a messaging library
```bash
cd 3rdparty
rd /s libzmq && robocopy /e ..\3rdparty\torchcraft\BWEnv\include\libzmq .\libzmq
cd libzmq
msbuild ./builds/msvc/vs2017/libzmq.sln /property:Configuration=DynRelease
xcopy /y bin\x64\Release\v141\dynamic\libzmq.lib ..\zmq.lib
cd ..\..\
```

#### Build CherryPi!
```bash
rd /s deps && mkdir deps
xcopy 3rdparty deps
mkdir build && cd build
cmake .. -DMSVC=true -DZMQ_LIBRARY="../3rdparty/zmq.lib" -DZMQ_INCLUDE_DIR="../3rdparty/libzmq/include" -DGFLAGS_LIBRARY="../3rdparty/gflags_static.lib" -DGFLAGS_INCLUDE_DIR="../3rdparty/gflags/build/include" -DGLOG_ROOT_DIR="../3rdparty/glog" -DCMAKE_CXX_FLAGS_RELEASE="/MP /EHsc" -G "Visual Studio 15 2017 Win64"
# It will complain about missing GFLAGS_LIBRARY, but that's fine.
msbuild CherryPi.sln  /property:Configuration=Release /m
```

### Play your first game with CherryPi
See [Play Games with CherryPi](play-games.md)

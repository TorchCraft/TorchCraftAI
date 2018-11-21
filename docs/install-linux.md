---
id: install-linux
title: Installation (Linux)
---

The follow instructions are tailored to the latest LTS release of Ubuntu, 18.04.

## Prerequisites

### TL;DR

Here's a short overview of everything that's required:
- Libraries and development files for zeromq (version 4.2), gflags, glog, curl and sdl2
- All PyTorch requirements to compile PyTorch
- [NCCL2](https://developer.nvidia.com/nccl) in order to use TorchCraftAI's model training library

### Install Required Packages
```bash
sudo apt-get update
sudo apt-get install git libsdl2-dev libzmq3-dev binutils-dev libdw-dev libgflags-dev libnuma-dev cmake curl libcurl-dev libgoogle-glog-dev
```

`apt-get` may complain about not finding an installation candidate for `libcurl-dev`.
In this case, replace `libcurl-dev` with one of the suggested alternatives.

For training on GPUs, NVIDIA's CUDA Toolkit is required. We strongly recommend [CUDA 9.2](https://developer.nvidia.com/cuda-92-download-archive) as older and newer versions suffer from incompabilities with newer compilers and the PyTorch stack, respectively.
For optimal performance, be sure to install [cuDNN](https://developer.nvidia.com/cudnn) as well.


### Clone the TorchCraftAI Repository
```bash
git clone https://github.com/TorchCraft/TorchCraftAI --recursive
cd TorchCraftAI
```

### Build PyTorch Backend Libraries
Since we live on the bleeding edge of PyTorch, you'll unfortunately have to compile your own pytorch libraries.

Here's a summary for compiling with an Anaconda installation:

```bash

# Download Anaconda from https://www.anaconda.com/download/#linux
bash Anaconda-latest-Linux-x86_64.sh

export CMAKE_PREFIX_PATH="$(dirname $(which conda))/../" # [anaconda root directory]

# Install basic dependencies
conda install numpy pyyaml mkl mkl-include setuptools cmake cffi typing
conda install -c mingfeima mkldnn

# Add LAPACK support for the GPU
conda install -c pytorch magma-cuda92 # or [magma-cuda80 | magma-cuda91] depending on your cuda version

pushd 3rdparty/pytorch/tools/
REL_WITH_DEB_INFO=1 python build_libtorch.py
popd
```

We recommend the [github instructions](https://github.com/ebetica/pytorch/tree/agppv0.4-1#from-source) if you run into issues with these steps.
Alternatively, instead of using Anaconda, you may install everything yourself, or choose not to install every optimization, such as mkl-dnn, if you prefer for a simpler installation process.

### Build and install Zstandard

We also require a manual Zstandard installation. This can be done as follows:
```bash
curl -sSL https://github.com/facebook/zstd/archive/v1.3.3.tar.gz | tar xvzf -
pushd zstd-1.3.3/build/cmake
cmake . -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC -DZSTD_BUILD_STATIC=ON -DCMAKE_BUILD_TYPE=Release -DZSTD_LEGACY_SUPPORT=0
make -j$(nproc)
sudo make install
popd
```

### Build and Install OpenBW
```bash
pushd 3rdparty/openbw
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=release -DOPENBW_ENABLE_UI=1
make -j$(nproc)
sudo make install
popd
```

### Install StarCraft
OpenBW requires the MPQ data files from an installation of StarCraft: Brood War.
You can either copy them from your existing StarCraft installation (maybe on a Windows machine) or perform the installation locally with Wine:
1. [Install Wine](https://wiki.winehq.org/Ubuntu)
2. From inside Wine, install StarCraft 1.16.1. Note that MPQs from newer versions (like 1.18 or 1.2 (Remastered)) are not supported yet
3. Find the MPQ files: `find ~/.wine/ -name "*.mpq"`
4. Set the MPQ file location for OpenBW: `echo 'export OPENBW_MPQ_PATH=[Path where you found the MPQs]' >> ~/.bashrc`


## Compilation and Usage

### Build TorchCraftAI and CherryPi
Back at the top-level directory of the TorchCraftAI repository:

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=relwithdebinfo [-DWITH_CPIDLIB=OFF] # Turn CPIDLIB off if you don't have the NCCL2 library installed
make -j$(nproc)
```

Verify your setup by running one of TorchCraftAI's test suites:
```bash
# Go back to the top-level directory
cd ..

./build/test/test_core -pass
```

### Play your first game with CherryPi
See [Play Games with CherryPi](play-games.md)

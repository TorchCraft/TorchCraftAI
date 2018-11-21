---
id: install-macos
title: Installation (macOS)
---

TorchCraftAI's modular framework and its CherryPi bot work on macOS. For training machine learning models however, you will (provided your machine features a CUDA-enabled GPU) need to install Linux on a virtual machine and refer to [the Linux instructions](./install-linux.md).

## Prerequesites

### Install required packages:
- Install [XCode](https://developer.apple.com/xcode/)
- Install [Homebrew](https://brew.sh/)
- `brew install --with-toolchain llvm`
- `brew install cmake sdl2 zmq gflags glog wget zstd`

### Clone the TorchCraftAI Repository
```bash
git clone https://github.com/TorchCraft/TorchCraftAI --recursive
cd TorchCraftAI
```

### Build PyTorch Backend Libraries
```bash
pushd 3rdparty/pytorch/tools
# You may need to prefix this with:
#
#  MACOSX_DEPLOYMENT_TARGET=10.9 CC=clang CXX=clang++
#
LDSHARED="cc -dynamiclib -undefined dynamic_lookup" REL_WITH_DEB_INFO=1 python build_libtorch.py
popd
```

### Build and Install OpenBW
```bash
pushd 3rdparty/openbw
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=relwithdebinfo -DOPENBW_ENABLE_UI=1
make -j$(sysctl -n hw.ncpu)
sudo make install
popd
```

### Install StarCraft
OpenBW requires the MPQ data files from an installation of StarCraft: Brood War.
You can either copy them from your existing StarCraft installation (maybe on a Windows machine) or perform the installation locally on your Mac, or with Wine:
1. `brew install wine`
2. From inside Wine, install StarCraft 1.16.1. Note that MPQs from newer versions (like 1.18 or 1.2 (Remastered)) are not supported yet.
3. Find the MPQ files: `find ~/.wine/ -name "*.mpq"`
4. Set the MPQ file location for OpenBW: `echo 'export OPENBW_MPQ_PATH=[Path where you found the MPQs]' >> ~/.bashrc`


## Compilation and Usage

### Build TorchCraftAI and CherryPi
Back at the top-level directory of the TorchCraftAI repository:

```bash
# If you're using a system-wide installation of Intel MKL:
source /opt/intel/bin/compilervars.sh intel64

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=relwithdebinfo
make -j$(sysctl -n hw.ncpu)
```

Verify your setup by running one of TorchCraftAI's test suites:
```bash
# Go back to the top-level directory
cd ..

./build/test/test_core -pass
```

### Play your first game with CherryPi
See [Play Games with CherryPi](play-games.md)

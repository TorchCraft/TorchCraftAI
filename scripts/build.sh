#!/usr/bin/env bash
CP_CMAKE_OPTIONS="${CP_CMAKE_OPTIONS:-}"
BWAPI_INSTALL_PREFIX="${BWAPI_INSTALL_PREFIX:-/usr/local}"
MAX_JOBS="${MAX_JOBS:-1}"
PYTORCH_LDSHARED="${PYTORCH_LDSHARED:-1}"
BUILD_WITH_NINJA="${BUILD_WITH_NINJA:-0}"
BUILD_OPENBW="${BUILD_OPENBW:-1}"
BUILD_CHERRYPI="${BUILD_CHERRYPI:-1}"
BUILD_DIR="${BUILD_DIR:-build}"
REBUILD_PYTORCH="${REBUILD_PYTORCH:-0}"
SPACER="============================================================"

set -ex
echo "$SPACER"
echo "Will build CherryPi using maximum $MAX_JOBS jobs"
echo "$SPACER"

echo "Building OpenBW to $BWAPI_INSTALL_PREFIX"
if [ "$BUILD_OPENBW" -eq "0" ]; then
  echo "-- skipped"
else
  mkdir -p 3rdparty/openbw/build
  pushd 3rdparty/openbw/build
  cmake .. -DCMAKE_BUILD_TYPE=relwithdebinfo -DOPENBW_ENABLE_UI=1 -DCMAKE_INSTALL_PREFIX:PATH=$BWAPI_INSTALL_PREFIX
  make -j$MAX_JOBS install
  popd
fi
echo "$SPACER"

echo "Building PyTorch"
if [ "$REBUILD_PYTORCH" -eq "1" ]; then
  (cd 3rdparty/pytorch; git clean -fd; python setup.py clean)
fi

if [ ! -e 3rdparty/pytorch/torch/lib/tmp_install ]; then
  (cd 3rdparty/pytorch/tools; MAX_JOBS=$MAX_JOBS LDSHARED=$PYTORCH_LDSHARED REL_WITH_DEB_INFO=1 python build_libtorch.py);
fi
echo "$SPACER"

echo "Building CherryPi"
if [ "$BUILD_CHERRYPI" -eq "0" ]; then
  echo "-- skipped"
else
  mkdir -p $BUILD_DIR
  pushd $BUILD_DIR
  if [ "$BUILD_WITH_NINJA" -eq 1 ]; then
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=relwithdebinfo -DBWAPI_DIR=$BWAPI_INSTALL_PREFIX $CP_CMAKE_OPTIONS
    ninja
  else
    cmake .. -DCMAKE_BUILD_TYPE=relwithdebinfo -DBWAPI_DIR=$BWAPI_INSTALL_PREFIX $CP_CMAKE_OPTIONS
    make -j$MAX_JOBS
  fi
  popd
fi

mkdir -p bwapi-data/read bwapi-data/write

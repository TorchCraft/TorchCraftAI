#!/usr/bin/env bash
OPENBW_DIR='openbw'
if [ ! -d "$OPENBW_DIR" ]; then
  echo "Cloning and patching openbw..."
  git clone git@github.com:OpenBW/openbw.git $OPENBW_DIR
  (cd $OPENBW_DIR && git checkout 4091cad0823eedef60283f1a9dc1fb55cc1793d7 && git apply --whitespace=nowarn ../openbw_patch.diff)
fi
rm $OPENBW_DIR/ui/test.*
BWAPI_DIR='../../3rdparty/openbw/bwapi'
BUILD_ARGS="-std=c++14 -I $OPENBW_DIR -I $BWAPI_DIR/bwapi/BWAPI/Source/BW/ -I $BWAPI_DIR/bwapi/Util/Source/ -I include $OPENBW_DIR/ui/dlmalloc.c"
BUILD_ARGS="$BUILD_ARGS $OPENBW_DIR/ui/unit_matcher.cpp $OPENBW_DIR/ui/sdl2.cpp $OPENBW_DIR/ui/gfxtest.cpp $BWAPI_DIR/bwapi/BWAPI/Source/BW/Bitmap.cpp"
BUILD_ARGS="$BUILD_ARGS -ferror-limit=4 -O3 --memory-init-file 0 -s ASM_JS=1 -s USE_SDL=2 -o test.html -s TOTAL_MEMORY=201326592"
BUILD_ARGS="$BUILD_ARGS -s INVOKE_RUN=0 --bind  -s DISABLE_EXCEPTION_CATCHING=1 -D OPENBW_NO_SDL_IMAGE -D USE_DL_PREFIX "
BUILD_ARGS="$BUILD_ARGS -s ABORTING_MALLOC=0 -DMSPACES -DFOOTERS -DOPENBW_NO_SDL_MIXER=1 -s ASSERTIONS=1 "
# Debug flags
#BUILD_ARGS="$BUILD_ARGS -O2 -g4 -s ASSERTIONS=2 -s DEMANGLE_SUPPORT=1 -s SAFE_HEAP=1 -s DISABLE_EXCEPTION_CATCHING=0"
em++ $BUILD_ARGS -s EXPORTED_FUNCTIONS="['_main','_ui_resize','_replay_get_value','_replay_set_value','_player_get_value','_load_replay', '_js_add_screen_overlay']"
mv test.* replays/static/replays/viewer/
du -h replays/static/replays/viewer/test*

#!/bin/bash
# TODO: replace this by a Makefile
# NOTES:
# - using -sALLOW_MEMORY_GROWTH now since there have been asyncs that blew the default limit of 16MB

source build.cfg

LIBS="$LIBS -lidbfs.js -DUSE_IDBFS"
BUILD_DIR="build/$NAME"
DEFINES="$DEFINES -DAP_NO_SCHEMA -DAPCLIENT_DEBUG -DUSB2SNES_DEBUG"
# NOTE: in WASM we don't want schema for size reasons, but we want debug output because it's easy to hide
EXTRA="$EXTRA -flto"

# clean up
rm -Rf --one-file-system "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "Building ..."

# debug build
if [[ "$1" == "debug" ]] || [[ "$1" == "stat" ]]; then
  em++ --bind $SRC "src/games/$GAME_C" $INCLUDE_DIRS -DGAME_H="\"games/$GAME_H\"" $DEFINES $LIBS --shell-file ui/shell.html -o "$BUILD_DIR/$NAME.html" -fexceptions -Og -sASSERTIONS -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=1024 $EXTRA || exit 1
fi

# release build
if [[ "$1" != "debug" ]]; then
  em++ --bind $SRC "src/games/$GAME_C" $INCLUDE_DIRS -DGAME_H="\"games/$GAME_H\"" $DEFINES $LIBS --shell-file ui/shell.html -o "$BUILD_DIR/$NAME.min.html" -fexceptions -Oz -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=1024MB $EXTRA || exit 1
  # pre-compress to be served through .htaccess overrides
  echo "Pre-compressing files ..."
  brotli -k -q 11 "$BUILD_DIR/$NAME.min.wasm" "$BUILD_DIR/$NAME.min.js" "$BUILD_DIR/$NAME.min.html"
  gzip -k -9 "$BUILD_DIR/$NAME.min.wasm" "$BUILD_DIR/$NAME.min.js" "$BUILD_DIR/$NAME.min.html"
fi

# show size comparison
if [[ "$1" == "stat" ]]; then
  sleep .1
  du -bh "$BUILD_DIR/$NAME.html" "$BUILD_DIR/$NAME.js" "$BUILD_DIR/$NAME.wasm"
  du -bh "$BUILD_DIR/$NAME.min.html" "$BUILD_DIR/$NAME.min.js" "$BUILD_DIR/$NAME.min.wasm"
  du -bh test.min.html.gz test.min.js.gz test.min.wasm.gz
  du -bh test.min.html.br test.min.js.br test.min.wasm.br
fi

# remove debug build if we did not specify debug, copy, rename and package everything
if [[ "$1" != "debug" ]]; then
  echo "Packaging files ..."
  rm -f "$BUILD_DIR/$NAME.html" "$BUILD_DIR/$NAME.js" "build/$NAME.wasm"
  mv "$BUILD_DIR/$NAME.min.html" "$BUILD_DIR/index.html" # this may change with version selection in the future
  cp "util/htaccess" "$BUILD_DIR/.htaccess" # copy .htaccess that automagically serves pre-compressed wasm, js and css
  cp "util/serve.py" "$BUILD_DIR/" # copy serve.py to build output
  # copy licenses & readme
  cp "LICENSE" "$BUILD_DIR/"
  cp "README.md" "$BUILD_DIR/"
  echo -e "# ap-soeclient\n" >> "$BUILD_DIR/CREDITS"
  echo "see LICENSE file" >> "$BUILD_DIR/CREDITS"
  echo -e "\n" >> "$BUILD_DIR/CREDITS"
  echo -e "# nlohman::json library\n" >> "$BUILD_DIR/CREDITS"
  cat "subprojects/json/LICENSE.MIT" >> "$BUILD_DIR/CREDITS"
  echo -e "\n" >> "$BUILD_DIR/CREDITS"
  echo -e "# valijson libaray\n" >> "$BUILD_DIR/CREDITS"
  cat "subprojects/valijson/LICENSE" >> "$BUILD_DIR/CREDITS" 
  
  OLD_CWD=`pwd`
  # remove unused files
  cd "$BUILD_DIR"
  rm *.min.html.* # remove precompressed html, let the server handle that
  # package zip
  cd ..
  7z -mx=9 a "${NAME}_wasm.zip" soeclient
  if [ -x `which advzip` ]; then
    advzip -z -4 "${NAME}_wasm.zip"
  fi
  # done
  cd $OLD_CWD
fi

#!/bin/bash
# TODO: replace this by a Makefile

source build.cfg

INCLUDE_DIRS="$INCLUDE_DIRS -Isubprojects/asio/include -Isubprojects/websocketpp"
DEFINES="$DEFINES -DASIO_STANDALONE -DWSWRAP_SEND_EXCEPTIONS" # not using boost
LIBS="-static-libstdc++ -static-libgcc $LIBS -lssl -lcrypto -lcrypt32 -lz -Wno-deprecated-declarations -lws2_32 -lssp -static -pthread"
BUILD_DIR="build/win64"
CPP="x86_64-w64-mingw32-g++"
STRIP="x86_64-w64-mingw32-strip"

# clean up
rm -Rf --one-file-system "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "Building ..."
if [[ "$1" == "debug" ]]; then
  # debug build
  DEFINES="$DEFINES -DAPCLIENT_DEBUG -DUSB2SNES_DEBUG"
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME.exe" -g -fexceptions $EXTRA || exit 1
else
  # release build
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME.exe" -fexceptions -Os -s $EXTRA || exit 1  # -flto
  $STRIP "$BUILD_DIR/$NAME.exe"
  echo "Copying other files ..."
  cp LICENSE "$BUILD_DIR/"
  if [ -f "cacert.pem" ]; then
    cp "cacert.pem" "$BUILD_DIR/"
  fi
  OLD_CWD=`pwd`
  # remove unused files
  cd "$BUILD_DIR"
  # package zip
  echo "Packaging files ..."
  7z -mx=9 a "../${NAME}_win64.zip" *
  if [ -x `which advzip` ]; then
    advzip -z -4 "../${NAME}_win64.zip"
  fi
  # done
  cd $OLD_CWD
fi

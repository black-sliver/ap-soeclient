#!/bin/bash
# TODO: replace this by a Makefile

source build.cfg

INCLUDE_DIRS="$INCLUDE_DIRS -Isubprojects/asio/include -Isubprojects/websocketpp"
DEFINES="$DEFINES -DASIO_STANDALONE" # not using boost
LIBS="-static-libstdc++ -static-libgcc $LIBS -lws2_32 -static -pthread"
BUILD_DIR="build/win64"
CPP="x86_64-w64-mingw32-g++"

# clean up
rm -Rf --one-file-system "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

if [[ "$1" == "debug" ]]; then
  # debug build
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME.exe" -g -fexceptions
else
  # release build
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME.exe" -fexceptions -Os # -flto
  cp LICENSE "$BUILD_DIR/"
  OLD_CWD=`pwd`
  # remove unused files
  cd "$BUILD_DIR"
  # package zip
  7z -mx=9 a "../${NAME}_win64.zip" *
  if [ -x `which advzip` ]; then
    advzip -z -4 "../${NAME}_win64.zip"
  fi
  # done
  cd $OLD_CWD
fi


#!/bin/bash
# TODO: replace this by a Makefile

source build.cfg

INCLUDE_DIRS="$INCLUDE_DIRS -Isubprojects/asio/include -Isubprojects/websocketpp"
DEFINES="$DEFINES -DASIO_STANDALONE -DWSWRAP_SEND_EXCEPTIONS" # not using boost
LIBS="$LIBS -pthread -lssl -lcrypto -lz -Wno-deprecated-declarations"
BUILD_DIR="build/native"
CPP="g++"

# clean up
rm -Rf --one-file-system "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# detect OS
if [ -z "$OS_NAME" ]; then
    case $(uname | tr '[:upper:]' '[:lower:]') in
        linux*)
            OS_NAME=linux
            ;;
        darwin*)
            OS_NAME=macos
            ;;
        msys*)
            OS_NAME=windows
            ;;
        cygwin*)
            OS_NAME=windows
            ;;
        mingw*)
            OS_NAME=windows
            ;;
        *)
            OS_NAME=other
    esac
fi

if [[ "$1" == "static" ]]; then
  LIBS="-static -Wl,-Bstatic $LIBS -static-libgcc -static-libstdc++"
fi

if [[ "$OS_NAME" == "windows" ]]; then
  LIBS="$LIBS -lcrypt32 -lws2_32"
fi

echo "Building ..."
if [[ "$1" == "debug" ]]; then
  # debug build
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME" -g -fexceptions || exit 1
else
  # release build
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME" -fexceptions -Os -flto || exit 1
  echo "Copying other files ..."
  cp LICENSE "$BUILD_DIR/"
  if [ -f "cacert.pem" ]; then
    cp "cacert.pem" "$BUILD_DIR/"
  fi
fi

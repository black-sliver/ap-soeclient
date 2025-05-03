#!/bin/bash
# TODO: replace this by a Makefile

source build.cfg

INCLUDE_DIRS="$INCLUDE_DIRS -Isubprojects/asio/include -Isubprojects/websocketpp"
DEFINES="$DEFINES -DASIO_STANDALONE -DWSWRAP_SEND_EXCEPTIONS" # not using boost
LIBS="$LIBS -pthread -lssl -lcrypto -lz -Wno-deprecated-declarations"
BUILD_DIR="build/native"
CPP="g++"
STRIP="strip"

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

ARCH=$(uname -m)

if [[ "$1" == "static" ]]; then
  LIBS="-static -Wl,-Bstatic $LIBS -static-libgcc -static-libstdc++"
fi

if [[ "$OS_NAME" == "windows" ]]; then
  LIBS="$LIBS -lcrypt32 -lws2_32"
else
  EXTRA="$EXTRA -flto"
fi

echo "Building ..."
if [[ "$1" == "debug" ]]; then
  # debug build
  DEFINES="$DEFINES -DAPCLIENT_DEBUG -DUSB2SNES_DEBUG"
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME" -g -fexceptions $EXTRA || exit 1
else
  # release build
  $CPP $SRC "src/games/$GAME_C" $INCLUDE_DIRS $DEFINES -DGAME_H="\"games/$GAME_H\"" $LIBS -o "$BUILD_DIR/$NAME" -fexceptions -Os $EXTRA || exit 1
  $STRIP $BUILD_DIR/$NAME*
  echo "Copying other files ..."
  cp LICENSE "$BUILD_DIR/"
  if [ -f "cacert.pem" ]; then
    cp "cacert.pem" "$BUILD_DIR/"
  fi

  OLD_CWD=`pwd`
  cd "$BUILD_DIR"
  echo "Packaging files ..."
  if [[ "$OS_NAME" == "windows" ]]; then
    # package zip
    7z -mx=9 a "../${NAME}-${OS_NAME}-${ARCH}.zip" *
    if [ -x `which advzip` ]; then
      advzip -z -4 "../${NAME}-${OS_NAME}-${ARCH}.zip"
    fi
  else
    tar -cJvf "../${NAME}-${OS_NAME}-${ARCH}.tar.xz" *
  fi
  # done
  cd $OLD_CWD
fi

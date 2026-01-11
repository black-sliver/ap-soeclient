ifeq ($(GAME),)
  $(error GAME must not be empty)
endif

# Common configuration

NAME = $(GAME)client
CONF ?= RELEASE  # make CONF=DEBUG for debug, CONF=DIST for .zip
BUILD_DIR ?= build

# Source files

INCLUDE_DIRS = \
  -I subprojects/asio/include \
  -I subprojects/json/include \
  -I subprojects/valijson/include \
  -I subprojects/wswrap/include \
  -I subprojects/websocketpp \
  -I subprojects/apclientpp

SRC = \
  $(wildcard src/*.cpp) \
  $(wildcard src/games/*.cpp) \
  $(wildcard src/games/$(GAME)/*.cpp)

HDR = \
  $(wildcard src/*.hpp) \
  $(wildcard src/games/*.hpp) \
  $(wildcard src/games/$(GAME)/*.hpp)

GAME_H = games/$(GAME)/$(GAME).hpp

EXTRA_FILES = LICENSE README.md

# Tool configuration

DEFINES := $(DEFINES) -DGAME_H="\"$(GAME_H)\""
CFLAGS := $(CFLAGS) -ffunction-sections -fdata-sections -Werror -Wall -Wextra -Wconversion
LDFLAGS:= $(LDFLAGS) -Wl,--gc-sections
# TODO: -Wl,-dead_strip instead of -Wl,--gc-sections on macos

ifeq ($(CONF),DEBUG)
  CFLAGS := $(CFLAGS) -Og -g
  DEFINES := $(DEFINES) -DAPCLIENT_DEBUG -DUSB2SNES_DEBUG
else
  CFLAGS := $(CFLAGS) -Os -flto
  LDFLAGS := $(LDFLAGS) -s
  DEFINES := $(DEFINES) -DAP_NO_SCHEMA -DNDEBUG
  ifeq ($(OS),Windows_NT)
    # the following optimization (default for -Os) crashes with LTO on Windows
    CFLAGS := $(CFLAGS) -fno-declone-ctor-dtor
    LDFLAGS := $(LDFLAGS) -fno-declone-ctor-dtor
  endif
endif

CFLAGS := $(CFLAGS) $(INCLUDE_DIRS)

DESKTOP_DEFINES = $(DEFINES) -DASIO_STANDALONE
DESKTOP_LIBS = -lz -lssl -lcrypto

ifeq ($(OS),Windows_NT)
  DESKTOP_LIBS := -static-libstdc++ -static-libgcc -static $(DESKTOP_LIBS) -lcrypt32 -lws2_32 -lssp -pthread
endif

# TODO: no -fstack-clash-protection on macos
DESKTOP_CFLAGS = $(CFLAGS) $(DESKTOP_DEFINES) -fno-omit-frame-pointer -fstack-protector-strong -fstack-clash-protection
DESKTOP_LDFLAGS = $(LDFLAGS) $(DESKTOP_LIBS)

NATIVE_CFLAGS = $(DESKTOP_CFLAGS)
NATIVE_LDFLAGS = $(DESKTOP_LDFLAGS)

WASM_DEFINES = $(DEFINES) -DUSE_IDBFS -DAPCLIENT_DEBUG -DUSB2SNES_DEBUG
WASM_LIBS = -lidbfs.js

WASM_CFLAGS = $(CFLAGS) $(WASM_DEFINES) -fexceptions
WASM_LDFLAGS = $(LDFLAGS) $(WASM_LIBS) -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=1024MB

ifeq ($(CONF),DEBUG)
  WASM_LDFLAGS := $(WASM_LDFLAGS) -sASSERTIONS
else
  WASM_CFLAGS := $(WASM_CFLAGS) -Oz
  WASM_LDFLAGS := $(WASM_LDFLAGS) -Oz
endif

ifeq ($(OS),Windows_NT)
  EXE_EXT := .exe
else
  EXE_EXT :=
endif

# Native build

NATIVE_BUILD_DIR = $(BUILD_DIR)/native/$(NAME)
NATIVE_EXE = $(NATIVE_BUILD_DIR)/$(NAME)$(EXE_EXT)
NATIVE_CERTS = $(NATIVE_BUILD_DIR)/cacert.pem

ifeq ($(OS),Windows_NT)
  NATIVE_OS_NAME = "win"
  NATIVE_OS_ARCH =$(shell uname -m | tr A-Z a-z)
else
  NATIVE_OS_NAME = $(shell uname -s | tr A-Z a-z)
  NATIVE_OS_ARCH =$(shell uname -m | tr A-Z a-z)
endif
NATIVE_ZIP = $(BUILD_DIR)/$(NAME)_$(NATIVE_OS_NAME)-$(NATIVE_OS_ARCH).zip
NATIVE_TAR_XZ = $(BUILD_DIR)/$(NAME)_$(NATIVE_OS_NAME)-$(NATIVE_OS_ARCH).tar.xz

$(NATIVE_BUILD_DIR): $(EXTRA_FILES)
	mkdir -p $@
	cp -a $^ $@

$(NATIVE_CERTS): cacert.pem | $(NATIVE_BUILD_DIR)
	cp -a $< $@

$(NATIVE_EXE): $(SRC) $(HDR) $(NATIVE_CERTS) | $(NATIVE_BUILD_DIR)
	$(CXX) -o "$@" $(SRC) $(NATIVE_CFLAGS) $(NATIVE_LDFLAGS)

$(NATIVE_ZIP): $(NATIVE_EXE) $(NATIVE_BUILD_DIR)
	cd "$(BUILD_DIR)/native" && 7z -mx=9 a "../../$@" $(NAME)
	if [ -x `which advzip` ]; then advzip -z -4 "$@"; fi

$(NATIVE_TAR_XZ): $(NATIVE_EXE) $(NATIVE_BUILD_DIR)
	cd "$(BUILD_DIR)/native" && tar -cJvf "../../$@" $(NAME)

ifeq ($(CONF)-$(OS),DIST-Windows_NT)
native: $(NATIVE_ZIP)
else ifeq ($(CONF),DIST)
native: $(NATIVE_TAR_XZ)
else
native: $(NATIVE_EXE) $(NATIVE_BUILD_DIR)
endif

ALL := $(ALL) native

# WASM build

WASM_BUILD_DIR = $(BUILD_DIR)/wasm/$(NAME)
ifeq ($(CONF),DEBUG)
  WASM_TEMP_HTML = $(WASM_BUILD_DIR)/$(NAME).html
  WASM_JS = $(WASM_BUILD_DIR)/$(NAME).js
  WASM_JS_GZ = $(WASM_BUILD_DIR)/$(NAME).js.gz
  WASM_JS_BR = $(WASM_BUILD_DIR)/$(NAME).js.br
  WASM_WASM = $(WASM_BUILD_DIR)/$(NAME).wasm
  WASM_WASM_GZ = $(WASM_BUILD_DIR)/$(NAME).wasm.gz
  WASM_WASM_BR = $(WASM_BUILD_DIR)/$(NAME).wasm.br
else
  WASM_TEMP_HTML = $(WASM_BUILD_DIR)/$(NAME).min.html
  WASM_JS = $(WASM_BUILD_DIR)/$(NAME).min.js
  WASM_JS_GZ = $(WASM_BUILD_DIR)/$(NAME).min.js.gz
  WASM_JS_BR = $(WASM_BUILD_DIR)/$(NAME).min.js.br
  WASM_WASM = $(WASM_BUILD_DIR)/$(NAME).min.wasm
  WASM_WASM_GZ = $(WASM_BUILD_DIR)/$(NAME).min.wasm.gz
  WASM_WASM_BR = $(WASM_BUILD_DIR)/$(NAME).min.wasm.br
endif
WASM_HTML = $(WASM_BUILD_DIR)/index.html
WASM_CREDITS = $(WASM_BUILD_DIR)/CREDITS
WASM_HTACCESS = $(WASM_BUILD_DIR)/.htaccess
WASM_ZIP = $(BUILD_DIR)/$(NAME)_wasm.zip

$(WASM_BUILD_DIR): util/serve.py $(EXTRA_FILES)
	mkdir -p $@
	cp -a $^ $@

$(WASM_HTACCESS): util/htaccess | $(WASM_BUILD_DIR)
	cp $< $@

$(WASM_CREDITS): subprojects/json/LICENSE.MIT subprojects/valijson/LICENSE | $(WASM_BUILD_DIR)
	echo -e "# ap-soeclient\n" >> "$@"
	echo "see LICENSE file" >> "$@"
	echo -e "\n" >> "$@"
	echo -e "# nlohman::json library\n" >> "$@"
	cat "subprojects/json/LICENSE.MIT" >> "$@"
	echo -e "\n" >> "$@"
	echo -e "# valijson libaray\n" >> "$@"
	cat "subprojects/valijson/LICENSE" >> "$@"

$(WASM_HTML): $(SRC) $(HDR) ui/shell.html | $(WASM_BUILD_DIR)
	em++ --bind $(SRC) --shell-file ui/shell.html -o "$(WASM_TEMP_HTML)" $(WASM_CFLAGS) $(WASM_LDFLAGS)
	mv "$(WASM_TEMP_HTML)" "$@"
	# TODO: brotli and gz

$(WASM_JS) $(WASM_WASM): $(WASM_HTML)  # js and wasm are side products

$(WASM_JS_GZ): $(WASM_JS)
	gzip -k -f -9 $<

$(WASM_WASM_GZ): $(WASM_WASM)
	gzip -k -f -9 $<

$(WASM_JS_BR): $(WASM_JS)
	brotli -k -f -q 11 $<

$(WASM_WASM_BR): $(WASM_WASM)
	brotli -k -f -q 11 $<

wasm_gz_files: $(WASM_JS_GZ) $(WASM_WASM_GZ)

wasm_br_files: $(WASM_JS_BR) $(WASM_WASM_BR)

wasm_files: $(WASM_HTML) wasm_gz_files wasm_br_files $(WASM_CREDITS) $(WASM_HTACCESS) $(WASM_BUILD_DIR)

$(WASM_ZIP): wasm_files | $(BUILD_DIR)
	cd "$(BUILD_DIR)/wasm" && 7z -mx=9 a "../$(NAME)_wasm.zip" "$(NAME)"
	if [ -x `which advzip` ]; then advzip -z -4 "$@"; fi

ifeq ($(CONF),DIST)
wasm: $(WASM_ZIP)
else
wasm: wasm_files
endif

ALL := $(ALL) wasm

# Cross Windows build(s)

ifneq ($(OS),Windows_NT)
ALL := $(ALL) cross
cross:
	echo "TODO: cross compile for win64"
endif

# Standard targets

$(BUILD_DIR):
	mkdir -p $@

clean:
	-rm -f "$(NATIVE_EXE)" "$(NATIVE_CERTS)"
	-$(foreach extra_file,$(EXTRA_FILES),rm -f "$(NATIVE_BUILD_DIR)/$(extra_file)" ;)
	-rmdir "$(NATIVE_BUILD_DIR)"
	-rmdir "$(BUILD_DIR)/native"
	-rm -rf --one-file-system "$(WASM_BUILD_DIR)"
	-rm -f "$(WASM_ZIP)"
	-rmdir "$(BUILD_DIR)/wasm"
	-rmdir "$(BUILD_DIR)"

all: $(ALL)

.PHONY: clean $(ALL) wasm_files wasm_gz_files wasm_br_files

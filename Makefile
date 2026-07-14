HEAP_SIZE  = 8388208
STACK_SIZE = 61800

PRODUCT = lcd-game-emulator.pdx
CC_FOR_BUILD ?= cc

SDK = ${PLAYDATE_SDK_PATH}
ifeq ($(SDK),)
	SDK = $(shell egrep '^\s*SDKRoot' ~/.Playdate/config | head -n 1 | cut -c9-)
endif

ifeq ($(SDK),)
$(error SDK path not found; set PLAYDATE_SDK_PATH or configure SDKRoot in ~/.Playdate/config)
endif

VPATH += src/platform/playdate src/frontend src/gw src/cpus

# Milestone 1 deliberately links only the Playdate lifecycle bootstrap. The
# emulator core is added after its host assumptions have been isolated.
SRC = playdate_main.c gw_renderer_playdate.c gw_audio_playdate.c rom_picker_unit.c gw_game_db.c gw_package.c gw_machine.c \
      sm510base.c sm510core.c sm510op.c sm500core.c sm500op.c sm5acore.c \
      sm511core.c

UINCDIR = vendor/pd-rom-picker/src src/gw src/gw_sys src/cpus
UASRC =
UDEFS =
UADEFS =
ULIBDIR =
ULIBS =

ifeq ($(DEBUG_METRICS),1)
UDEFS += -DGW_DEVELOPMENT_METRICS=1
endif

include $(SDK)/C_API/buildsupport/common.mk

OPT = -O2 -fomit-frame-pointer

# This repository has no supported Simulator product. Both the default target
# and the deployment prerequisite build the ARM device binary only.
.DEFAULT_GOAL := device

PLAYDATE_GAMES ?= /Volumes/PLAYDATE/Games

.PHONY: install _push test

test:
	mkdir -p build
	$(CC_FOR_BUILD) -std=c99 -Wall -Wextra -Werror -Isrc/gw \
		tests/test_gw_package.c src/gw/gw_package.c src/gw/gw_game_db.c \
		-o build/test_gw_package
	build/test_gw_package

install: device
	@test -d "$(PLAYDATE_GAMES)" || (echo "Playdate volume not mounted at $(PLAYDATE_GAMES)" && exit 1)
	$(RM) -rf "$(PLAYDATE_GAMES)/$(PRODUCT)"
	COPYFILE_DISABLE=1 cp -R "$(PRODUCT)" "$(PLAYDATE_GAMES)/"
	-dot_clean -m "$(PLAYDATE_GAMES)/$(PRODUCT)"

_push: install

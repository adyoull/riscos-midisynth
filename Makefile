# riscos-midisynth
#
#   make                 host build (Linux/macOS): libmidisynth.a + midi2wav
#   make riscos          cross build with GCCSDK: libmidisynth.a + midiplay,ff8
#   make install         copy the RISC OS library and header into the
#                        GCCSDK environment ($(GCCSDK_INSTALL_ENV))
#   make app             build !MIDISynth (needs a SoundFont, see README)
#   make zip             zip !MIDISynth with RISC OS filetypes
#   make test            host tests (also: test-asan, test-tsan, test-update)
#
# How to make a release: docs/RELEASING.md.
#
# Cross build settings; override on the command line if yours differ.
GCCSDK_INSTALL_ENV ?= $(HOME)/gccsdk/env
CROSS  ?= $(GCCSDK_INSTALL_ENV)/bin/arm-riscos-gnueabihf-
ELF2AIF ?= elf2aif
SOUNDFONT ?= TimGM6mb.sf2
# Stack probes: RISC OS GCC programs grow the stack a page at a time.
# -ffast-math lets GCC reorder the float maths (output differs by at most
# 1 in 32768). A NEON build was tried: GCC finds almost nothing in
# TinySoundFont's voice loop to vectorise, so there's one library for all.
RO_CFLAGS ?= -O3 -ffast-math -mtune=cortex-a72 -fstack-clash-protection
PYTHON ?= python3
# The version comes from include/midisynth.h, so there's one place to change.
VERSION := $(shell sed -n 's/^\#define MIDISYNTH_VERSION  *"\(.*\)"/\1/p' include/midisynth.h)

CC      ?= cc
CFLAGS  ?= -O2
WARN     = -Wall
INC      = -Iinclude -Ithird_party/TinySoundFont
# midisynth.c compiles these in, so it must be rebuilt when they change
LIB_DEPS = src/midisynth.c include/midisynth.h \
           third_party/TinySoundFont/tsf.h third_party/TinySoundFont/tml.h \
           third_party/stb/stb_vorbis.c

HOST_DIR = build/host
RO_DIR   = build/riscos

.PHONY: all host riscos install app zip test test-asan test-tsan test-update clean

all: host

# ---- host ---------------------------------------------------------------
host: $(HOST_DIR)/libmidisynth.a $(HOST_DIR)/midi2wav

$(HOST_DIR)/midisynth.o: $(LIB_DEPS)
	@mkdir -p $(HOST_DIR)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c $< -o $@

$(HOST_DIR)/libmidisynth.a: $(HOST_DIR)/midisynth.o
	ar rcs $@ $^

$(HOST_DIR)/midi2wav: examples/midi2wav.c $(HOST_DIR)/libmidisynth.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude $< -L$(HOST_DIR) -lmidisynth -lpthread -lm -o $@

# ---- RISC OS ------------------------------------------------------------
riscos: $(RO_DIR)/libmidisynth.a $(RO_DIR)/midiplay,ff8

$(RO_DIR)/midisynth.o: $(LIB_DEPS)
	@mkdir -p $(RO_DIR)
	$(CROSS)gcc $(RO_CFLAGS) $(WARN) $(INC) -c $< -o $@

$(RO_DIR)/libmidisynth.a: $(RO_DIR)/midisynth.o
	$(CROSS)ar rcs $@ $^

$(RO_DIR)/midiplay: examples/midiplay.c $(RO_DIR)/libmidisynth.a
	$(CROSS)gcc $(RO_CFLAGS) $(WARN) -Iinclude -static $< -L$(RO_DIR) -lmidisynth -lpthread -lm -o $@

$(RO_DIR)/midiplay,ff8: $(RO_DIR)/midiplay
	$(ELF2AIF) -e $< $@

install: $(RO_DIR)/libmidisynth.a
	mkdir -p $(GCCSDK_INSTALL_ENV)/include $(GCCSDK_INSTALL_ENV)/lib
	cp include/midisynth.h $(GCCSDK_INSTALL_ENV)/include/
	cp $(RO_DIR)/libmidisynth.a $(GCCSDK_INSTALL_ENV)/lib/

# ---- !MIDISynth resource application -------------------------------------
app: $(RO_DIR)/midiplay,ff8
	rm -rf build/!MIDISynth
	cp -r app/!MIDISynth build/
	cp $(RO_DIR)/midiplay,ff8 build/!MIDISynth/midiplay,ff8
	@if [ -f "$(SOUNDFONT)" ]; then \
	  cp "$(SOUNDFONT)" build/!MIDISynth/SoundFont,ffd; \
	else \
	  echo "note: no SoundFont at $(SOUNDFONT); put one in !MIDISynth as SoundFont"; \
	fi

# tools/mkrozip.py stores the RISC OS filetypes from the ,xxx suffixes
zip: app
	rm -f build/MIDISynth-$(VERSION).zip
	$(PYTHON) tools/mkrozip.py build/MIDISynth-$(VERSION).zip build '!MIDISynth'

# ---- tests (host) -------------------------------------------------------
TEST_DIR = build/test
ASAN_FLAGS = -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
$(TEST_DIR)/files: tests/mktestfiles.py
	$(PYTHON) tests/mktestfiles.py $@
	@touch $@

define test_build
	@mkdir -p $(TEST_DIR)
	$(CC) -g $(1) $(WARN) $(INC) src/midisynth.c tests/test_midisynth.c -lpthread -lm -o $(2)
endef

$(TEST_DIR)/test_midisynth: $(LIB_DEPS) tests/test_midisynth.c
	$(call test_build,$(CFLAGS),$@)

test: $(TEST_DIR)/test_midisynth $(TEST_DIR)/files
	$(TEST_DIR)/test_midisynth $(TEST_DIR)/files

test-asan: $(TEST_DIR)/files
	$(call test_build,$(ASAN_FLAGS),$(TEST_DIR)/test_asan)
	$(TEST_DIR)/test_asan $(TEST_DIR)/files

test-tsan: $(TEST_DIR)/files
	$(call test_build,-O1 -fsanitize=thread,$(TEST_DIR)/test_tsan)
	$(TEST_DIR)/test_tsan $(TEST_DIR)/files

# Only after checking that a change in the sound is intended
test-update: $(TEST_DIR)/test_midisynth $(TEST_DIR)/files
	$(TEST_DIR)/test_midisynth $(TEST_DIR)/files --update


clean:
	rm -rf build

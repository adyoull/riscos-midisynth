# riscos-midisynth
#
#   make                 host build (Linux/macOS): libmidisynth.a + midi2wav
#   make riscos          cross build with GCCSDK: libmidisynth.a + midiplay,ff8
#   make install         copy the RISC OS library and header into the
#                        GCCSDK environment ($(GCCSDK_INSTALL_ENV))
#   make app             build !MIDISynth (needs a SoundFont, see README)
#   make zip             zip !MIDISynth with RISC OS filetypes
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
ZIP ?= $(GCCSDK_INSTALL_ENV)/bin/zip
VERSION = 0.2.0

CC      ?= cc
CFLAGS  ?= -O2
WARN     = -Wall
INC      = -Iinclude -Ithird_party/TinySoundFont

HOST_DIR = build/host
RO_DIR   = build/riscos

.PHONY: all host riscos install app zip clean

all: host

# ---- host ---------------------------------------------------------------
host: $(HOST_DIR)/libmidisynth.a $(HOST_DIR)/midi2wav

$(HOST_DIR)/midisynth.o: src/midisynth.c include/midisynth.h
	@mkdir -p $(HOST_DIR)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c $< -o $@

$(HOST_DIR)/libmidisynth.a: $(HOST_DIR)/midisynth.o
	ar rcs $@ $^

$(HOST_DIR)/midi2wav: examples/midi2wav.c $(HOST_DIR)/libmidisynth.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude $< -L$(HOST_DIR) -lmidisynth -lpthread -lm -o $@

# ---- RISC OS ------------------------------------------------------------
riscos: $(RO_DIR)/libmidisynth.a $(RO_DIR)/midiplay,ff8

$(RO_DIR)/midisynth.o: src/midisynth.c include/midisynth.h
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

# GCCSDK's zip; -, stores the RISC OS filetypes from the ,xxx suffixes
zip: app
	rm -f build/MIDISynth-$(VERSION).zip
	cd build && $(ZIP) -, -9 -r MIDISynth-$(VERSION).zip '!MIDISynth'

clean:
	rm -rf build

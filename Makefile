# riscos-softmidi (libmidisynth and !SoftMIDI)
#
#   make                 host build (Linux/macOS): libmidisynth.a + midi2wav
#   make riscos          cross build with GCCSDK: libmidisynth.a + midiplay,ff8
#   make install         copy the RISC OS library and header into the
#                        GCCSDK environment ($(GCCSDK_INSTALL_ENV))
#   make app             build !SoftMIDI (needs a SoundFont, see README)
#   make zip             zip !SoftMIDI with RISC OS filetypes
#   make test            host tests (also: test-asan, test-tsan, test-update)
#   make riscos-tests    a zip of tests to run on RISC OS (see tests/riscos)
#   make dist            the RISC OS library, header and licences as a tarball
#   make soundfont       fetch TimGM6mb.sf2 (Ubuntu's package, checked by sha256)
#   make check-riscos    riscos-crossdev's checks on midiplay (needs RCD_DIR)
#   make ci              what GitHub runs: all of the above, then the zip
#
# How to make a release: docs/RELEASING.md.
#
# Cross build settings; override on the command line if yours differ.
# In the riscos-crossdev image (and with its env.sh) RISCOS_TOOLCHAIN names
# the toolchain: GCCSDK GCC 10.2 with riscos-unixlib, elf2aif on PATH.
ifneq ($(RISCOS_TOOLCHAIN),)
GCCSDK_INSTALL_ENV ?= $(RISCOS_TOOLCHAIN)
else
GCCSDK_INSTALL_ENV ?= $(HOME)/gccsdk/env
endif
CROSS  ?= $(GCCSDK_INSTALL_ENV)/bin/arm-riscos-gnueabihf-
ELF2AIF ?= elf2aif
SOUNDFONT ?= TimGM6mb.sf2
# Stack probes: RISC OS GCC programs grow the stack a page at a time.
# -ffast-math lets GCC reorder the float maths (output differs by at most
# 1 in 32768). A NEON build was tried: GCC finds almost nothing in
# TinySoundFont's voice loop to vectorise, so there's one library for all.
RO_CFLAGS ?= -O3 -ffast-math -mtune=cortex-a72 -fstack-clash-protection
# UnixLib for midiplay: the folder holding the libunixlib.a to link with,
# e.g. a riscos-unixlib release. Empty means the one in GCCSDK_INSTALL_ENV.
UNIXLIB ?=
PYTHON ?= python3
# The version comes from include/midisynth.h, so there's one place to change.
VERSION := $(shell sed -n 's/^\#define MIDISYNTH_VERSION  *"\(.*\)"/\1/p' include/midisynth.h)

CC      ?= cc
CFLAGS  ?= -O2
WARN     = -Wall
INC      = -Iinclude -Ithird_party/TinySoundFont

# The library's source files (see src/midisynth_internal.h for what each does)
LIB_SRCS = src/midisynth.c src/output.c src/output_ssb.c src/output_dr.c
LIB_HDRS = include/midisynth.h src/midisynth_internal.h src/output.h src/riscos_swi.h
# midisynth.c compiles these in, so it must be rebuilt when they change
THIRD_PARTY = third_party/TinySoundFont/tsf.h third_party/TinySoundFont/tml.h \
              third_party/stb/stb_vorbis.c

HOST_DIR = build/host
RO_DIR   = build/riscos
HOST_OBJS = $(LIB_SRCS:src/%.c=$(HOST_DIR)/%.o)
RO_OBJS   = $(LIB_SRCS:src/%.c=$(RO_DIR)/%.o)

.PHONY: all host riscos install app zip dist soundfont check-riscos ci riscos-tests test test-asan test-tsan test-update clean

all: host

# ---- host ---------------------------------------------------------------
# (no sound output here: midisynth_output_open returns 0)
host: $(HOST_DIR)/libmidisynth.a $(HOST_DIR)/midi2wav

$(HOST_DIR)/%.o: src/%.c $(LIB_HDRS) $(THIRD_PARTY)
	@mkdir -p $(HOST_DIR)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c $< -o $@

$(HOST_DIR)/libmidisynth.a: $(HOST_OBJS)
	ar rcs $@ $^

$(HOST_DIR)/midi2wav: examples/midi2wav.c $(HOST_DIR)/libmidisynth.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude $< -L$(HOST_DIR) -lmidisynth -lpthread -lm -o $@

# ---- RISC OS ------------------------------------------------------------
riscos: $(RO_DIR)/libmidisynth.a $(RO_DIR)/midiplay,ff8

$(RO_DIR)/%.o: src/%.c $(LIB_HDRS) $(THIRD_PARTY)
	@mkdir -p $(RO_DIR)
	$(CROSS)gcc $(RO_CFLAGS) $(WARN) $(INC) -c $< -o $@

$(RO_DIR)/libmidisynth.a: $(RO_OBJS)
	$(CROSS)ar rcs $@ $^

$(RO_DIR)/midiplay: examples/midiplay.c $(RO_DIR)/libmidisynth.a
	$(CROSS)gcc $(RO_CFLAGS) $(WARN) -Iinclude -static $< -L$(RO_DIR) $(if $(UNIXLIB),-L$(UNIXLIB)) -lmidisynth -lpthread -lm -o $@

$(RO_DIR)/midiplay,ff8: $(RO_DIR)/midiplay
	$(ELF2AIF) -e $< $@

install: $(RO_DIR)/libmidisynth.a
	mkdir -p $(GCCSDK_INSTALL_ENV)/include $(GCCSDK_INSTALL_ENV)/lib
	cp include/midisynth.h $(GCCSDK_INSTALL_ENV)/include/
	cp $(RO_DIR)/libmidisynth.a $(GCCSDK_INSTALL_ENV)/lib/

# ---- !SoftMIDI resource application -------------------------------------
app: $(RO_DIR)/midiplay,ff8
	rm -rf build/!SoftMIDI
	cp -r app/!SoftMIDI build/
	cp $(RO_DIR)/midiplay,ff8 build/!SoftMIDI/midiplay,ff8
	@if [ -f "$(SOUNDFONT)" ]; then \
	  cp "$(SOUNDFONT)" build/!SoftMIDI/SoundFont,ffd; \
	else \
	  echo "note: no SoundFont at $(SOUNDFONT); put one in !SoftMIDI as SoundFont"; \
	fi

# tools/mkrozip.py stores the RISC OS filetypes from the ,xxx suffixes
zip: app
	rm -f build/SoftMIDI-$(VERSION).zip
	$(PYTHON) tools/mkrozip.py build/SoftMIDI-$(VERSION).zip build '!SoftMIDI'

# ---- library tarball, for programs that link midisynth ------------------
# include/midisynth.h, lib/libmidisynth.a and the licences, so a program
# can build against a release without building midisynth itself.
DIST = libmidisynth-$(VERSION)-riscos
dist: $(RO_DIR)/libmidisynth.a
	rm -rf build/$(DIST) && mkdir -p build/$(DIST)/include build/$(DIST)/lib build/$(DIST)/licences
	cp include/midisynth.h build/$(DIST)/include/
	cp $(RO_DIR)/libmidisynth.a build/$(DIST)/lib/
	cp LICENSE build/$(DIST)/licences/midisynth.txt
	cp third_party/TinySoundFont/LICENSE build/$(DIST)/licences/TinySoundFont.txt
	cp third_party/stb/LICENSE build/$(DIST)/licences/stb_vorbis.txt
	cp CHANGELOG.md build/$(DIST)/
	tar -C build --owner=0 --group=0 --numeric-owner --sort=name \
	    --mtime=@$$(git log -1 --format=%ct 2>/dev/null || echo 0) \
	    -czf build/$(DIST).tar.gz $(DIST)

# ---- the SoundFont for the zip -----------------------------------------
# TimGM6mb 1.3 (GPL-2) from Ubuntu's timgm6mb-soundfont package, checked by
# sha256, so a build on GitHub needs no files put in by hand.
SF_DEB     = timgm6mb-soundfont_1.3-5_all.deb
SF_DEB_URL = http://archive.ubuntu.com/ubuntu/pool/universe/t/timgm6mb-soundfont/$(SF_DEB)
SF_DEB_SHA = b70bc29b8f27adef8f92a2d9c1e26cee977b84c68110f0dd4326d97730a7c3ed
SF_SHA     = c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854
SF_FILE    = build/dl/TimGM6mb.sf2
soundfont: $(SF_FILE)
$(SF_FILE):
	@mkdir -p build/dl/deb
	test -s build/dl/$(SF_DEB) || curl -fsSL --retry 3 -o build/dl/$(SF_DEB) $(SF_DEB_URL)
	echo "$(SF_DEB_SHA)  build/dl/$(SF_DEB)" | sha256sum -c --quiet
	cd build/dl/deb && ar x ../$(SF_DEB) data.tar.xz && tar xJf data.tar.xz ./usr/share/sounds/sf2/TimGM6mb.sf2
	mv build/dl/deb/usr/share/sounds/sf2/TimGM6mb.sf2 $@
	rm -rf build/dl/deb
	echo "$(SF_SHA)  $@" | sha256sum -c --quiet

# ---- riscos-crossdev's checks, and the whole CI build ------------------
# static linking, stack probes, FPU level (no VFPv4) and UnixLib
check-riscos: $(RO_DIR)/midiplay
	@test -n "$(RCD_DIR)" || { echo "set RCD_DIR to a riscos-crossdev checkout"; exit 1; }
	$(RCD_DIR)/checks/check-all.sh $(RO_DIR)/midiplay $(RO_DIR)/libmidisynth.a

# .github/workflows run this in the riscos-crossdev image
ci:
	$(MAKE) host
	$(MAKE) test
	$(MAKE) riscos
	$(MAKE) check-riscos
	$(MAKE) dist
	$(MAKE) soundfont
	$(MAKE) zip SOUNDFONT=$(SF_FILE)

# ---- test pack for RISC OS (tests/riscos) --------------------------------
# OPENMSX: a folder with OpenMSX's tttheme2.mid, linns_basket.mid,
# license.txt and readme.txt (in OpenTTD's music download).
# SF3: FluidR3Mono_GM.sf3, for the separate -SF3 zip (optional).
OPENMSX ?=
SF3 ?=
TEST_SUFFIX ?=
TESTS = SoftMIDITests-$(VERSION)$(TEST_SUFFIX)

riscos-tests: $(RO_DIR)/midiplay,ff8
	@test -n "$(OPENMSX)" || { echo "set OPENMSX=<folder with tttheme2.mid etc.>"; exit 1; }
	rm -rf build/tests && mkdir -p build/tests/SoftMIDITests/Songs
	cp tests/riscos/*,f?? build/tests/SoftMIDITests/
	cp app/!SoftMIDI/LoadSound,feb $(RO_DIR)/midiplay,ff8 build/tests/SoftMIDITests/
	cp $(OPENMSX)/tttheme2.mid build/tests/SoftMIDITests/Songs/tttheme2,fd4
	cp $(OPENMSX)/linns_basket.mid build/tests/SoftMIDITests/Songs/linns_basket,fd4
	cp $(OPENMSX)/license.txt build/tests/SoftMIDITests/Songs/Licence,fff
	cp $(OPENMSX)/readme.txt build/tests/SoftMIDITests/Songs/ReadMe,fff
	$(PYTHON) tools/mkrozip.py build/$(TESTS).zip build/tests SoftMIDITests
	@if [ -n "$(SF3)" ]; then \
	  rm -rf build/tests-sf3 && mkdir -p build/tests-sf3/SoftMIDITests/SoundFonts && \
	  cp "$(SF3)" build/tests-sf3/SoftMIDITests/SoundFonts/FluidR3Mono,ffd && \
	  cp "$(SF3:.sf3=-copyright.txt)" build/tests-sf3/SoftMIDITests/SoundFonts/Copyright,fff && \
	  $(PYTHON) tools/mkrozip.py build/SoftMIDITests-SF3.zip build/tests-sf3 SoftMIDITests; \
	fi

# ---- tests (host) -------------------------------------------------------
# test_midisynth: the synth, built as for the host (no sound output).
# test_output: the output backends, built against fake RISC OS sound SWIs
# (tests/fake_swi.c).
TEST_DIR   = build/test
ASAN_FLAGS = -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
TSAN_FLAGS = -O1 -fsanitize=thread
FAKE_FLAGS = -DMIDISYNTH_FAKE_SWI -Itests/fake -Itests
TEST_DEPS  = $(LIB_SRCS) $(LIB_HDRS) $(THIRD_PARTY) tests/test_midisynth.c \
             tests/test_output.c tests/fake_swi.c tests/fake_swi.h tests/fake/kernel.h

$(TEST_DIR)/files: tests/mktestfiles.py
	$(PYTHON) tests/mktestfiles.py $@
	@touch $@

# $(1) = flags, $(2) = name suffix
define test_build
	@mkdir -p $(TEST_DIR)
	$(CC) -g $(1) $(WARN) $(INC) $(LIB_SRCS) tests/test_midisynth.c -lpthread -lm -o $(TEST_DIR)/test_midisynth$(2)
	$(CC) -g $(1) $(WARN) $(INC) $(FAKE_FLAGS) $(LIB_SRCS) tests/fake_swi.c tests/test_output.c -lpthread -lm -o $(TEST_DIR)/test_output$(2)
endef
define test_run
	$(TEST_DIR)/test_midisynth$(1) $(TEST_DIR)/files
	$(TEST_DIR)/test_output$(1) $(TEST_DIR)/files
endef

$(TEST_DIR)/built: $(TEST_DEPS)
	$(call test_build,$(CFLAGS),)
	@touch $@

test: $(TEST_DIR)/built $(TEST_DIR)/files
	$(call test_run,)

test-asan: $(TEST_DIR)/files
	$(call test_build,$(ASAN_FLAGS),_asan)
	$(call test_run,_asan)

test-tsan: $(TEST_DIR)/files
	$(call test_build,$(TSAN_FLAGS),_tsan)
	$(call test_run,_tsan)

# Only after checking that a change in the sound is intended
test-update: $(TEST_DIR)/built $(TEST_DIR)/files
	$(TEST_DIR)/test_midisynth $(TEST_DIR)/files --update

clean:
	rm -rf build

# Changelog

## 0.3.2 (not yet released)

Easier to maintain. The sound is unchanged.

- Tests: `make test` (and `test-asan`, `test-tsan`) checks the library on
  a PC with a tiny SoundFont and MIDI files it makes itself
  (`tests/mktestfiles.py`), including the -60 dB note cut-off, muting,
  mixing, looping, the drum channel, bad files and two threads.
- `tools/mkrozip.py` makes the release zip with RISC OS filetypes, so
  GCCSDK's `zip` isn't needed. The zip is the same each time for the same
  files.
- `docs/DESIGN.md` (how it works and why) and `docs/RELEASING.md` (how to
  build and release).
- `midisynth.h`: `MIDISYNTH_VERSION` and friends; says what the functions
  return, which calls are safe from other threads, and that stop fades
  notes over 10 ms. The Makefile takes the version from here.
- `midisynth.c`: the code that uses TinySoundFont's private data is in one
  marked place; SharedSoundBuffer values have names and comments.
- `midiplay` gives its usage message for an unknown option or too many
  file names (it used to take them as file names).
- `midi2wav` and `midiplay` free the synth when they stop with an error.
- The Makefile rebuilds the library when the third-party files change.

## 0.3.1 (2026-09-26)

- `!MIDISynth` checks for the sound modules `*MIDIPlay` needs
  (SharedSound 1.07, StreamManager 0.03, SharedSoundBuffer 0.07). It
  loads them from `System:Modules` if they're there, and otherwise gives
  an error that points to `!Help`. The check runs when `*MIDIPlay` is
  used and when `!MIDISynth` is double-clicked, not at boot.
- `!Help` and the README say where to get StreamManager and
  SharedSoundBuffer, and link to John Duffell's site on the Internet
  Archive. They are not included: his terms don't allow publishing them
  on other web sites.
- `!Boot` loads `!Sprites`, so `!MIDISynth` shows its icon. It didn't
  before: when an app has a `!Boot`, the Filer leaves loading the
  sprites to it.
- No changes to the library or `midiplay`.

## 0.3.0 (2026-09-26)

- SF3 SoundFonts (SoundFont 2 with Ogg Vorbis compressed samples, as used
  by MuseScore) now load. They're decoded with stb_vorbis (v1.22, public
  domain / MIT) when the SoundFont loads. Before this, an .sf3 loaded
  without an error but played noise.
- SF2 output is unchanged. Loading an .sf2 briefly needs a few MB more
  memory.

## 0.2.0 (2026-09-26)

Faster.

- Released notes stop once they've faded below -60 dB (TinySoundFont
  waits for -80 dB). About 20% less work over the OpenMSX songs; the
  output differs by less than -70 dB. Only the note's envelope is used,
  not the channel volume, which a song can turn down and back up.
- Volume 0 mutes the synth: sounding notes stop, new notes aren't
  started, and rendering is skipped. The song keeps its place and its
  instrument and controller settings, so it carries on when the volume
  comes back.
- The RISC OS library is built with `-O3 -ffast-math -mtune=cortex-a72`.
- `midiplay -t` renders a song without playing it and reports the
  processor time; `-r` sets the sample rate.

## 0.1.0 (2026-09-26)

First version.

- The midisynth library:
  - plays Standard MIDI Files, or live MIDI events, through a SoundFont
    using TinySoundFont (commit 853a0a1);
  - renders into a caller's buffer (overwrite or mix);
  - on RISC OS, can also play on its own through SharedSoundBuffer;
  - accepts RISC OS filenames;
  - uses `MIDISynth$SoundFont` / `MIDISYNTH_SOUNDFONT` when no SoundFont
    is given.
- `midiplay`: a RISC OS command-line player (`*MIDIPlay`). Escape stops it.
- `midi2wav`: renders MIDI to WAV on any system.
- `!MIDISynth` resource application with the TimGM6mb SoundFont.

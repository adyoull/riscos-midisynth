# Changelog

## 0.1.0 (not yet released)

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

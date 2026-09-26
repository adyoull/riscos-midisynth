# riscos-midisynth

A General MIDI software synthesiser for RISC OS 5 programs.

RISC OS has no built-in General MIDI synth. Its MIDI support is for
external hardware. So a program that wants to play `.mid` music has to bring
its own. This library does that. It plays Standard MIDI Files, or MIDI events
sent live, through a SoundFont (`.sf2`), using
[TinySoundFont](https://github.com/schellingb/TinySoundFont).

You can use it in two ways:

- **Render into your own buffer.** `midisynth_render()` fills (or mixes into)
  a buffer of 16-bit stereo samples. Use this when your program already has
  sound output, such as an SDL audio callback or a game's mixer.
- **Play on its own.** On RISC OS, `midisynth_output_open()` plays through
  the SharedSoundBuffer module, so it mixes with other programs' sound.
  Call `midisynth_output_poll()` often (at least every 50 ms, for example on
  every Wimp null event).

It is written in portable C. It also builds on Linux and macOS, which is
handy for testing (`midi2wav`).

## Contents

| | |
|---|---|
| `include/midisynth.h` | the API |
| `src/midisynth.c` | the library |
| `examples/midiplay.c` | RISC OS command-line MIDI player (`*MIDIPlay`) |
| `examples/midi2wav.c` | render a MIDI file to a WAV file (any system) |
| `app/!MIDISynth` | resource application: holds the SoundFont, sets `MIDISynth$SoundFont`, adds `*MIDIPlay` |
| `third_party/TinySoundFont` | `tsf.h` / `tml.h`, unmodified |
| `third_party/stb` | `stb_vorbis.c` (decodes SF3 SoundFonts), unmodified |

## Using it

```c
#include "midisynth.h"

midisynth *ms = midisynth_create(NULL, 44100);   /* NULL: use MIDISynth$SoundFont */
if (!ms) { printf("%s\n", midisynth_error()); ... }
midisynth_load_file(ms, "<MyApp$Dir>.Music.theme");
midisynth_set_loop(ms, 1);
midisynth_play(ms);

/* then either, from your audio callback: */
midisynth_render(ms, samples, frames, 1);        /* 1 = mix into what's there */

/* or, with no sound output of your own: */
midisynth_output_open(ms, "MyApp");
/* ...and midisynth_output_poll(ms) on every null event */
```

Link with `-lmidisynth -lpthread -lm`.

- RISC OS filenames (`<MyApp$Dir>.Music.theme`, `SDFS::Disc.$.x`) and
  Unix-style names both work.
- The library is thread-safe. You can control playback from your main
  thread while another thread renders.
- Channel 9 (the tenth) is drums, as General MIDI requires.

### The SoundFont

A SoundFont holds the instrument samples. General MIDI ones are typically
6 to 30 MB, so it is better for programs to share one than to ship their own.
Passing `NULL` to `midisynth_create()` uses:

1. `MIDISynth$SoundFont` on RISC OS, which `!MIDISynth` sets when it boots;
2. `MIDISYNTH_SOUNDFONT` on other systems.

A program can also pass its own path, for example to use a SoundFont it
ships itself if `MIDISynth$SoundFont` isn't set.

The `!MIDISynth` release zip includes **TimGM6mb** (6 MB, GPL-2; see
`app/!MIDISynth/SFLicence,fff`). It gives good General MIDI coverage for its
size. Any General MIDI `.sf2` works. Larger ones (such as FluidR3_GM, 140 MB)
sound better but take a long time to load and use a lot of memory.

### File formats

- **Music:** Standard MIDI Files (`.mid`, filetype &FD4), formats 0, 1 and
  2. Not supported: SMPTE-timed files and RIFF `.rmi` files. SysEx and
  aftertouch are ignored.
- **SoundFonts:** SoundFont 2 (`.sf2`) and SF3 (`.sf3`, SoundFont 2 with
  Ogg Vorbis compressed samples, as used by MuseScore). SF3 files are small
  to download, but every sample is decoded when the SoundFont loads, and
  all SoundFonts are held in memory as 32-bit floats. For example,
  FluidR3Mono_GM.sf3 is a 24 MB file but needs about 285 MB of memory
  (nearer 400 MB while loading), and takes a while to decode. TimGM6mb
  needs 11 MB.

## Building

You need the [GCCSDK](https://www.riscos.info/index.php/GCCSDK) cross compiler
(GCC 10, `arm-riscos-gnueabihf`) for RISC OS builds.

```sh
make                                   # host: build/host/libmidisynth.a, midi2wav
make riscos GCCSDK_INSTALL_ENV=~/gccsdk/env ELF2AIF=/path/to/elf2aif
make install GCCSDK_INSTALL_ENV=~/gccsdk/env   # header + library into the GCCSDK env
make zip SOUNDFONT=/path/to/TimGM6mb.sf2 ...   # build/MIDISynth-0.3.1.zip
```

- `midiplay` is converted to an Absolute (AIF) file with `elf2aif -e`, so it
  doesn't need `!SharedLibs`.
- You can get TimGM6mb from Debian/Ubuntu's `timgm6mb-soundfont` package
  (`/usr/share/sounds/sf2/TimGM6mb.sf2`) or from the MuseScore repository.

## Requirements (RISC OS)

- RISC OS 5.
- The SharedSound, StreamManager and SharedSoundBuffer modules, for
  `midisynth_output_*` and `midiplay`. These are in `System:Modules` on
  current RISC OS 5 releases. `!MIDISynth` loads them when `*MIDIPlay`
  runs (its `LoadSound` file), and reports a clear error if one is
  missing.
  - StreamManager and SharedSoundBuffer are freeware, © John Duffell
    2004. His terms allow passing them on intact but not publishing them
    on other web sites (you must link to his site), so they are **not**
    included in the `!MIDISynth` zip. Get `ssb.zip` from the !RDPClient
    page at <https://orac.co.uk/software/rdpclient/>, where they are
    hosted by kind permission of the author. John Duffell's own site is
    on the Internet Archive:
    <https://web.archive.org/web/20110920080106/http://www.duffell.riscos.me.uk/>.
  - Programs that use `midisynth_output_*` should load the modules
    themselves first, for example with the same `RMEnsure` lines.
- CPU: rendering General MIDI in software takes a fair amount of CPU. On a
  PC it renders about 170 times faster than real time. It hasn't yet been
  measured on RISC OS hardware: `*MIDIPlay -t <file>` renders a song
  without playing it and reports the processor time it takes. On slower
  machines, a lower sample rate (22050) roughly halves the work.

## Performance

- SF3 decoding costs time only while the SoundFont loads; playing is the
  same speed as with an `.sf2`.

- Released notes are stopped once they have faded below -60 dB, instead of
  -80 dB. Over the OpenMSX songs this cuts the work by about a fifth, and
  the output changes by less than -70 dB.
- At volume 0 the synth is muted and costs almost nothing: notes aren't
  started, but the song keeps its place and its instrument settings.
- The RISC OS library is built with `-O3 -ffast-math`. A NEON build was
  tried; GCC finds almost nothing in the voice loop to vectorise.

## Programs using it

- [OpenTTD for RISC OS](https://github.com/adyoull/riscos-openttd): the
  `midisynth` music driver plays the OpenMSX soundtrack.

## Licence

MIT, see `LICENSE`. TinySoundFont is by Bernhard Schelling, also MIT.
SoundFonts have their own licences.

# How midisynth works, and why

Notes for whoever maintains this next. What the code does is in the code;
this is about the decisions behind it and the numbers that set them.

## Overview

- `src/midisynth.c` is the synth. It compiles TinySoundFont (`tsf.h`, the
  synthesiser; `tml.h`, the MIDI file reader) and stb_vorbis into itself,
  so users link one static library. It is portable C.
- `src/output.c` and one file per backend (`output_ssb.c`,
  `output_dr.c`) play the sound on RISC OS (see "Sound output" below).
  They reach RISC OS only through `_kernel_swi`, so on a PC the tests
  build them against fake SWIs.
- `src/midisynth_internal.h` has the synth's structure and what the files
  share; `include/midisynth.h` is all that users see.
- The third-party files are **unmodified**. Their commits are in each
  `third_party/*/VERSION`. Change behaviour in `midisynth.c`, not in them.
- One `struct midisynth` holds a TinySoundFont instance, the song (a
  linked list of `tml_message`s), the play position in milliseconds, the
  sound output if one is open, and the last error.
- Every public function takes the synth's pthread mutex, so one thread can
  render while another controls playback.

## API conventions

- Functions that can fail return 1/0 (or a pointer/NULL) and record why
  with `ms_error`, readable with `midisynth_last_error(ms)`. Each synth
  keeps its own message, so threads and synths don't overwrite each
  other's. `midisynth_error()` (one shared message) is kept for older
  programs.
- New settings go in `midisynth_config` with a default set by
  `midisynth_config_init`, which callers must use first. Programs link
  statically and are rebuilt with the header, so a longer struct is safe;
  new parameters on existing functions would not be.
- `midisynth_output_*` exist on every system, so portable programs need no
  `#ifdef`s; outside RISC OS `midisynth_output_open` returns 0.

## Rendering

- `midisynth_render` works in blocks of 64 frames
  (`TSF_RENDER_EFFECTSAMPLEBLOCK`). Before each block it plays the MIDI
  events that are due, so events are at most 64 frames (1.5 ms) late.
- At the end of a song it either rewinds (loop) or stops; notes that are
  still sounding ring out.
- All voices (96 by default, `max_voices` in `midisynth_config`) and all
  16 channels are created up front, so rendering never allocates memory. When all are in use, a new
  note takes the voice furthest into its release; if none is releasing,
  the new note isn't played.
- Channel 9 (MIDI channel 10) is the General MIDI drum kit: bank 128,
  and program changes on it stay in the drum bank.

## The numbers, and where they came from

All measured on an x86 PC with TimGM6mb and the 31 OpenMSX songs
(3814 s of music) that OpenTTD plays.

| Setting | Value | Why |
|---|---|---|
| `MS_BASE_GAIN` | 0.3 | At 0.6, TimGM6mb clipped up to 43,000 samples in a song. At 0.3, 19 samples clip over all 31 songs. |
| `MS_CULL_LEVEL` | 0.001 (-60 dB) | See below. |
| `MS_DEFAULT_VOICES` | 96 | On average 28 voices sound; the busiest songs reach 96. |
| SharedSoundBuffer queue | ~100 ms | Enough for a Wimp program that is polled every few tens of ms. Less means lower latency but more risk of gaps. |
| Output block | 1024 frames | StreamManager handles blocks of one size best. |

### The -60 dB cut-off (`ms_cull`)

- 78% of voice time was spent on notes that had been released, and 41%
  on released notes already quieter than -60 dB. TinySoundFont keeps them
  until -80 dB or so.
- After each 64-frame block, `ms_cull` stops released voices whose
  envelope is below -60 dB. That saved about 20% of the processor time;
  the output changed by less than -70 dB (largest sample difference 121).
- **Only the envelope level is used, not the channel volume.** A version
  that also counted the channel volume saved 36%, but broke songs that
  turn a channel down to 0 and back up (Linn's Basket does, at 72 s):
  loud notes were cut while silent, and were missing when the volume
  returned. The envelope never rises again after release, so it is safe.

### Muting (`set_volume(0)`)

- Kills every voice immediately (`tsf_voice_kill`), but not with
  `tsf_reset`, which would also forget each channel's instrument and
  controllers.
- Note-ons are ignored while muted, and rendering is skipped (the buffer
  is cleared, or left alone when mixing). 20 s of muted music takes
  0.002 s against 0.089 s playing. OpenTTD's music volume slider uses this.
- TinySoundFont would start a note silent anyway at volume 0, so skipping
  note-ons only saves processor time.

### Stop and pause

- `stop` and `pause` end notes with TinySoundFont's quick release: a 10 ms
  fade (`TSF_FASTRELEASETIME`) to avoid a click. Anything rendered in the
  next 10 ms still has a little of them.

## TinySoundFont's private data

`ms_cull` and `ms_kill_voices` read TinySoundFont's voice array directly
and call its static `tsf_voice_kill`. That's possible only because the
implementation is compiled into `midisynth.c`. They are grouped in one
marked section. After updating TinySoundFont, check them and run
`make test`.

## Things tried and dropped

- **NEON.** With `-mfpu=neon-vfpv4`, GCC vectorised almost nothing: the
  voice loop gathers samples at a fractional position (a `double`), which
  doesn't vectorise. The GCCSDK default FPU (VFPv3, no NEON) is used, so
  one build runs everywhere.
- **Build flags.** `-O3 -ffast-math -mtune=cortex-a72` made no measurable
  difference on a PC (0-5%); `-ffast-math` changes samples by at most 1.
  They are kept for the Pi, where they are more likely to help.
- **A SharedSound handler, or DigitalRenderer, as the main output.**
  Synthesis is the cost, not the output path, and rendering in the sound
  interrupt brings problems with paging and the VFP. SharedSoundBuffer
  stays first; DigitalRenderer is only the fallback (0.4.0).

## Ideas not done yet

- A 22050 Hz option (with 2x upsampling): about half the work, loses
  sound above 11 kHz (30% of TimGM6mb's samples are 44.1 kHz).
- Samples as 16-bit instead of float: half the memory (TinySoundFont
  converts to float at load: TimGM6mb's 6 MB becomes 12 MB; SF3 files are
  decoded completely, e.g. 24 MB becomes 285 MB). This means changing
  TinySoundFont's voice renderer, which would probably also be the
  biggest speed-up on a Pi. Measure on the Pi first (`*MIDIPlay -t`).
- More settings in `midisynth_config` (a gain, the queue length).
- A RISC OS module (SWIs `MIDISynth_Open/Write/Reset/Close`) so UnixLib's
  `/dev/midi`, BASIC and other programs can share one synth. The
  interface UnixLib expects is in riscos-unixlib `docs/MIDISYNTH-MODULE.md`.

## Sound output (RISC OS)

`midisynth_output_open` (`output.c`) tries each backend in turn and keeps
the first that opens; the reasons the others failed go in the error
message ("SharedSoundBuffer: SWI not known; DigitalRenderer: in use by
another program"). `MIDISynth$Output` names the only one to try. A backend
is three functions (`open`, `poll`, `close`, see `src/output.h`); each
keeps about 100 ms queued and renders more on every poll.

1. **SharedSoundBuffer** (`output_ssb.c`): mixes with other programs'
   sound through SharedSound, so it's the first choice.
2. **DigitalRenderer** (`output_dr.c`): the fallback, for systems without
   SharedSoundBuffer. It has one user at a time, so midisynth only uses
   it if nobody else is (UnixLib's `/dev/dsp` takes it over; we don't).
   The SWIs are called directly, not through `/dev/dsp`, which busy-waits.
   DigitalRenderer may play at a rate other than the one asked for; the
   synth then switches to that rate (`ms_set_rate`) before any sound is
   made.

### SharedSoundBuffer and StreamManager

The modules are by John Duffell (2004), in `System:Modules` on current
RISC OS 5. Their documentation (in `ssb.zip`) is out of date in places.
Where it differs, this code follows RDPClient's `c/Sound`, which works.

| SWI | Number | Use here |
|---|---|---|
| `SharedSoundBuffer_OpenStream` | &55FC0 | R0 = 2 (block size in R2), R1 = name, R2 = block size in bytes. Returns a handle. |
| `SharedSoundBuffer_CloseStream` | &55FC1 | R0 = handle |
| `SharedSoundBuffer_Volume` | &55FC4 | R1 = &LLLLRRRR; always full, the library applies its own volume |
| `SharedSoundBuffer_SampleRate` | &55FC5 | R1 = rate x 1024 |
| `SharedSoundBuffer_Pause` | &55FC9 | R1 bit 0 set = play, clear = pause |
| `SharedSoundBuffer_ReturnStreamHandle` | &55FCE | the StreamManager stream underneath |
| `StreamManager_AddBlock` | &57282 | R1 = data, R2 = bytes; copies the data |
| `StreamManager_SetBuffer` | &57287 | R1 = most bytes it may hold |
| `StreamManager_BufferStats` | &57288 | returns R0 = bytes added, R1 = bytes played (the 2004 docs say R0 = unplayed) |

- The stream starts paused and is started once two blocks are queued, so
  it doesn't begin with a gap.
- `midisynth_output_poll` renders up to 16 blocks per call, until about
  100 ms is queued. It must be called at least every 50 ms.
- StreamManager and SharedSoundBuffer may not be redistributed on web
  sites (John Duffell's terms), so the release zip doesn't include them;
  `!MIDISynth.LoadSound` loads them from `System:Modules`.

### DigitalRenderer

By Andreas Dehmel. SWI chunk &4F700; numbers and usage follow GCCSDK's
`DRender.h` and UnixLib's `sound/dsp.c`.

| SWI | Offset | Use here |
|---|---|---|
| `Deactivate` | 1 | on close |
| `ReadState` | 5 | bit 0 set = in use (by someone else: don't open) |
| `NumBuffers` | 9 | R0 = buffers to queue (100 ms of 512 frames: 9 at 44.1 kHz); 0 on close |
| `Stream16BitSamples` | 11 | R0 = data, R1 = samples (left and right count separately) |
| `StreamStatistics` | 12 | returns the buffers waiting |
| `StreamFlags` | 13 | R0 = EOR, R1 = AND: set bit 0, silence when we fall behind |
| `Activate16` | 15 | R0 = 2 channels, R1 = 512 frames per buffer, R2 = rate, R3 = 1 (restore the old handler after) |
| `GetFrequency` | 16 | the rate actually used |
| `SampleFormat` | 18 | 3 = 16-bit, left then right |

- Order, as in UnixLib: NumBuffers, StreamFlags, Activate16, then
  GetFrequency and SampleFormat (which only work once active).

### Testing without RISC OS

`tests/fake_swi.c` fakes the three modules: it records what each SWI was
given, keeps the sound it receives, and "plays" only when the test says
time has passed. `tests/test_output.c` checks the settings each backend
uses, how much it keeps queued, that the sound received is exactly what
`midisynth_render` produces, the fallback, the rate change, and that
DigitalRenderer isn't taken from another program. Pointers passed in
registers go through `MS_PTR` (`src/riscos_swi.h`), which the fake swaps
for small tokens, because a 64-bit pointer doesn't fit in an int.

## File names

`ms_path` converts RISC OS names (containing `:` or starting with one of
`<$@&%`) to Unix form with UnixLib's `__unixify_std`, because TinySoundFont
opens files with `fopen`. Unix-style names pass through.

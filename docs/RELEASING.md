# Building and releasing midisynth

## What you need

- Linux or macOS with `make`, a C compiler and Python 3.
- For RISC OS builds: the GCCSDK GCC 10 cross compiler
  (`arm-riscos-gnueabihf`). Set `GCCSDK_INSTALL_ENV` to its `env` folder.
- `elf2aif` to turn `midiplay` into an Absolute file. Use the one in
  [riscos-openttd](https://github.com/adyoull/riscos-openttd)
  `tools/elf2aif`: GCCSDK's, with fixes for large programs. Build it with
  its Makefile; it needs the GCCSDK source for two headers
  (`GCCSDK_SRC=... make`, see its README). Pass it as `ELF2AIF=`.
- The TimGM6mb SoundFont for the release zip: Debian/Ubuntu package
  `timgm6mb-soundfont` (`/usr/share/sounds/sf2/TimGM6mb.sf2`, version
  1.3), or the MuseScore repository. It is GPL-2: keep
  `app/!MIDISynth/SFLicence` with it.

## Everyday work

```sh
make                 # host library and midi2wav
make test            # tests of the synth and of the sound output (a few seconds)
make test-asan       # the same, checking memory use
make test-tsan       # the same, checking threads
```

- Keep `CHANGELOG.md` up to date with every change.
- The tests compare the test song's loudness over time with
  `tests/expected-rms.txt`. If a change is *meant* to change the sound
  (a TinySoundFont update, a new gain), listen to the result first
  (`build/host/midi2wav song.mid TimGM6mb.sf2 out.wav`), then run
  `make test-update` and commit the new file with the change.
- The Pi is the machine that matters for speed: `*MIDIPlay -t song.mid`
  prints how much processor time a song needs (`-r 22050` for half rate).

## Making a release

1. Set the version in `include/midisynth.h` (`MIDISYNTH_VERSION_*` and
   `MIDISYNTH_VERSION`). The Makefile reads it from there.
2. In `CHANGELOG.md`, change "(not yet released)" to the date.
3. Check: `make clean && make test test-asan test-tsan`.
4. Build for RISC OS and make the zip, linking `midiplay` with the latest
   [riscos-unixlib](https://github.com/adyoull/riscos-unixlib) release
   (`UNIXLIB` is the folder holding its `libunixlib.a`; leave it out to use
   the one in the GCCSDK environment):
   ```sh
   make riscos zip GCCSDK_INSTALL_ENV=~/gccsdk/env ELF2AIF=/path/to/elf2aif \
        UNIXLIB=/path/to/riscos-unixlib-release SOUNDFONT=/path/to/TimGM6mb.sf2
   ```
   Check the `libunixlib.a` against the release's `SHA256SUMS` first.
   This gives `build/MIDISynth-<version>.zip`, with `!MIDISynth`, the
   SoundFont and `midiplay`, and the RISC OS filetypes set.
   - Set `SOURCE_DATE_EPOCH` to get the same zip every time.
   - Don't add files to an existing zip with GCCSDK's `zip -,`: it
     duplicates entries. Make a new one.
5. Try it on RISC OS: double-click `!MIDISynth` (icon, no error), then
   `*MIDIPlay -l song.mid` (plays, loops, Escape stops it). Then
   `*Set MIDISynth$Output DigitalRenderer` and play again (it should say
   "playing through DigitalRenderer"), and `*Unset MIDISynth$Output`.
6. Commit, tag and push:
   ```sh
   git tag v<version>
   git push
   git push origin v<version>
   ```
7. On GitHub, make a release from the tag, paste the CHANGELOG entry as
   the notes, and attach the zip. With the GitHub CLI:
   ```sh
   gh release create v<version> build/MIDISynth-<version>.zip --title "midisynth <version>" --notes-file notes.md
   ```

## Programs that use the library

- OpenTTD for RISC OS links it statically (its `build/build-deps.sh` runs
  `make install` here). Keep the API backwards compatible; if it has to
  change, OpenTTD's music driver (`src/music/midisynth_m.cpp`) needs the
  matching change, and `MIDISYNTH_VERSION_NUM` lets it check.

## Version numbers

- Text-only changes (docs, `!Help`, Obey files) keep the same version.
- Anything else gets a new patch (0.3.x) or minor (0.x.0) version.

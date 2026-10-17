# Building and releasing midisynth

## What you need

- Linux or macOS with `make`, a C compiler and Python 3.
- For RISC OS builds: [riscos-crossdev](https://github.com/adyoull/riscos-crossdev),
  the release named in `.riscos-deps`. Its toolchain is GCCSDK GCC 10.2
  with riscos-unixlib, and its `elf2aif` has the fixes for large
  programs. Either:
  - use its container image, as GitHub does:
    `docker run --rm -v "$PWD":/src ghcr.io/adyoull/riscos-crossdev:1.8.1 make ci`;
  - or install it (its `docs/SETUP.md`) and `. env.sh` from it, which sets
    `RISCOS_TOOLCHAIN` and `RCD_DIR` and puts `elf2aif` on `PATH`. The
    Makefile then finds the toolchain by itself.

  Another GCCSDK environment works too: set `GCCSDK_INSTALL_ENV` to it,
  and `ELF2AIF=` to an `elf2aif`.
- The TimGM6mb SoundFont for the release zip: `make soundfont` fetches it
  (from Ubuntu's `timgm6mb-soundfont` 1.3 package, checked by sha256) as
  `build/dl/TimGM6mb.sf2`. It is GPL-2: keep `app/!SoftMIDI/SFLicence`
  with it.

## On GitHub

- `.github/workflows/riscos-build.yml` runs `make ci` in the
  riscos-crossdev image on every push and pull request: the host build and
  tests, the RISC OS build, riscos-crossdev's checks on `midiplay`
  (static, stack probes, no VFPv4, UnixLib), the library tarball and the
  zip. The zip and tarball are kept with the run.
- `.github/workflows/riscos-deps-update.yml` looks once a day for a newer
  release of what `.riscos-deps` names (riscos-crossdev). If there is one,
  it builds with it and opens a pull request saying whether that passed.
  Nothing is merged or released by itself: merge, test on a Pi, release.
- They need, in Settings > Actions > General, "Workflow permissions: Read
  and write" and "Allow GitHub Actions to create and approve pull
  requests". See riscos-crossdev's `docs/CI.md`.

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
4. Build everything with `make ci` (in the riscos-crossdev image, or with
   its `env.sh`). This gives:
   - `build/SoftMIDI-<version>.zip`: `!SoftMIDI`, with the SoundFont and
     `midiplay`, and the RISC OS filetypes set;
   - `build/libmidisynth-<version>-riscos.tar.gz`: the header, the RISC OS
     library and the licences, for programs that link midisynth.

   `midiplay` is linked with the toolchain's UnixLib. To link it with a
   different [riscos-unixlib](https://github.com/adyoull/riscos-unixlib)
   release, add `UNIXLIB=<folder with its libunixlib.a>` (check it against
   the release's `SHA256SUMS`).
   - Set `SOURCE_DATE_EPOCH` to get the same zip every time.
   - Don't add files to an existing zip with GCCSDK's `zip -,`: it
     duplicates entries. Make a new one.
5. Try it on RISC OS: double-click `!SoftMIDI` (icon, no error), then
   `*MIDIPlay -l song.mid` (plays, loops, Escape stops it). Then
   `*Set SoftMIDI$Output DigitalRenderer` and play again (it should say
   "playing through DigitalRenderer"), and `*Unset SoftMIDI$Output`.
6. Commit, tag and push:
   ```sh
   git tag v<version>
   git push
   git push origin v<version>
   ```
7. On GitHub, make a release from the tag, paste the CHANGELOG entry as
   the notes, and attach the zip and the library tarball. Mark it
   "latest": programs that depend on midisynth (their `.riscos-deps`) only
   see the latest release. With the GitHub CLI:
   ```sh
   gh release create v<version> build/SoftMIDI-<version>.zip \
      build/libmidisynth-<version>-riscos.tar.gz \
      --title "SoftMIDI <version>" --notes-file notes.md --latest
   ```

## Programs that use the library

- OpenTTD and fheroes2 for RISC OS link it statically, and pin a release
  (`riscos-softmidi v<version>` in their `.riscos-deps`). They can take
  the library tarball from that release instead of building it. Keep the API backwards compatible; if it has to
  change, OpenTTD's music driver (`src/music/midisynth_m.cpp`) needs the
  matching change, and `MIDISYNTH_VERSION_NUM` lets it check.

## Version numbers

- Text-only changes (docs, `!Help`, Obey files) keep the same version.
- Anything else gets a new patch (0.3.x) or minor (0.x.0) version.

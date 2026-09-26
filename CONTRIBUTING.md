# Working on midisynth

## Before you commit

- `make test` passes (and `make test-asan test-tsan` for anything that
  touches memory or threads).
- `CHANGELOG.md` says what changed, in plain words.
- If the sound changes on purpose, `make test-update` and commit
  `tests/expected-rms.txt` with the change (see `docs/RELEASING.md`).

## Where things go

- `src/midisynth.c`: the synth. Portable C: no RISC OS calls apart from
  file names.
- `src/output*.c`: sound output on RISC OS, one file per backend (see
  `src/output.h`). They talk to RISC OS only through `_kernel_swi`, so the
  tests can fake it (`tests/fake_swi.c`).
- `src/midisynth_internal.h`: what the source files share. Nothing in it
  is public.
- `include/midisynth.h`: the public API. Keep it backwards compatible:
  OpenTTD links the library statically. Add new settings to
  `midisynth_config` (with a default in `midisynth_config_init`), not new
  parameters to existing functions.
- `third_party/`: unmodified upstream code, with the commit in `VERSION`.
  Change behaviour in `src/`, never here.

## Code style

- C99, 4-space indents, no tabs (Makefile excepted); see `.editorconfig`.
- In `src/`, a function's return type goes on its own line, and so does
  the opening brace of a function; other braces go on the same line.
- Public names start `midisynth_`; internal ones `ms_` (shared between
  files) or a short file prefix (`ssb_`, `dr_`); constants in capitals.
- Functions that can fail return 1 or 0 (or a pointer or NULL) and record
  the reason with `ms_error`.
- Comments say why, not what. Numbers that were measured say where from
  (or point to `docs/DESIGN.md`).
- No memory allocation while rendering.
- Plain, short English in comments and docs.

## Versions

- The version is in `include/midisynth.h`; the Makefile reads it.
- Changes to text only (docs, `!Help`, Obey files) keep the version.
  Fixes get a new patch number (0.4.x); new API or features a new minor
  number (0.x.0).

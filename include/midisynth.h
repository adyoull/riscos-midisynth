/*
 * midisynth - General MIDI software synthesiser for RISC OS programs
 *
 * Plays Standard MIDI Files, or MIDI events sent live, through a
 * SoundFont (.sf2) using TinySoundFont. It can render into a buffer you
 * supply (to mix with your own sound, e.g. from an SDL audio callback), or
 * play on its own through the RISC OS 5 SharedSoundBuffer module.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence, see LICENSE.
 */
#ifndef MIDISYNTH_H
#define MIDISYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The library version. Programs that link midisynth statically can check
   it at compile time, e.g. #if MIDISYNTH_VERSION_NUM >= 302 */
#define MIDISYNTH_VERSION_MAJOR 0
#define MIDISYNTH_VERSION_MINOR 3
#define MIDISYNTH_VERSION_PATCH 2
#define MIDISYNTH_VERSION       "0.3.2"
#define MIDISYNTH_VERSION_NUM   (MIDISYNTH_VERSION_MAJOR * 10000 + \
                                 MIDISYNTH_VERSION_MINOR * 100 + MIDISYNTH_VERSION_PATCH)

/* Threads: every call on a synth may be made from any thread; each synth
   has its own lock. The exceptions: midisynth_error() is shared (see
   below), and the midisynth_output_* calls should all be made from one
   thread. Functions returning int return 1 on success, 0 on failure. */

typedef struct midisynth midisynth;

/* Create a synthesiser. sf2 is the SoundFont to use; NULL means the one
   named by the MIDISynth$SoundFont system variable (RISC OS) or the
   MIDISYNTH_SOUNDFONT environment variable (elsewhere). Output is 16-bit
   stereo at sample_rate. Returns NULL on failure (see midisynth_error). */
midisynth *midisynth_create(const char *sf2, int sample_rate);
void midisynth_destroy(midisynth *ms);

/* Why the last call failed. One buffer shared by all synths and threads,
   so read it straight after the call that failed. */
const char *midisynth_error(void);

/* Load a Standard MIDI File (from a file or memory). Any song already
   playing stops. The new song starts paused; call midisynth_play.
   Return 1, or 0 if it isn't a MIDI file (see midisynth_error). The data
   given to midisynth_load_memory is copied, so it can be freed after. */
int midisynth_load_file(midisynth *ms, const char *path);
int midisynth_load_memory(midisynth *ms, const void *data, int size);

void midisynth_play(midisynth *ms);          /* start or resume */
void midisynth_pause(midisynth *ms);
void midisynth_stop(midisynth *ms);          /* stop and rewind; sounding notes
                                                fade out over 10 ms */
int  midisynth_playing(midisynth *ms);       /* 1 while a song is playing */
void midisynth_set_loop(midisynth *ms, int loop);
/* Volume 0.0 - 1.0. At 0 the synth is muted and costs almost nothing:
   sounding notes stop, new notes aren't started, but the song keeps its
   place and follows instrument and controller changes. */
void midisynth_set_volume(midisynth *ms, float volume);

/* Live MIDI (channels 0-15, channel 9 is drums) */
void midisynth_note_on(midisynth *ms, int channel, int key, int velocity);
void midisynth_note_off(midisynth *ms, int channel, int key);
void midisynth_program(midisynth *ms, int channel, int program);
void midisynth_control(midisynth *ms, int channel, int control, int value);
void midisynth_pitch_bend(midisynth *ms, int channel, int value); /* 0-16383 */
void midisynth_all_notes_off(midisynth *ms);
/* A raw MIDI message: status byte plus up to two data bytes */
void midisynth_send(midisynth *ms, int status, int data1, int data2);

/* Render 'frames' stereo frames of 16-bit samples into 'buffer'. With
   mix != 0 the music is added to what's already there (with clipping),
   otherwise the buffer is overwritten. Safe to call from another thread
   than the one controlling playback. */
void midisynth_render(midisynth *ms, int16_t *buffer, int frames, int mix);

#ifdef __riscos__
/* Play through SharedSoundBuffer on its own, for programs without their
   own sound output. After open, call midisynth_output_poll often (for
   example on every Wimp null event, at least every 50 ms): it renders
   more audio whenever the queue runs low. 'name' is shown as the sound
   source's name. Returns 1, or 0 if SharedSoundBuffer can't be used
   (see midisynth_error). midisynth_destroy closes the output too. */
int  midisynth_output_open(midisynth *ms, const char *name);
void midisynth_output_poll(midisynth *ms);
void midisynth_output_close(midisynth *ms);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MIDISYNTH_H */

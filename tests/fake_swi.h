/* Fake SharedSoundBuffer, StreamManager and DigitalRenderer for the
   output tests (tests/test_output.c). They keep everything played in
   'audio', and play only when the test says time has passed. */
#ifndef FAKE_SWI_H
#define FAKE_SWI_H

#include <stdint.h>

#define FAKE_AUDIO_FRAMES (44100 * 20)

struct fake_state
{
    /* set by the test */
    int ssb_present, dr_present;
    int dr_busy;                   /* another program is using DigitalRenderer */
    int dr_rate;                   /* the rate DigitalRenderer plays at (0: as asked) */
    int sm_refuse;                 /* StreamManager_AddBlock fails (buffer full) */

    /* what the code under test did */
    int ssb_open, ssb_flags, ssb_blocksize, ssb_rate, ssb_paused, ssb_volume, ssb_closed;
    char ssb_name[64];
    int sm_limit;                  /* SetBuffer */
    long sm_added, sm_played;      /* bytes */
    int dr_active, dr_numbuffers, dr_flags, dr_format, dr_bufframes, dr_asked_rate;
    int dr_queued;                 /* buffers waiting */
    long dr_played;                /* frames of the current buffer played */
    int dr_deactivated;
    int16_t audio[FAKE_AUDIO_FRAMES * 2];
    long audio_frames;             /* frames received so far */
};

extern struct fake_state fake;

void fake_reset(void);
/* Let 'frames' frames of queued sound play. */
void fake_play(long frames);

#endif

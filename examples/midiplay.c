/* midiplay: play a MIDI file through SharedSoundBuffer (RISC OS).
   midiplay [-l] [-v volume%] <song.mid> [soundfont.sf2]
   Without a SoundFont argument it uses MIDISynth$SoundFont.
   Press Escape to stop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "midisynth.h"

static volatile sig_atomic_t stop_req;
static void on_sigint(int sig) { (void)sig; stop_req = 1; }

int main(int argc, char **argv)
{
    int i = 1, loop = 0, vol = 100, tail = 0;
    const char *song = NULL, *sf2 = NULL;
    midisynth *ms;
    struct timespec ts = { 0, 20 * 1000000 };   /* 20 ms */

    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) loop = 1;
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) vol = atoi(argv[++i]);
        else if (!song) song = argv[i];
        else sf2 = argv[i];
    }
    if (!song) {
        fprintf(stderr, "usage: midiplay [-l] [-v volume%%] <song.mid> [soundfont.sf2]\n");
        return 1;
    }
    ms = midisynth_create(sf2, 44100);
    if (!ms || !midisynth_load_file(ms, song) || !midisynth_output_open(ms, "midiplay")) {
        fprintf(stderr, "midiplay: %s\n", midisynth_error());
        return 1;
    }
    midisynth_set_loop(ms, loop);
    midisynth_set_volume(ms, vol / 100.0f);
    signal(SIGINT, on_sigint);   /* UnixLib raises SIGINT on Escape */
    midisynth_play(ms);
    /* Keep the queue topped up until the song (and 2 s of ringing notes)
       has finished, or Escape is pressed. */
    while (tail < 100) {
        midisynth_output_poll(ms);
        if (stop_req)
            break;
        if (!midisynth_playing(ms))
            tail++;
        nanosleep(&ts, NULL);
    }
    midisynth_destroy(ms);
    return 0;
}

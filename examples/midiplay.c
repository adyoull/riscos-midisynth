/* midiplay: play a MIDI file through SharedSoundBuffer (RISC OS).
   midiplay [-l] [-v volume%] [-r rate] [-t] <song.mid> [soundfont.sf2]
   Without a SoundFont argument it uses MIDISynth$SoundFont.
   Press Escape to stop.
   -t renders the whole song as fast as possible without playing it, and
   reports how much processor time the music takes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>
#include "midisynth.h"

static volatile sig_atomic_t stop_req;
static void on_sigint(int sig) { (void)sig; stop_req = 1; }

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Render the song flat out and report the cost. */
static int benchmark(midisynth *ms, int rate)
{
    static int16_t buf[1024 * 2];
    long frames = 0;
    double start, worst = 0, t0, t1, took;

    midisynth_play(ms);
    start = now();
    while (midisynth_playing(ms) && !stop_req) {
        t0 = now();
        midisynth_render(ms, buf, 1024, 0);
        t1 = now();
        if (t1 - t0 > worst) worst = t1 - t0;
        frames += 1024;
    }
    took = now() - start;
    printf("%.1f s of music (%d Hz) rendered in %.2f s\n", (double) frames / rate, rate, took);
    printf("Processor use while playing: %.1f%%\n", 100.0 * took * rate / frames);
    printf("Slowest 1024-sample block: %.1f ms (it lasts %.1f ms)\n", worst * 1000, 1024000.0 / rate);
    midisynth_destroy(ms);
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1, loop = 0, vol = 100, tail = 0, rate = 44100, timing = 0;
    const char *song = NULL, *sf2 = NULL;
    midisynth *ms;
    struct timespec ts = { 0, 20 * 1000000 };   /* 20 ms */

    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) loop = 1;
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) vol = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t")) timing = 1;
        else if (!song) song = argv[i];
        else sf2 = argv[i];
    }
    if (!song) {
        fprintf(stderr, "usage: midiplay [-l] [-v volume%%] [-r rate] [-t] <song.mid> [soundfont.sf2]\n");
        return 1;
    }
    signal(SIGINT, on_sigint);   /* UnixLib raises SIGINT on Escape */
    ms = midisynth_create(sf2, rate);
    if (ms && timing) {
        if (!midisynth_load_file(ms, song)) {
            fprintf(stderr, "midiplay: %s\n", midisynth_error());
            return 1;
        }
        return benchmark(ms, rate);
    }
    if (!ms || !midisynth_load_file(ms, song) || !midisynth_output_open(ms, "midiplay")) {
        fprintf(stderr, "midiplay: %s\n", midisynth_error());
        return 1;
    }
    midisynth_set_loop(ms, loop);
    midisynth_set_volume(ms, vol / 100.0f);
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

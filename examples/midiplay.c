/* midiplay: play a MIDI file (RISC OS), through SharedSoundBuffer or, if
   that isn't available, DigitalRenderer.
   midiplay [-l] [-v volume%] [-r rate] [-t] [-s secs] [-o file] <song.mid> [soundfont]
   Without a SoundFont argument it uses MIDISynth$SoundFont.
   Press Escape to stop.
   -t       renders the whole song as fast as possible without playing it,
            and reports how much processor time the music takes.
   -s secs  stops after that many seconds (of playing, or of music with -t).
   -o file  adds everything midiplay reports to the end of that file too
            (for test runs, where "> file" redirection isn't wanted). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <signal.h>
#include "midisynth.h"
#ifdef __riscos__
#include <unixlib/local.h>
#endif

static volatile sig_atomic_t stop_req;
static void on_sigint(int sig) { (void)sig; stop_req = 1; }

static FILE *outlog;                 /* -o file, or NULL */

/* Report to 'to' (stdout or stderr) and to the -o file */
static void say(FILE *to, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(to, fmt, ap);
    va_end(ap);
    if (outlog) {
        va_start(ap, fmt);
        vfprintf(outlog, fmt, ap);
        va_end(ap);
        fflush(outlog);
    }
}

/* A RISC OS file name ("<Test$Dir>.Results", "SDFS::Disc.$.x") in the
   Unix form fopen wants; other names are returned as they are. */
static const char *unix_name(const char *name, char *buf, size_t len)
{
#ifdef __riscos__
    if (name[0] != '/' && (strchr(name, ':') || strchr("<$@&%", name[0])))
        if (__unixify_std(name, buf, len, __RISCOSIFY_FILETYPE_NOTSPECIFIED))
            return buf;
#else
    (void)buf; (void)len;
#endif
    return name;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Render the song flat out (or 'secs' of it) and report the cost. */
static int benchmark(midisynth *ms, int rate, int secs)
{
    static int16_t buf[1024 * 2];
    long frames = 0, limit = secs > 0 ? (long)secs * rate : -1;
    double start, worst = 0, t0, t1, took;

    midisynth_play(ms);
    start = now();
    while (midisynth_playing(ms) && !stop_req && (limit < 0 || frames < limit)) {
        t0 = now();
        midisynth_render(ms, buf, 1024, 0);
        t1 = now();
        if (t1 - t0 > worst) worst = t1 - t0;
        frames += 1024;
    }
    took = now() - start;
    if (frames == 0)
        frames = 1;
    say(stdout, "%.1f s of music (%d Hz) rendered in %.2f s\n", (double) frames / rate, rate, took);
    say(stdout, "Processor use while playing: %.1f%%\n", 100.0 * took * rate / frames);
    say(stdout, "Slowest 1024-sample block: %.1f ms (it lasts %.1f ms)\n", worst * 1000, 1024000.0 / rate);
    midisynth_destroy(ms);
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1, loop = 0, vol = 100, tail = 0, rate = 44100, timing = 0, bad = 0, secs = 0;
    const char *song = NULL, *sf2 = NULL, *logname = NULL;
    midisynth *ms;
    struct timespec ts = { 0, 20 * 1000000 };   /* 20 ms */
    double t0, started;

    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) loop = 1;
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) vol = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) secs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) logname = argv[++i];
        else if (!strcmp(argv[i], "-t")) timing = 1;
        else if (argv[i][0] == '-') bad = 1;    /* unknown option */
        else if (!song) song = argv[i];
        else if (!sf2) sf2 = argv[i];
        else bad = 1;                            /* too many names */
    }
    if (!song || bad) {
        fprintf(stderr, "usage: midiplay [-l] [-v volume%%] [-r rate] [-t] [-s secs] [-o file] <song.mid> [soundfont]\n");
        return 1;
    }
    char lbuf[1024];
    if (logname && (outlog = fopen(unix_name(logname, lbuf, sizeof lbuf), "a")) == NULL) {
        fprintf(stderr, "midiplay: can't open %s\n", logname);
        return 1;
    }
    if (outlog) {
        fprintf(outlog, "midiplay (midisynth %s):", MIDISYNTH_VERSION);
        for (i = 1; i < argc; i++)
            fprintf(outlog, " %s", argv[i]);
        fprintf(outlog, "\n");
    }
    signal(SIGINT, on_sigint);   /* UnixLib raises SIGINT on Escape */

    t0 = now();
    ms = midisynth_create(sf2, rate);
    if (!ms) {
        say(stderr, "midiplay: %s\n", midisynth_last_error(NULL));
        return 1;
    }
    say(stdout, "SoundFont loaded in %.2f s\n", now() - t0);
    if (!midisynth_load_file(ms, song)) {
        say(stderr, "midiplay: %s\n", midisynth_last_error(ms));
        midisynth_destroy(ms);
        return 1;
    }
    if (timing)
        return benchmark(ms, rate, secs);

    if (!midisynth_output_open(ms, "midiplay")) {
        say(stderr, "midiplay: %s\n", midisynth_last_error(ms));
        midisynth_destroy(ms);
        return 1;
    }
    say(stdout, "midiplay: playing through %s\n", midisynth_output_name(ms));
    midisynth_set_loop(ms, loop);
    midisynth_set_volume(ms, vol / 100.0f);
    midisynth_play(ms);
    started = now();
    /* Keep the queue topped up until the song (and 2 s of ringing notes)
       has finished, Escape is pressed, or -s seconds have passed. */
    while (tail < 100) {
        midisynth_output_poll(ms);
        if (stop_req || (secs > 0 && now() - started >= secs))
            break;
        if (!midisynth_playing(ms))
            tail++;
        nanosleep(&ts, NULL);
    }
    say(stdout, "midiplay: stopped after %.1f s%s\n", now() - started, stop_req ? " (Escape)" : "");
    midisynth_destroy(ms);
    if (outlog)
        fclose(outlog);
    return 0;
}

/* Tests for the midisynth library, run on the host (Linux/macOS):
       make test           normal build
       make test-asan      with the address and undefined-behaviour checkers
       make test-tsan      with the thread checker
   The files it uses are made by tests/mktestfiles.py (a tiny SoundFont
   and some MIDI files), so nothing has to be downloaded.

   Usage: test_midisynth <dir with the test files> [--update]
   --update rewrites tests/expected-rms.txt (the loudness of the test song
   over time) from the current output. Do that only after checking a
   change in the sound is intended, e.g. after updating TinySoundFont. */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "midisynth.h"

#define RATE     44100
#define BLOCK    2048                     /* frames per RMS step */
#define MAXSONG  (RATE * 10)              /* frames: more than the song */

static int failures, checks;
static char dir[512], sf2[600], song[600];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *path(char *buf, const char *name)
{
    snprintf(buf, 600, "%s/%s", dir, name);
    return buf;
}

static double rms(const int16_t *b, int frames)
{
    double sum = 0;
    int i;
    for (i = 0; i < frames * 2; i++)
        sum += (double)b[i] * b[i];
    return frames ? sqrt(sum / (frames * 2)) : 0;
}

static int peak(const int16_t *b, int frames)
{
    int i, p = 0;
    for (i = 0; i < frames * 2; i++)
        if (abs(b[i]) > p) p = abs(b[i]);
    return p;
}

/* Render the whole song (until it stops playing, plus 1 s). Returns frames. */
static int render_song(midisynth *ms, int16_t *out)
{
    int frames = 0, tail = 0;
    midisynth_play(ms);
    while (frames + BLOCK <= MAXSONG && tail < RATE) {
        if (!midisynth_playing(ms)) tail += BLOCK;
        midisynth_render(ms, out + frames * 2, BLOCK, 0);
        frames += BLOCK;
    }
    return frames;
}

static void test_create_errors(void)
{
    char p[600];
    midisynth *ms = midisynth_create(path(p, "no-such.sf2"), RATE);
    CHECK(ms == NULL, "a missing SoundFont was accepted");
    CHECK(strstr(midisynth_error(), "Couldn't load") != NULL, "error was \"%s\"", midisynth_error());
    ms = midisynth_create(path(p, "song.mid"), RATE);
    CHECK(ms == NULL, "a MIDI file was accepted as a SoundFont");
    ms = midisynth_create(sf2, 1000);
    CHECK(ms == NULL, "sample rate 1000 was accepted");
    CHECK(strstr(midisynth_error(), "sample rate") != NULL, "error was \"%s\"", midisynth_error());
}

static void test_bad_songs(midisynth *ms)
{
    static const char *bad[] = { "bad-short.mid", "bad-random.mid", "bad-empty.mid", "no-such.mid" };
    char p[600];
    unsigned i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(midisynth_load_file(ms, path(p, bad[i])) == 0, "%s was accepted", bad[i]);
        CHECK(strstr(midisynth_error(), "Not a MIDI file") != NULL, "%s: error was \"%s\"", bad[i], midisynth_error());
    }
    CHECK(midisynth_load_memory(ms, "MThd", 4) == 0, "4 bytes of memory were accepted");
}

/* The song plays, ends, and sounds as it did when expected-rms.txt was made. */
static void test_song(midisynth *ms, int16_t *a, int16_t *b, int update)
{
    int frames, frames2, i, n, bad = 0;
    FILE *f;

    CHECK(midisynth_load_file(ms, song), "song.mid: %s", midisynth_error());
    CHECK(!midisynth_playing(ms), "a new song should start paused");
    frames = render_song(ms, a);
    CHECK(frames < MAXSONG, "the song never ended");
    CHECK(!midisynth_playing(ms), "still playing after the end");
    CHECK(peak(a, frames) > 1000, "too quiet: peak %d", peak(a, frames));
    CHECK(rms(a + (frames - BLOCK) * 2, BLOCK) == 0, "not silent 1 s after the end: released notes weren't stopped");

    /* stop rewinds; playing again gives exactly the same sound */
    midisynth_stop(ms);
    frames2 = render_song(ms, b);
    CHECK(frames2 == frames && !memcmp(a, b, frames * 4), "second play differs from the first");

    if (update) {
        f = fopen("tests/expected-rms.txt", "w");
        if (!f) { perror("tests/expected-rms.txt"); exit(1); }
        fprintf(f, "# RMS of tests/song.mid through test.sf2, per %d frames at %d Hz (see test_midisynth.c)\n", BLOCK, RATE);
        for (i = 0; i < frames / BLOCK; i++)
            fprintf(f, "%.1f\n", rms(a + i * BLOCK * 2, BLOCK));
        fclose(f);
        printf("tests/expected-rms.txt updated (%d values)\n", frames / BLOCK);
        return;
    }
    f = fopen("tests/expected-rms.txt", "r");
    CHECK(f != NULL, "tests/expected-rms.txt is missing (make test-update)");
    if (!f) return;
    {
        char line[100];
        n = 0;
        while (fgets(line, sizeof line, f)) {
            double want, got;
            if (line[0] == '#') continue;
            want = atof(line);
            got = n < frames / BLOCK ? rms(a + n * BLOCK * 2, BLOCK) : -1;
            if (fabs(got - want) > want * 0.02 + 2 && bad++ < 5)
                printf("  block %d: RMS %.1f, expected %.1f\n", n, got, want);
            n++;
        }
        fclose(f);
    }
    CHECK(n == frames / BLOCK, "song length %d blocks, expected %d", frames / BLOCK, n);
    CHECK(bad == 0, "%d blocks sound different from tests/expected-rms.txt", bad);
}

/* Stop and let the notes finish: stop fades them over 10 ms
   (TinySoundFont's TSF_FASTRELEASETIME) rather than cutting them off. */
static void stop_quietly(midisynth *ms)
{
    int16_t scratch[BLOCK * 2];
    midisynth_stop(ms);
    midisynth_render(ms, scratch, BLOCK, 0);
    CHECK(peak(scratch + (BLOCK - 64) * 2, 64) == 0, "notes still sounding 46 ms after stop");
}

/* Mixing adds to what's in the buffer. */
static void test_mix(midisynth *ms, int16_t *a, int16_t *b)
{
    int i, wrong = 0;
    midisynth_load_file(ms, song);
    midisynth_play(ms);
    midisynth_render(ms, a, BLOCK * 4, 0);
    stop_quietly(ms);
    midisynth_play(ms);
    for (i = 0; i < BLOCK * 4 * 2; i++) b[i] = 1000;
    midisynth_render(ms, b, BLOCK * 4, 1);
    for (i = 0; i < BLOCK * 4 * 2; i++) {
        int want = a[i] + 1000;
        if (want > 32767) want = 32767;
        if (want < -32768) want = -32768;
        if (b[i] != want)
            wrong++;
    }
    CHECK(wrong == 0, "mix: %d samples aren't the rendered music + 1000", wrong);
    stop_quietly(ms);
}

/* Zero crossings of the left channel: twice as many = an octave up */
static int crossings(const int16_t *b, int frames)
{
    int i, n = 0;
    for (i = 1; i < frames; i++)
        if ((b[i * 2] > 0) != (b[i * 2 - 2] > 0)) n++;
    return n;
}

/* Released notes stop once they're below -60 dB (MS_CULL_LEVEL), sooner
   than TinySoundFont alone would stop them. test.sf2's release is 500 ms:
   with the cull a note is silent after about 380 ms, without it 420 ms. */
static void test_cull(midisynth *ms, int16_t *a)
{
    int ms_until_silent = -1, i;
    midisynth_note_on(ms, 0, 60, 100);
    midisynth_render(ms, a, BLOCK, 0);
    midisynth_note_off(ms, 0, 60);
    for (i = 0; i < 100 && ms_until_silent < 0; i++) {
        midisynth_render(ms, a, 512, 0);
        if (peak(a, 512) == 0)
            ms_until_silent = i * 512 * 1000 / RATE;
    }
    CHECK(ms_until_silent >= 0 && ms_until_silent < 400,
          "a released note took %d ms to stop (expected under 400)", ms_until_silent);
}

/* Volume 0: silent, but the song keeps its place. */
static void test_mute(midisynth *ms, int16_t *a)
{
    midisynth_load_file(ms, song);
    midisynth_play(ms);
    midisynth_render(ms, a, BLOCK, 0);
    midisynth_set_volume(ms, 0.0f);
    midisynth_render(ms, a, BLOCK * 8, 0);
    CHECK(peak(a, BLOCK * 8) == 0, "muted but not silent (peak %d)", peak(a, BLOCK * 8));
    CHECK(midisynth_playing(ms), "muting stopped the song");
    midisynth_set_volume(ms, 1.0f);
    midisynth_render(ms, a, BLOCK * 8, 0);
    CHECK(peak(a, BLOCK * 8) > 0, "silent after unmuting");
    stop_quietly(ms);

    /* Muting drops held notes, and notes sent while muted never start.
       (ms_event skipping note-ons while muted only saves processor time:
       TinySoundFont would start them silent anyway, so it can't be seen
       in the output.) */
    midisynth_note_on(ms, 0, 60, 100);
    midisynth_render(ms, a, BLOCK, 0);
    midisynth_set_volume(ms, 0.0f);
    midisynth_note_on(ms, 1, 64, 100);
    midisynth_set_volume(ms, 1.0f);
    midisynth_render(ms, a, BLOCK, 0);
    CHECK(peak(a, BLOCK) == 0, "notes from before or during the mute are sounding (peak %d)", peak(a, BLOCK));
    midisynth_all_notes_off(ms);
}

static void test_loop(midisynth *ms, int16_t *a)
{
    int frames = 0;
    midisynth_load_file(ms, song);
    midisynth_set_loop(ms, 1);
    midisynth_play(ms);
    while (frames < RATE * 8) {                /* twice the song's length */
        midisynth_render(ms, a, BLOCK, 0);
        frames += BLOCK;
    }
    CHECK(midisynth_playing(ms), "a looping song stopped");
    midisynth_set_loop(ms, 0);
    midisynth_stop(ms);
}

/* Memory given to load_memory can be freed straight away. */
static void test_load_memory(midisynth *ms, int16_t *a)
{
    FILE *f = fopen(song, "rb");
    char *buf = malloc(4096);
    int n = f ? (int)fread(buf, 1, 4096, f) : 0;
    if (f) fclose(f);
    CHECK(midisynth_load_memory(ms, buf, n), "load_memory: %s", midisynth_error());
    memset(buf, 0xAA, 4096);
    free(buf);
    midisynth_play(ms);
    midisynth_render(ms, a, BLOCK * 16, 0);
    CHECK(peak(a, BLOCK * 16) > 1000, "song from memory is silent");
    midisynth_stop(ms);
}

/* Live MIDI: a note sounds, then fades to exact silence after note off. */
static void test_live(midisynth *ms, int16_t *a)
{
    midisynth_load_file(ms, song);             /* not played: just a quiet synth */
    midisynth_send(ms, 0xC0, 0, 0);
    midisynth_send(ms, 0x90, 60, 100);
    midisynth_render(ms, a, BLOCK, 0);
    CHECK(peak(a, BLOCK) > 1000, "note on is silent");
    midisynth_send(ms, 0xE0, 0x00, 0x7F);      /* pitch bend right up: mustn't crash */
    midisynth_note_off(ms, 0, 60);
    midisynth_render(ms, a, RATE * 2, 0);      /* longer than the 0.5 s release */
    CHECK(peak(a + (RATE * 2 - BLOCK) * 2, BLOCK) == 0, "note didn't end after note off");
    {
        int melody, drums;
        midisynth_note_on(ms, 0, 60, 100);
        midisynth_render(ms, a, BLOCK, 0);
        melody = crossings(a, BLOCK);
        stop_quietly(ms);
        midisynth_control(ms, 9, 0, 0);            /* bank select 0, as some songs send, */
        midisynth_program(ms, 9, 0);               /* then a program change: still drums */
        midisynth_note_on(ms, 9, 60, 100);         /* channel 10: the drum kit */
        midisynth_render(ms, a, BLOCK, 0);
        drums = crossings(a, BLOCK);
        CHECK(drums > melody * 3 / 2, "channel 10 doesn't use the drum kit (%d vs %d crossings)", drums, melody);
    }
    midisynth_all_notes_off(ms);
    midisynth_stop(ms);
}

/* One thread renders while another controls: run with make test-tsan. */
static midisynth *thread_ms;
static int thread_done;
static pthread_mutex_t done_lock = PTHREAD_MUTEX_INITIALIZER;

static int done(void)
{
    int d;
    pthread_mutex_lock(&done_lock);
    d = thread_done;
    pthread_mutex_unlock(&done_lock);
    return d;
}

static void *render_thread(void *arg)
{
    int16_t buf[1024 * 2];
    (void)arg;
    while (!done())
        midisynth_render(thread_ms, buf, 1024, 1);
    return NULL;
}

static void test_threads(midisynth *ms)
{
    pthread_t t;
    int i;
    thread_ms = ms;
    pthread_create(&t, NULL, render_thread, NULL);
    for (i = 0; i < 200; i++) {
        midisynth_load_file(ms, song);
        midisynth_play(ms);
        midisynth_send(ms, 0x90 | (i & 15), 40 + i % 40, 100);
        midisynth_set_volume(ms, (i % 3) / 2.0f);
        midisynth_pitch_bend(ms, i & 15, i * 80);
        midisynth_pause(ms);
        midisynth_stop(ms);
    }
    pthread_mutex_lock(&done_lock);
    thread_done = 1;
    pthread_mutex_unlock(&done_lock);
    pthread_join(t, NULL);
    midisynth_set_volume(ms, 1.0f);
    CHECK(1, "threads");
}

int main(int argc, char **argv)
{
    int update = argc > 2 && !strcmp(argv[2], "--update");
    int16_t *a = calloc(MAXSONG * 2, sizeof(int16_t)), *b = calloc(MAXSONG * 2, sizeof(int16_t));
    midisynth *ms;

    if (argc < 2 || !a || !b) {
        fprintf(stderr, "usage: test_midisynth <test files dir> [--update]\n");
        return 2;
    }
    snprintf(dir, sizeof dir, "%s", argv[1]);
    path(sf2, "test.sf2");
    path(song, "song.mid");

    test_create_errors();
    ms = midisynth_create(sf2, RATE);
    CHECK(ms != NULL, "test.sf2: %s", midisynth_error());
    if (!ms) return 1;
    CHECK(strcmp(MIDISYNTH_VERSION, "") != 0, "no version");
    test_bad_songs(ms);
    test_song(ms, a, b, update);
    if (!update) {
        test_mix(ms, a, b);
        test_mute(ms, a);
        test_cull(ms, a);
        test_loop(ms, a);
        test_load_memory(ms, a);
        test_live(ms, a);
        test_threads(ms);
    }
    midisynth_destroy(ms);
    free(a);
    free(b);
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}

/* Tests for midisynth_output_* (output.c, output_ssb.c, output_dr.c),
   run on the host against fake SharedSoundBuffer, StreamManager and
   DigitalRenderer SWIs (tests/fake_swi.c). Built by "make test".

   Usage: test_output <dir with the test files> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "midisynth.h"
#include "fake_swi.h"

static int failures, checks;
static char sf2[600], song[600];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* A synth playing the test song, at 'rate' */
static midisynth *playing_synth(int rate)
{
    midisynth *ms = midisynth_create(sf2, rate);
    if (!ms || !midisynth_load_file(ms, song)) {
        printf("can't set up: %s\n", midisynth_last_error(ms));
        exit(1);
    }
    midisynth_play(ms);
    return ms;
}

/* Is what the fake received the same as rendering directly? */
static int same_as_direct(int rate)
{
    midisynth *ref = playing_synth(rate);
    int16_t *buf = malloc(fake.audio_frames * 4 + 4);
    int same;
    midisynth_render(ref, buf, (int)fake.audio_frames, 0);
    same = fake.audio_frames > 0 && !memcmp(buf, fake.audio, fake.audio_frames * 4);
    free(buf);
    midisynth_destroy(ref);
    return same;
}

/* Poll and let time pass, like a program polling every 20 ms */
static void run(midisynth *ms, int polls, int rate)
{
    int i;
    for (i = 0; i < polls; i++) {
        midisynth_output_poll(ms);
        fake_play(rate / 50);
    }
}

static void test_ssb(void)
{
    midisynth *ms;
    long queued;

    fake_reset();
    ms = playing_synth(44100);
    CHECK(midisynth_output_name(ms) == NULL, "output name before open");
    CHECK(midisynth_output_open(ms, "Test"), "open: %s", midisynth_last_error(ms));
    CHECK(midisynth_output_name(ms) && !strcmp(midisynth_output_name(ms), "SharedSoundBuffer"),
          "used %s", midisynth_output_name(ms));
    CHECK(!strcmp(fake.ssb_name, "Test"), "stream name \"%s\"", fake.ssb_name);
    CHECK(fake.ssb_flags == 2 && fake.ssb_blocksize == 4096, "OpenStream flags %d block %d", fake.ssb_flags, fake.ssb_blocksize);
    CHECK(fake.ssb_rate == 44100 * 1024, "SampleRate %d", fake.ssb_rate);
    CHECK(fake.ssb_volume == -1, "Volume %x", fake.ssb_volume);
    CHECK(fake.ssb_paused, "should start paused");
    CHECK(midisynth_output_open(ms, "Again"), "second open should be harmless");

    midisynth_output_poll(ms);
    queued = fake.sm_added - fake.sm_played;
    CHECK(queued >= 17640 && queued < 17640 + 4096, "queued %ld bytes after the first poll (want about 100 ms)", queued);
    CHECK(!fake.ssb_paused, "not started with two blocks queued");
    midisynth_output_poll(ms);
    CHECK(fake.sm_added - fake.sm_played == queued, "a poll with a full queue added more");

    run(ms, 150, 44100);           /* 3 seconds */
    CHECK(fake.audio_frames > 44100 * 3, "only %ld frames played in 3 s", fake.audio_frames);
    CHECK(same_as_direct(44100), "sound through SharedSoundBuffer differs from direct rendering");

    fake.sm_refuse = 1;            /* StreamManager full: poll must give up quietly */
    run(ms, 5, 44100);
    fake.sm_refuse = 0;

    midisynth_output_close(ms);
    CHECK(fake.ssb_closed == 1 && midisynth_output_name(ms) == NULL, "close");
    midisynth_output_poll(ms);     /* after close: nothing happens */
    midisynth_output_close(ms);
    CHECK(fake.ssb_closed == 1, "closed twice");

    CHECK(midisynth_output_open(ms, NULL) && !strcmp(fake.ssb_name, "MIDISynth"), "default name \"%s\"", fake.ssb_name);
    midisynth_destroy(ms);
    CHECK(fake.ssb_closed == 2, "destroy didn't close the stream");
}

static void test_dr_fallback(void)
{
    midisynth *ms;

    fake_reset();
    fake.ssb_present = 0;
    ms = playing_synth(44100);
    CHECK(midisynth_output_open(ms, "Test"), "open: %s", midisynth_last_error(ms));
    CHECK(midisynth_output_name(ms) && !strcmp(midisynth_output_name(ms), "DigitalRenderer"),
          "used %s", midisynth_output_name(ms));
    CHECK(fake.dr_numbuffers == 9, "NumBuffers %d (want 9: 100 ms of 512 frames)", fake.dr_numbuffers);
    CHECK(fake.dr_flags == 1, "StreamFlags %d", fake.dr_flags);
    CHECK(fake.dr_asked_rate == 44100 && fake.dr_bufframes == 512, "Activate16 rate %d, buffer %d", fake.dr_asked_rate, fake.dr_bufframes);
    CHECK(fake.dr_format == 3, "SampleFormat %d (want 3, left-right)", fake.dr_format);
    midisynth_output_poll(ms);
    CHECK(fake.dr_queued == 9, "%d buffers queued after the first poll", fake.dr_queued);
    run(ms, 150, 44100);
    CHECK(fake.audio_frames > 44100 * 3, "only %ld frames played in 3 s", fake.audio_frames);
    CHECK(same_as_direct(44100), "sound through DigitalRenderer differs from direct rendering");
    midisynth_destroy(ms);
    CHECK(fake.dr_deactivated == 1 && fake.dr_numbuffers == 0, "destroy didn't stop DigitalRenderer");
}

static void test_dr_rate(void)
{
    midisynth *ms;

    fake_reset();
    fake.ssb_present = 0;
    fake.dr_rate = 48000;          /* DigitalRenderer can't do 44100 */
    ms = playing_synth(44100);
    CHECK(midisynth_output_open(ms, "Test"), "open: %s", midisynth_last_error(ms));
    run(ms, 100, 48000);
    CHECK(same_as_direct(48000), "after DigitalRenderer chose 48000 Hz, the sound isn't the synth's at 48000 Hz");
    midisynth_destroy(ms);

    fake_reset();
    fake.ssb_present = 0;
    fake.dr_rate = 200000;         /* a rate the synth can't use */
    ms = playing_synth(44100);
    CHECK(!midisynth_output_open(ms, "Test"), "opened at 200 kHz");
    CHECK(strstr(midisynth_last_error(ms), "200000") != NULL, "error \"%s\"", midisynth_last_error(ms));
    CHECK(fake.dr_deactivated == 1 && fake.dr_numbuffers == 0, "DigitalRenderer left active after a failed open");
    midisynth_destroy(ms);
}

static void test_failures(void)
{
    midisynth *ms, *other;

    fake_reset();
    fake.ssb_present = 0;
    fake.dr_busy = 1;
    ms = playing_synth(44100);
    other = playing_synth(44100);
    CHECK(!midisynth_output_open(ms, "Test"), "took DigitalRenderer from another program");
    CHECK(strstr(midisynth_last_error(ms), "SharedSoundBuffer: SWI not known") != NULL
          && strstr(midisynth_last_error(ms), "DigitalRenderer: in use") != NULL,
          "error \"%s\"", midisynth_last_error(ms));
    CHECK(midisynth_last_error(other)[0] == 0, "the other synth's error is \"%s\"", midisynth_last_error(other));
    CHECK(fake.dr_numbuffers == 0 && !fake.dr_active, "touched DigitalRenderer while it was busy");

    fake.dr_present = 0;
    CHECK(!midisynth_output_open(ms, "Test"), "opened with no sound modules");
    CHECK(midisynth_output_name(ms) == NULL, "has an output name after failing");
    midisynth_output_poll(ms);     /* must be harmless */
    midisynth_destroy(ms);
    midisynth_destroy(other);
}

static void test_choice(void)
{
    midisynth *ms;

    fake_reset();
    setenv("MIDISynth$Output", "digitalrenderer", 1);
    ms = playing_synth(44100);
    CHECK(midisynth_output_open(ms, "Test"), "open: %s", midisynth_last_error(ms));
    CHECK(midisynth_output_name(ms) && !strcmp(midisynth_output_name(ms), "DigitalRenderer"),
          "MIDISynth$Output ignored: used %s", midisynth_output_name(ms));
    CHECK(!fake.ssb_open, "opened SharedSoundBuffer too");
    midisynth_destroy(ms);

    fake_reset();
    setenv("MIDISynth$Output", "Nonsense", 1);
    ms = playing_synth(44100);
    CHECK(!midisynth_output_open(ms, "Test"), "opened with MIDISynth$Output=Nonsense");
    CHECK(strstr(midisynth_last_error(ms), "no known output") != NULL, "error \"%s\"", midisynth_last_error(ms));
    midisynth_destroy(ms);
    unsetenv("MIDISynth$Output");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_output <test files dir>\n");
        return 2;
    }
    snprintf(sf2, sizeof sf2, "%s/test.sf2", argv[1]);
    snprintf(song, sizeof song, "%s/song.mid", argv[1]);
    unsetenv("MIDISynth$Output");

    test_ssb();
    test_dr_fallback();
    test_dr_rate();
    test_failures();
    test_choice();

    printf("output: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}

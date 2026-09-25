/* midi2wav: render a MIDI file to a WAV file with midisynth.
   midi2wav <song.mid> <soundfont.sf2> <out.wav> [rate]
   Useful for testing a SoundFont, and on any system. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "midisynth.h"

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    int rate = argc > 4 ? atoi(argv[4]) : 44100;
    midisynth *ms;
    FILE *out;
    int16_t buf[1024 * 2];
    uint32_t frames = 0, tail = 0;

    if (argc < 4) {
        fprintf(stderr, "usage: midi2wav <song.mid> <soundfont.sf2> <out.wav> [rate]\n");
        return 1;
    }
    ms = midisynth_create(argv[2], rate);
    if (!ms || !midisynth_load_file(ms, argv[1])) {
        fprintf(stderr, "midi2wav: %s\n", midisynth_error());
        return 1;
    }
    out = fopen(argv[3], "wb");
    if (!out) { perror(argv[3]); return 1; }
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, out);
    put32(out, 16); put16(out, 1); put16(out, 2); put32(out, rate);
    put32(out, rate * 4); put16(out, 4); put16(out, 16);
    fwrite("data\0\0\0\0", 1, 8, out);

    midisynth_play(ms);
    /* play the song, then 2 seconds for the last notes to ring out */
    while (midisynth_playing(ms) || tail < (uint32_t)rate * 2) {
        if (!midisynth_playing(ms)) tail += 1024;
        midisynth_render(ms, buf, 1024, 0);
        fwrite(buf, 4, 1024, out);
        frames += 1024;
    }
    fseek(out, 4, SEEK_SET); put32(out, 36 + frames * 4);
    fseek(out, 40, SEEK_SET); put32(out, frames * 4);
    fclose(out);
    midisynth_destroy(ms);
    printf("%s: %.1f seconds\n", argv[3], frames / (double)rate);
    return 0;
}

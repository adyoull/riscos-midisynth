/*
 * midisynth - General MIDI software synthesiser for RISC OS programs
 * Copyright (c) 2026 Andrew Youll. MIT licence, see LICENSE.
 *
 * TinySoundFont (tsf.h, tml.h) by Bernhard Schelling, MIT licence.
 * stb_vorbis by Sean Barrett, public domain / MIT licence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* stb_vorbis decodes the Ogg Vorbis samples in SF3 SoundFonts.
   TinySoundFont uses it when it has been included first. Only decoding
   from memory is needed. */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#define STB_VORBIS_MAX_CHANNELS 2
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"   /* known false alarms */
#include "../third_party/stb/stb_vorbis.c"
#pragma GCC diagnostic pop

#define TSF_IMPLEMENTATION
#include "../third_party/TinySoundFont/tsf.h"
#define TML_IMPLEMENTATION
#include "../third_party/TinySoundFont/tml.h"

#include "midisynth.h"

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>
#endif

#define MS_CHANNELS    16
#define MS_MAX_VOICES  96          /* allocated up front: no malloc while rendering */
#define MS_BASE_GAIN   0.3f        /* General MIDI SoundFonts are loud */
/* A released note is stopped once its envelope is below -60 dB, rather
   than TinySoundFont's -80 dB. Over the OpenMSX songs this saves about a
   quarter of the work; the difference in the output is 70 dB below the
   music. Only the envelope is used: it never rises again after release,
   whereas the channel volume can (songs dip CC7 to 0 and back). */
#define MS_CULL_LEVEL  0.001f

#ifdef __riscos__
/* SharedSoundBuffer 0.07 and StreamManager 0.03, by John Duffell. The
   documentation in ssb.zip (2004) is out of date in places; where it
   differs, this follows RDPClient's c/Sound, which is known to work (see
   docs/DESIGN.md). The X bit (0x20000) makes errors come back instead of
   being raised. */
#define XSharedSoundBuffer_OpenStream         (0x20000 | 0x55FC0)
#define XSharedSoundBuffer_CloseStream        (0x20000 | 0x55FC1)
#define XSharedSoundBuffer_Volume             (0x20000 | 0x55FC4)
#define XSharedSoundBuffer_SampleRate         (0x20000 | 0x55FC5)
#define XSharedSoundBuffer_Pause              (0x20000 | 0x55FC9)
#define XSharedSoundBuffer_ReturnStreamHandle (0x20000 | 0x55FCE)
#define XStreamManager_AddBlock               (0x20000 | 0x57282)
#define XStreamManager_SetBuffer              (0x20000 | 0x57287)
#define XStreamManager_BufferStats            (0x20000 | 0x57288)

#define SSB_OPEN_BLOCKSIZE  (1u << 1)      /* OpenStream R0: R2 holds the usual block size */
#define SSB_PAUSE_RESUME    (1u << 0)      /* Pause R1: set = play, clear = pause */
#define SSB_VOLUME_FULL     0xFFFFFFFFu    /* Volume R1: &LLLLRRRR, left and right 0-&FFFF */
#define SSB_RATE_UNIT       1024           /* SampleRate R1 is in 1/1024 Hz */

#define MS_FRAME_BYTES 4           /* one frame: 16-bit left + 16-bit right */
#define MS_OUT_FRAMES  1024        /* frames per block given to StreamManager */
#define MS_QUEUE_MS    100         /* audio to keep queued ahead */
#define MS_BUFFER_MAX  4           /* StreamManager may hold up to 4 x that */
#endif

struct midisynth
{
    pthread_mutex_t lock;
    tsf *sf;
    int rate;
    tml_message *song;             /* whole song (freed on unload) */
    tml_message *next;             /* next event to play */
    double msec;                   /* song position */
    int playing, loop;
    float volume;
#ifdef __riscos__
    int ssb;                       /* SharedSoundBuffer stream handle, 0 = closed */
    int stream;                    /* the StreamManager stream under it */
    int started;                   /* playing yet? (starts paused) */
    int16_t *outbuf;               /* one block, MS_OUT_FRAMES frames */
    int target;                    /* bytes to keep queued (MS_QUEUE_MS worth) */
#endif
};

/* The last error. One buffer for all synths and threads, so read it
   straight after the call that failed. */
static char ms_errbuf[320];

/* File names may be given the RISC OS way (from a system variable, say:
   "SDFS::Disc.$.Sounds.GM/sf2" or "<MIDISynth$Dir>.GM/sf2"); UnixLib's
   fopen expects Unix-style names, so convert those first. */
static const char *
ms_path(const char *name, char *buf, size_t len)
{
#ifdef __riscos__
    if (name[0] != '/' && (strchr(name, ':') != NULL || strchr("<$@&%", name[0]) != NULL)) {
        if (__unixify_std(name, buf, len, __RISCOSIFY_FILETYPE_NOTSPECIFIED) != NULL)
            return buf;
    }
#else
    (void)buf; (void)len;
#endif
    return name;
}

const char *
midisynth_error(void)
{
    return ms_errbuf;
}

static void
ms_seterror(const char *msg, const char *extra)
{
    snprintf(ms_errbuf, sizeof(ms_errbuf), "%s%s%s", msg, extra ? ": " : "", extra ? extra : "");
}

/* ---- TinySoundFont internals -------------------------------------------
   These two functions use TinySoundFont's private voice data (tsf->voices,
   voiceNum, playingPreset, ampenv) and its static tsf_voice_kill(), which
   we can reach only because the implementation is compiled into this
   file. They are the only places that do. Check them whenever
   third_party/TinySoundFont is updated: if the names change they stop
   compiling; if the meaning changes, the tests in tests/ should catch it. */

/* Stop every sounding voice at once, without resetting the channels'
   instruments and controllers (tsf_reset would). Lock held. */
static void
ms_kill_voices(midisynth *ms)
{
    struct tsf_voice *v = ms->sf->voices, *end = v + ms->sf->voiceNum;
    for (; v != end; v++)
        tsf_voice_kill(v);
}

/* Stop released notes that have faded below MS_CULL_LEVEL. Lock held. */
static void
ms_cull(midisynth *ms)
{
    struct tsf_voice *v = ms->sf->voices, *end = v + ms->sf->voiceNum;
    for (; v != end; v++)
        if (v->playingPreset != -1 && v->ampenv.segment == TSF_SEGMENT_RELEASE &&
            v->ampenv.level < MS_CULL_LEVEL)
            tsf_voice_kill(v);
}
/* ---- end of TinySoundFont internals ------------------------------------ */

/* Channel set-up: every channel exists (so rendering never allocates), 9 is
   the General MIDI drum kit. */
static void
ms_init_channels(midisynth *ms)
{
    int ch;
    for (ch = 0; ch < MS_CHANNELS; ch++) {
        if (ch == 9)
            tsf_channel_set_bank_preset(ms->sf, 9, 128, 0);
        else
            tsf_channel_set_presetnumber(ms->sf, ch, 0, 0);
    }
}

midisynth *
midisynth_create(const char *sf2, int sample_rate)
{
    midisynth *ms;

    if (sf2 == NULL || *sf2 == 0) {
#ifdef __riscos__
        sf2 = getenv("MIDISynth$SoundFont");
#else
        sf2 = getenv("MIDISYNTH_SOUNDFONT");
#endif
    }
    if (sf2 == NULL || *sf2 == 0) {
        ms_seterror("No SoundFont given (and MIDISynth$SoundFont isn't set)", NULL);
        return NULL;
    }
    if (sample_rate < 8000 || sample_rate > 96000) {
        ms_seterror("Unsupported sample rate", NULL);
        return NULL;
    }

    ms = (midisynth *) calloc(1, sizeof(*ms));
    if (ms == NULL) {
        ms_seterror("Out of memory", NULL);
        return NULL;
    }
    {
        char pbuf[1024];
        ms->sf = tsf_load_filename(ms_path(sf2, pbuf, sizeof(pbuf)));
    }
    if (ms->sf == NULL) {
        ms_seterror("Couldn't load the SoundFont", sf2);
        free(ms);
        return NULL;
    }
    ms->rate = sample_rate;
    ms->volume = 1.0f;
    tsf_set_output(ms->sf, TSF_STEREO_INTERLEAVED, sample_rate, 0.0f);
    tsf_set_volume(ms->sf, MS_BASE_GAIN);
    tsf_set_max_voices(ms->sf, MS_MAX_VOICES);
    ms_init_channels(ms);
    pthread_mutex_init(&ms->lock, NULL);
    ms_errbuf[0] = 0;
    return ms;
}

static void
ms_unload(midisynth *ms)
{
    ms->playing = 0;
    ms->next = NULL;
    ms->msec = 0;
    if (ms->song) {
        tml_free(ms->song);
        ms->song = NULL;
    }
    tsf_reset(ms->sf);             /* silence and default every channel */
    ms_init_channels(ms);
}

void
midisynth_destroy(midisynth *ms)
{
    if (ms == NULL)
        return;
#ifdef __riscos__
    midisynth_output_close(ms);
#endif
    pthread_mutex_lock(&ms->lock);
    ms_unload(ms);
    tsf_close(ms->sf);
    pthread_mutex_unlock(&ms->lock);
    pthread_mutex_destroy(&ms->lock);
    free(ms);
}

static int
ms_set_song(midisynth *ms, tml_message *song, const char *what)
{
    if (song == NULL) {
        ms_seterror("Not a MIDI file", what);
        return 0;
    }
    pthread_mutex_lock(&ms->lock);
    ms_unload(ms);
    ms->song = song;
    ms->next = song;
    pthread_mutex_unlock(&ms->lock);
    return 1;
}

int
midisynth_load_file(midisynth *ms, const char *path)
{
    char pbuf[1024];
    return ms_set_song(ms, tml_load_filename(ms_path(path, pbuf, sizeof(pbuf))), path);
}

int
midisynth_load_memory(midisynth *ms, const void *data, int size)
{
    return ms_set_song(ms, tml_load_memory(data, size), NULL);
}

void
midisynth_play(midisynth *ms)
{
    pthread_mutex_lock(&ms->lock);
    if (ms->song) {
        if (ms->next == NULL) {    /* finished: start again */
            ms->next = ms->song;
            ms->msec = 0;
        }
        ms->playing = 1;
    }
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_pause(midisynth *ms)
{
    pthread_mutex_lock(&ms->lock);
    ms->playing = 0;
    tsf_note_off_all(ms->sf);
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_stop(midisynth *ms)
{
    pthread_mutex_lock(&ms->lock);
    ms->playing = 0;
    ms->next = ms->song;
    ms->msec = 0;
    tsf_reset(ms->sf);
    ms_init_channels(ms);
    pthread_mutex_unlock(&ms->lock);
}

int
midisynth_playing(midisynth *ms)
{
    int p;
    pthread_mutex_lock(&ms->lock);
    p = ms->playing;
    pthread_mutex_unlock(&ms->lock);
    return p;
}

void
midisynth_set_loop(midisynth *ms, int loop)
{
    pthread_mutex_lock(&ms->lock);
    ms->loop = loop;
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_set_volume(midisynth *ms, float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    pthread_mutex_lock(&ms->lock);
    ms->volume = volume;
    tsf_set_volume(ms->sf, MS_BASE_GAIN * volume);
    if (volume == 0.0f)            /* muted: drop every note now */
        ms_kill_voices(ms);
    pthread_mutex_unlock(&ms->lock);
}

/* One MIDI event. Called with the lock held. */
static void
ms_event(midisynth *ms, int type, int channel, int a, int b)
{
    tsf *sf = ms->sf;
    channel &= 15;
    switch (type) {
    case TML_NOTE_ON:
        if (b > 0) {
            /* When muted, notes aren't started at all, so nothing is
               rendered. Controllers etc. are still followed, so the
               music carries on correctly when the volume comes back. */
            if (ms->volume > 0.0f)
                tsf_channel_note_on(sf, channel, a & 127, (b & 127) / 127.0f);
            break;
        }
        /* note on with velocity 0 is a note off */
        /* fall through */
    case TML_NOTE_OFF:
        tsf_channel_note_off(sf, channel, a & 127);
        break;
    case TML_PROGRAM_CHANGE:
        tsf_channel_set_presetnumber(sf, channel, a & 127, channel == 9);
        break;
    case TML_CONTROL_CHANGE:
        tsf_channel_midi_control(sf, channel, a & 127, b & 127);
        break;
    case TML_PITCH_BEND:
        tsf_channel_set_pitchwheel(sf, channel, a & 16383);
        break;
    default:
        break;
    }
}

void
midisynth_render(midisynth *ms, int16_t *buffer, int frames, int mix)
{
    pthread_mutex_lock(&ms->lock);
    while (frames > 0) {
        int block = frames > TSF_RENDER_EFFECTSAMPLEBLOCK ? TSF_RENDER_EFFECTSAMPLEBLOCK : frames;

        if (ms->playing) {
            ms->msec += block * (1000.0 / ms->rate);
            while (ms->next && ms->msec >= ms->next->time) {
                tml_message *m = ms->next;
                if (m->type == TML_PITCH_BEND)
                    ms_event(ms, m->type, m->channel, m->pitch_bend, 0);
                else
                    ms_event(ms, m->type, m->channel, m->key, m->velocity);
                ms->next = m->next;
            }
            if (ms->next == NULL) {
                if (ms->loop && ms->song) {
                    ms->next = ms->song;
                    ms->msec = 0;
                } else {
                    ms->playing = 0;   /* song over; notes still ring out */
                }
            }
        }
        if (ms->volume > 0.0f) {
            tsf_render_short(ms->sf, (short *)buffer, block, mix);
            ms_cull(ms);
        } else if (!mix) {
            memset(buffer, 0, block * 2 * sizeof(int16_t));
        }
        buffer += block * 2;
        frames -= block;
    }
    pthread_mutex_unlock(&ms->lock);
}

/* Live MIDI */
void
midisynth_note_on(midisynth *ms, int channel, int key, int velocity)
{
    pthread_mutex_lock(&ms->lock);
    ms_event(ms, TML_NOTE_ON, channel, key, velocity);
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_note_off(midisynth *ms, int channel, int key)
{
    pthread_mutex_lock(&ms->lock);
    ms_event(ms, TML_NOTE_OFF, channel, key, 0);
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_program(midisynth *ms, int channel, int program)
{
    pthread_mutex_lock(&ms->lock);
    ms_event(ms, TML_PROGRAM_CHANGE, channel, program, 0);
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_control(midisynth *ms, int channel, int control, int value)
{
    pthread_mutex_lock(&ms->lock);
    ms_event(ms, TML_CONTROL_CHANGE, channel, control, value);
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_pitch_bend(midisynth *ms, int channel, int value)
{
    pthread_mutex_lock(&ms->lock);
    ms_event(ms, TML_PITCH_BEND, channel, value, 0);
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_all_notes_off(midisynth *ms)
{
    pthread_mutex_lock(&ms->lock);
    tsf_note_off_all(ms->sf);
    pthread_mutex_unlock(&ms->lock);
}

void
midisynth_send(midisynth *ms, int status, int data1, int data2)
{
    int type = status & 0xF0, channel = status & 0x0F;
    if (type == TML_PITCH_BEND)
        data1 = (data1 & 127) | ((data2 & 127) << 7);
    pthread_mutex_lock(&ms->lock);
    ms_event(ms, type, channel, data1, data2);
    pthread_mutex_unlock(&ms->lock);
}

#ifdef __riscos__
/* ---- Output through SharedSoundBuffer --------------------------------- */

/* Bytes queued but not yet played, or -1 on error. BufferStats returns
   R0 = bytes added so far, R1 = bytes played (the 2004 documentation says
   R0 = unplayed bytes; RDPClient uses R0 - R1). */
static int
ms_queued(midisynth *ms)
{
    _kernel_swi_regs r;
    r.r[0] = ms->stream;
    if (_kernel_swi(XStreamManager_BufferStats, &r, &r) != NULL)
        return -1;
    return r.r[0] - r.r[1];
}

static void
ms_ssb_pause(midisynth *ms, int play)
{
    _kernel_swi_regs r;
    r.r[0] = ms->ssb;
    r.r[1] = play ? SSB_PAUSE_RESUME : 0;
    _kernel_swi(XSharedSoundBuffer_Pause, &r, &r);
}

int
midisynth_output_open(midisynth *ms, const char *name)
{
    _kernel_swi_regs r;
    _kernel_oserror *e;
    const int block = MS_OUT_FRAMES * MS_FRAME_BYTES;

    if (ms->ssb)
        return 1;
    ms->outbuf = (int16_t *) malloc(block);
    if (ms->outbuf == NULL) {
        ms_seterror("Out of memory", NULL);
        return 0;
    }
    r.r[0] = SSB_OPEN_BLOCKSIZE;
    r.r[1] = (int)(name && *name ? name : "MIDISynth");
    r.r[2] = block;
    if ((e = _kernel_swi(XSharedSoundBuffer_OpenStream, &r, &r)) != NULL) {
        ms_seterror("SharedSoundBuffer isn't available", e->errmess);
        free(ms->outbuf);
        ms->outbuf = NULL;
        return 0;
    }
    ms->ssb = r.r[0];
    r.r[0] = ms->ssb;
    if ((e = _kernel_swi(XSharedSoundBuffer_ReturnStreamHandle, &r, &r)) != NULL) {
        ms_seterror("SharedSoundBuffer_ReturnStreamHandle", e->errmess);
        midisynth_output_close(ms);
        return 0;
    }
    ms->stream = r.r[0];
    /* Keep about MS_QUEUE_MS queued (at least two blocks): plenty for a
       Wimp program that is only polled now and then. StreamManager may
       hold a few times that, so a burst of polls can't overfill it. */
    ms->target = ms->rate * MS_FRAME_BYTES * MS_QUEUE_MS / 1000;
    if (ms->target < block * 2)
        ms->target = block * 2;
    /* The next three calls only fail if the stream is broken, which
       AddBlock will then report; so their errors aren't checked. */
    r.r[0] = ms->stream;
    r.r[1] = ms->target * MS_BUFFER_MAX;
    _kernel_swi(XStreamManager_SetBuffer, &r, &r);
    r.r[0] = ms->ssb;
    r.r[1] = ms->rate * SSB_RATE_UNIT;
    _kernel_swi(XSharedSoundBuffer_SampleRate, &r, &r);
    r.r[0] = ms->ssb;
    r.r[1] = (int)SSB_VOLUME_FULL;     /* our own volume is applied while rendering */
    _kernel_swi(XSharedSoundBuffer_Volume, &r, &r);
    ms_ssb_pause(ms, 0);               /* paused until two blocks are queued */
    ms->started = 0;
    return 1;
}

void
midisynth_output_poll(midisynth *ms)
{
    _kernel_swi_regs r;
    int queued, n;

    if (!ms->ssb)
        return;
    for (n = 0; n < 16; n++) {         /* at most 16 blocks per call */
        queued = ms_queued(ms);
        if (queued < 0 || queued >= ms->target)
            break;
        midisynth_render(ms, ms->outbuf, MS_OUT_FRAMES, 0);
        r.r[0] = ms->stream;
        r.r[1] = (int)ms->outbuf;
        r.r[2] = MS_OUT_FRAMES * MS_FRAME_BYTES;
        if (_kernel_swi(XStreamManager_AddBlock, &r, &r) != NULL)   /* copies the data */
            break;
    }
    if (!ms->started && ms_queued(ms) >= MS_OUT_FRAMES * MS_FRAME_BYTES * 2) {
        ms_ssb_pause(ms, 1);
        ms->started = 1;
    }
}

void
midisynth_output_close(midisynth *ms)
{
    if (ms->ssb) {
        _kernel_swi_regs r;
        r.r[0] = ms->ssb;
        _kernel_swi(XSharedSoundBuffer_CloseStream, &r, &r);
        ms->ssb = 0;
    }
    free(ms->outbuf);
    ms->outbuf = NULL;
}
#endif /* __riscos__ */

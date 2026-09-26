/* Fake RISC OS sound SWIs for the output tests: see fake_swi.h. Only the
   calls and registers midisynth uses are modelled. */
#include <stdint.h>
#include <string.h>
#include "kernel.h"
#include "fake_swi.h"

struct fake_state fake;

static _kernel_oserror err_unknown = { 0x1E6, "SWI not known" };
static _kernel_oserror err_full = { 0x81A144, "Buffer full" };
static _kernel_oserror err_state = { 1, "fake: called in the wrong state" };

/* Pointers passed in registers: the code under test swaps each for a
   token (MS_PTR in src/riscos_swi.h), and PTR() swaps it back. */
#define NTOKENS 64
static const void *tokens[NTOKENS];
static int next_token;

int
fake_ptr_token(const void *p)
{
    int t = next_token++ % NTOKENS;
    tokens[t] = p;
    return 0x40000000 | t;
}

static const void *
PTR(int r)
{
    if ((r & ~(NTOKENS - 1)) != 0x40000000)
        return NULL;               /* not a pointer we were given */
    return tokens[r & (NTOKENS - 1)];
}

void
fake_reset(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.ssb_present = 1;
    fake.dr_present = 1;
}

static void
keep_audio(const int16_t *data, long frames)
{
    if (fake.audio_frames + frames > FAKE_AUDIO_FRAMES)
        frames = FAKE_AUDIO_FRAMES - fake.audio_frames;
    memcpy(fake.audio + fake.audio_frames * 2, data, frames * 4);
    fake.audio_frames += frames;
}

void
fake_play(long frames)
{
    if (fake.ssb_open && !fake.ssb_paused) {
        fake.sm_played += frames * 4;
        if (fake.sm_played > fake.sm_added)
            fake.sm_played = fake.sm_added;
    }
    if (fake.dr_active && fake.dr_bufframes) {
        fake.dr_played += frames;              /* whole buffers finish */
        while (fake.dr_played >= fake.dr_bufframes && fake.dr_queued > 0) {
            fake.dr_played -= fake.dr_bufframes;
            fake.dr_queued--;
        }
        if (fake.dr_queued == 0)
            fake.dr_played = 0;
    }
}

static _kernel_oserror *
ssb(int n, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    const int handle = 0x5AB;      /* our one stream */
    if (!fake.ssb_present)
        return &err_unknown;
    if (n != 0x55FC0 && (!fake.ssb_open || in->r[0] != handle))
        return &err_state;
    switch (n) {
    case 0x55FC0:                  /* OpenStream */
        fake.ssb_open = 1;
        fake.ssb_flags = in->r[0];
        strncpy(fake.ssb_name, PTR(in->r[1]) ? (const char *)PTR(in->r[1]) : "(bad pointer)", sizeof(fake.ssb_name) - 1);
        fake.ssb_blocksize = in->r[2];
        out->r[0] = handle;
        return NULL;
    case 0x55FC1: fake.ssb_open = 0; fake.ssb_closed++; return NULL;           /* CloseStream */
    case 0x55FC4: fake.ssb_volume = in->r[1]; return NULL;                    /* Volume */
    case 0x55FC5: fake.ssb_rate = in->r[1]; return NULL;                      /* SampleRate */
    case 0x55FC9: fake.ssb_paused = !(in->r[1] & 1); return NULL;             /* Pause */
    case 0x55FCE: out->r[0] = 0x57A; return NULL;                             /* ReturnStreamHandle */
    }
    return &err_unknown;
}

static _kernel_oserror *
sm(int n, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    if (!fake.ssb_present)
        return &err_unknown;
    if (!fake.ssb_open || in->r[0] != 0x57A)
        return &err_state;
    switch (n) {
    case 0x57282:                  /* AddBlock: copies the data */
        if (!PTR(in->r[1]))
            return &err_state;
        if (fake.sm_refuse || (fake.sm_limit && fake.sm_added - fake.sm_played + in->r[2] > fake.sm_limit))
            return &err_full;
        keep_audio(PTR(in->r[1]), in->r[2] / 4);
        fake.sm_added += in->r[2];
        return NULL;
    case 0x57287: fake.sm_limit = in->r[1]; return NULL;                      /* SetBuffer */
    case 0x57288:                  /* BufferStats: R0 added, R1 played */
        out->r[0] = (int)fake.sm_added;
        out->r[1] = (int)fake.sm_played;
        return NULL;
    }
    return &err_unknown;
}

static _kernel_oserror *
dr(int n, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    if (!fake.dr_present)
        return &err_unknown;
    switch (n) {
    case 1: fake.dr_active = 0; fake.dr_deactivated++; return NULL;          /* Deactivate */
    case 5: out->r[0] = (fake.dr_active || fake.dr_busy) ? 1 : 0; return NULL; /* ReadState */
    case 9: fake.dr_numbuffers = in->r[0]; out->r[0] = in->r[0]; return NULL; /* NumBuffers */
    case 11:                       /* Stream16BitSamples: R1 samples */
        if (!fake.dr_active || fake.dr_format != 3 || in->r[1] != fake.dr_bufframes * 2 || !PTR(in->r[0]))
            return &err_state;
        keep_audio(PTR(in->r[0]), in->r[1] / 2);
        fake.dr_queued++;
        return NULL;
    case 12: out->r[0] = fake.dr_queued; return NULL;                         /* StreamStatistics */
    case 13: out->r[0] = fake.dr_flags; fake.dr_flags = (fake.dr_flags & in->r[1]) ^ in->r[0]; return NULL;
    case 15:                       /* Activate16 */
        if (in->r[0] != 2 || !fake.dr_numbuffers)
            return &err_state;
        fake.dr_active = 1;
        fake.dr_bufframes = in->r[1];
        fake.dr_asked_rate = in->r[2];
        return NULL;
    case 16: out->r[0] = fake.dr_active ? (fake.dr_rate ? fake.dr_rate : fake.dr_asked_rate) : 0; return NULL;
    case 18: fake.dr_format = in->r[0]; out->r[0] = in->r[0]; return NULL;   /* SampleFormat */
    }
    return &err_unknown;
}

_kernel_oserror *
_kernel_swi(int no, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    _kernel_swi_regs r = *in;      /* in and out may be the same */
    _kernel_oserror *e;
    int n = no & ~0x20000;
    if (!(no & 0x20000))
        return &err_state;         /* midisynth always uses the X form */
    if (n >= 0x55FC0 && n <= 0x55FCF)
        e = ssb(n, &r, out);
    else if (n >= 0x57280 && n <= 0x5728F)
        e = sm(n, &r, out);
    else if (n >= 0x4F700 && n <= 0x4F73F)
        e = dr(n - 0x4F700, &r, out);
    else
        e = &err_unknown;
    return e;
}

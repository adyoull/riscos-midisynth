#!/usr/bin/env python3
"""Make the files the tests use, so they need nothing from outside:

    mktestfiles.py <dir>

  test.sf2    a tiny SoundFont: a looped 441 Hz sine wave as bank 0
              preset 0, and an octave higher as the drum kit (bank 128
              preset 0); both with a half-second release
  song.mid    a scale, a chord with running status, a drum hit, a pitch
              bend and a tempo change (format 1, two tracks)
  bad-*.mid   files that must be refused: cut short, random bytes, empty
"""
import math, os, random, struct, sys

# ---- SoundFont 2 -----------------------------------------------------------
def chunk(cid, data):
    pad = b'\0' if len(data) & 1 else b''
    return cid + struct.pack('<I', len(data)) + data + pad

def listchunk(kind, *chunks):
    return chunk(b'LIST', kind + b''.join(chunks))

def name20(s):
    return s.encode().ljust(20, b'\0')

def make_sf2():
    rate, period, cycles = 44100, 100, 100          # 441 Hz sine
    n = period * cycles
    pcm = [int(12000 * math.sin(2 * math.pi * i / period)) for i in range(n)]
    smpl = struct.pack('<%dh' % (n + 46), *(pcm + [0] * 46))   # 46 zeros after each sample

    info = listchunk(b'INFO', chunk(b'ifil', struct.pack('<HH', 2, 1)),
                     chunk(b'isng', b'EMU8000\0'), chunk(b'INAM', b'midisynth test\0'))
    sdta = listchunk(b'sdta', chunk(b'smpl', smpl))

    # presets: 0 = bank 0 preset 0, 1 = bank 128 preset 0 (drums); then EOP
    phdr = b''.join(name20(nm) + struct.pack('<HHHIII', pre, bank, bag, 0, 0, 0)
                    for nm, pre, bank, bag in (('Sine', 0, 0, 0), ('Drums', 0, 128, 1), ('EOP', 0, 0, 2)))
    pbag = struct.pack('<HH', 0, 0) + struct.pack('<HH', 1, 0) + struct.pack('<HH', 2, 0)
    pmod = b'\0' * 10
    GEN_INSTRUMENT, GEN_RELEASE_VOL, GEN_COARSE_TUNE, GEN_SAMPLE_MODES, GEN_SAMPLE_ID = 41, 38, 51, 54, 53
    # preset 0 uses instrument 0, the drum kit instrument 1
    pgen = struct.pack('<HH', GEN_INSTRUMENT, 0) + struct.pack('<HH', GEN_INSTRUMENT, 1) + struct.pack('<HH', 0, 0)
    inst = (name20('Sine') + struct.pack('<H', 0) + name20('Drum') + struct.pack('<H', 1)
            + name20('EOI') + struct.pack('<H', 2))
    ibag = struct.pack('<HH', 0, 0) + struct.pack('<HH', 3, 0) + struct.pack('<HH', 7, 0)
    imod = b'\0' * 10
    sine = (struct.pack('<Hh', GEN_RELEASE_VOL, -1200)        # 0.5 s release
            + struct.pack('<HH', GEN_SAMPLE_MODES, 1)          # loop
            + struct.pack('<HH', GEN_SAMPLE_ID, 0))
    # the drum kit is the same sine an octave up, so the tests can tell
    # channel 10 (the drums) from the others
    drum = struct.pack('<Hh', GEN_COARSE_TUNE, 12) + sine
    igen = sine + drum + struct.pack('<HH', 0, 0)
    shdr = (name20('Sine') + struct.pack('<IIIIIBbHH', 0, n, 0, n, rate, 69, 0, 0, 1)
            + name20('EOS') + b'\0' * 26)
    pdta = listchunk(b'pdta', chunk(b'phdr', phdr), chunk(b'pbag', pbag), chunk(b'pmod', pmod),
                     chunk(b'pgen', pgen), chunk(b'inst', inst), chunk(b'ibag', ibag),
                     chunk(b'imod', imod), chunk(b'igen', igen), chunk(b'shdr', shdr))
    return chunk(b'RIFF', b'sfbk' + info + sdta + pdta)

# ---- MIDI --------------------------------------------------------------------
def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)

def track(events):
    data = b''.join(vlq(dt) + bytes(ev) for dt, ev in events) + vlq(0) + b'\xFF\x2F\x00'
    return b'MTrk' + struct.pack('>I', len(data)) + data

def make_song():
    tempo = [(0, [0xFF, 0x51, 3, 0x07, 0xA1, 0x20]),          # 500000 us per beat
             (480 * 4, [0xFF, 0x51, 3, 0x03, 0xD0, 0x90])]     # then twice as fast
    music = [(0, [0xC0, 0]), (0, [0xB0, 7, 100])]
    for k in (60, 62, 64, 65, 67):
        music += [(0, [0x90, k, 100]), (240, [0x80, k, 0])]
    music += [(0, [0x90, 60, 90]), (0, [64, 90]), (0, [67, 90]),   # running status
              (480, [0x90, 60, 0]), (0, [64, 0]), (0, [67, 0])]      # velocity 0 = off
    music += [(0, [0x99, 36, 120]), (240, [0x89, 36, 0])]
    music += [(0, [0x90, 69, 100]), (120, [0xE0, 0x00, 0x60]), (240, [0x80, 69, 0])]
    return b'MThd' + struct.pack('>IHHH', 6, 1, 2, 480) + track(tempo) + track(music)

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    song = make_song()
    rnd = random.Random(1)
    files = {'test.sf2': make_sf2(), 'song.mid': song,
             'bad-short.mid': song[:40],
             'bad-random.mid': bytes(rnd.randrange(256) for _ in range(2000)),
             'bad-empty.mid': b''}
    for name, data in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)

if __name__ == '__main__':
    main()

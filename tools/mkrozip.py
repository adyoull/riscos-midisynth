#!/usr/bin/env python3
"""Make a zip of RISC OS files, keeping their filetypes.

    mkrozip.py <out.zip> <base dir> <path> [path ...]

Each path (relative to the base dir) is added with everything under it.
Files named like "name,xxx" are stored as "name" with filetype &xxx;
other files get &FFF (Text). The filetype goes in the "ARC0" extra field
(0x4341), which SparkFS, unzip on RISC OS and GCCSDK's "zip -," all use.

The zip is the same every time for the same files: entries are sorted,
and all dates are SOURCE_DATE_EPOCH if that is set (else the newest file's
date). This replaces GCCSDK's "zip -,", which isn't always installed.
Never add files to an existing zip with "zip -,": it duplicates entries.
"""
import os, re, struct, sys, time, zipfile

ARC0_ID = 0x4341
ATTR_RW_R = 0x13            # owner read/write, public read
SUFFIX = re.compile(r'^(.*),([0-9a-fA-F]{3})$')
RISCOS_EPOCH = 2208988800   # seconds from 1900-01-01 to 1970-01-01


def arc0(filetype, unix_time):
    """ARC0 extra field: load/exec addresses (filetype + 5-byte date) and attributes."""
    cs = (unix_time + RISCOS_EPOCH) * 100          # centiseconds since 1900
    load = 0xFFF00000 | (filetype << 8) | ((cs >> 32) & 0xFF)
    body = b'ARC0' + struct.pack('<IIII', load, cs & 0xFFFFFFFF, ATTR_RW_R, 0)
    return struct.pack('<HH', ARC0_ID, len(body)) + body


def entries(base, paths):
    """(name in zip, path on disk or None for a directory, filetype)"""
    out = []
    for top in paths:
        for root, dirs, files in os.walk(os.path.join(base, top)):
            dirs.sort()
            rel = os.path.relpath(root, base)
            out.append((rel + '/', None, 0xFFF))
            for f in sorted(files):
                m = SUFFIX.match(f)
                name, ftype = (m.group(1), int(m.group(2), 16)) if m else (f, 0xFFF)
                out.append((rel + '/' + name, os.path.join(root, f), ftype))
    return out


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    zpath, base, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
    items = entries(base, paths)
    if 'SOURCE_DATE_EPOCH' in os.environ:
        when = int(os.environ['SOURCE_DATE_EPOCH'])
    else:
        when = int(max(os.path.getmtime(p) for _, p, _ in items if p))
    date_time = time.gmtime(when)[:6]
    with zipfile.ZipFile(zpath, 'w') as z:
        for name, path, ftype in items:
            info = zipfile.ZipInfo(name, date_time)
            info.create_system = 3
            info.extra = arc0(ftype, when)
            if path is None:
                info.external_attr = 0o40755 << 16
                z.writestr(info, b'')
            else:
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(path, 'rb') as f:
                    z.writestr(info, f.read(), compresslevel=9)
    print(f'{zpath}: {sum(1 for _, p, _ in items if p)} files')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Mux a main mix plus four stems into a Native Instruments Stems .stem.mp4.

Mixxx recognises a stem file by looking for the MP4 atom path moov/udta/stem
(see src/track/steminfoimporter.cpp). That atom holds a JSON manifest:

    {"version": 1, "stems": [{"name": "Drums", "color": "#fd4a4a"}, ...]}

Stream 0 of the file is the main mix, streams 1..4 are the stems in manifest
order. ffmpeg can mux the five streams but cannot write an arbitrary atom, so
the manifest is patched in afterwards.

Usage:
    stemmp4.py <main.wav> <drums.wav> <bass.wav> <other.wav> <vocals.wav> <out.stem.mp4>
"""

import json
import struct
import subprocess
import sys


def box(typ, payload):
    """Serialise one MP4 atom: 4-byte big endian size, 4-byte type, payload."""
    return struct.pack('>I', 8 + len(payload)) + typ.encode('latin1') + payload


def atoms(buf, start=0, end=None):
    """Yield (offset, size, type) for each atom in buf[start:end]."""
    if end is None:
        end = len(buf)
    pos = start
    while pos + 8 <= end:
        size = struct.unpack_from('>I', buf, pos)[0]
        typ = buf[pos + 4:pos + 8].decode('latin1')
        if size == 1:
            size = struct.unpack_from('>Q', buf, pos + 8)[0]
        elif size == 0:
            size = end - pos
        if size < 8:
            return
        yield pos, size, typ
        pos += size


def write_manifest(path, manifest):
    with open(path, 'rb') as f:
        buf = bytearray(f.read())

    moov = None
    for pos, size, typ in atoms(buf):
        if typ == 'moov':
            moov = (pos, size)
    if moov is None:
        raise SystemExit('no moov atom in ' + path)
    mpos, msize = moov
    if mpos + msize != len(buf):
        raise SystemExit('moov is not the last atom in ' + path +
                '; patching it would invalidate the chunk offsets in mdat')
    payload = bytes(buf[mpos + 8:mpos + msize])

    stem_box = box('stem', manifest.encode('utf-8'))

    udta = None
    for pos, size, typ in atoms(payload):
        if typ == 'udta':
            udta = (pos, size)
            break

    if udta is None:
        new_payload = payload + box('udta', stem_box)
    else:
        upos, usize = udta
        kept = b''.join(
                payload[upos + 8 + p:upos + 8 + p + s]
                for p, s, t in atoms(payload[upos + 8:upos + usize])
                if t != 'stem')
        new_payload = payload[:upos] + box('udta', kept + stem_box) + payload[upos + usize:]

    with open(path, 'wb') as f:
        f.write(bytes(buf[:mpos]) + box('moov', new_payload))


def build(main, drums, bass, other, vocals, out):
    cmd = ['ffmpeg', '-y', '-hide_banner', '-loglevel', 'error', '-i', main]
    for s in (drums, bass, other, vocals):
        cmd += ['-i', s]
    for i in range(5):
        cmd += ['-map', '%d:a:0' % i]
    # Carry the source's tags across explicitly rather than relying on ffmpeg
    # defaulting to input 0's metadata.
    cmd += ['-map_metadata', '0']
    # Carry the cover art across too. An MP3 keeps it in an ID3 frame that
    # ffmpeg exposes as a video stream, and the MP4 muxer writes a mapped
    # attached picture as the covr atom that players read. The trailing ? makes
    # the mapping optional, so a track without art still produces a container.
    cmd += ['-map', '0:v:0?', '-c:v', 'copy', '-disposition:v:0', 'attached_pic']
    cmd += ['-c:a', 'alac', out]
    subprocess.run(cmd, check=True)

    manifest = json.dumps({
        'version': 1,
        'stems': [
            {'name': 'Drums', 'color': '#fd4a4a'},
            {'name': 'Bass', 'color': '#ffff00'},
            {'name': 'Other', 'color': '#00e8e8'},
            {'name': 'Vocals', 'color': '#ad65ff'},
        ],
    }, indent=2)
    write_manifest(out, manifest)
    return out


if __name__ == '__main__':
    if len(sys.argv) != 7:
        raise SystemExit('usage: stemmp4.py <main> <drums> <bass> <other> <vocals> <out.stem.mp4>')
    build(*sys.argv[1:])
    print(sys.argv[6])

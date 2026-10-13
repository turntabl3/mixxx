#!/usr/bin/env python3
"""Separate a track into four stems and write a Mixxx-loadable .stem.mp4.

Runs demucs (https://github.com/facebookresearch/demucs) over the track, then
muxes mains mix + stems into the NI Stems container that Mixxx already knows how
to play (see tools/stems/stemmp4.py).

Needs demucs, which is not a Mixxx build dependency, so point the script at an
interpreter that has it, e.g.:

    python3 -m venv ~/.venvs/stems
    ~/.venvs/stems/bin/pip install torch torchaudio --index-url https://download.pytorch.org/whl/cpu
    ~/.venvs/stems/bin/pip install demucs numpy soundfile
    ~/.venvs/stems/bin/python tools/stems/separate.py <track> [out.stem.mp4]

Separation is roughly 0.4x realtime for htdemucs on a 12-core laptop CPU, so a
three minute track takes a few minutes. That is why stems are produced offline
and cached, and the deck then plays the cache -- the same shape as Serato Stems
and djay Pro, both of which separate ahead of time and only mix the stems live.
"""

import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stemmp4  # noqa: E402

MODEL = 'htdemucs'
STEM_NAMES = ('drums', 'bass', 'other', 'vocals')


def separate(track, out=None):
    out = out or os.path.splitext(track)[0] + '.stem.mp4'
    tmp = tempfile.mkdtemp(prefix='mixxx-stems-')
    try:
        subprocess.run(
                [sys.executable, '-m', 'demucs', '-n', MODEL, '--out', tmp, track],
                check=True)
        stem_dir = os.path.join(
                tmp, MODEL, os.path.splitext(os.path.basename(track))[0])
        stems = [os.path.join(stem_dir, n + '.wav') for n in STEM_NAMES]
        for s in stems:
            if not os.path.exists(s):
                raise SystemExit('demucs did not produce ' + s)
        stemmp4.build(track, *stems, out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return out


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        raise SystemExit('usage: separate.py <track> [out.stem.mp4]')
    print(separate(*sys.argv[1:]))

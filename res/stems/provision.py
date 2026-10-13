#!/usr/bin/env python3
"""Create the Python environment that separates tracks into stems.

Mixxx runs stem separation through demucs, which is far too large to ship with
the application (a CPU build of PyTorch alone is a few hundred megabytes). So
instead of asking the user to assemble a virtual environment by hand, Mixxx
creates one for itself: Preferences > Stems runs this script with a system
interpreter and then points the separation program at

    <target-dir>/venv/bin/python

Usage:
    python3 provision.py <target-dir>

The target directory is normally the "stems" directory inside Mixxx's settings
directory. Re-running is safe: an existing environment is reused and its
packages are brought up to date.
"""

import os
import shutil
import subprocess
import sys
import venv

# PyTorch's CPU index. The default PyPI wheels for x86-64 drag in several
# gigabytes of CUDA libraries that a separation job on the CPU never uses. If
# this index has no wheel for the platform we fall back to plain PyPI, where
# the aarch64 wheels are CPU builds anyway.
TORCH_CPU_INDEX = 'https://download.pytorch.org/whl/cpu'

# Both versions are pinned, and both pins are load-bearing. What matters is that
# installing them needs nothing but pip: no compiler, no Rust, no CUDA.
#
# torchaudio 2.9 and later dropped their own audio decoders: torchaudio.load now
# goes through TorchCodec, which has to find FFmpeg's shared libraries at
# runtime. demucs 4.0.1 loads tracks with torchaudio.load, so the last pair that
# decodes audio by itself is the one to use.
TORCH_VERSION = '2.6.0'
# demucs 4.1.0 needs sphn, which publishes wheels only for macOS arm64, Linux
# x86-64 and Windows x64. On arm64 Linux and arm64 Windows pip would build it
# from source and fail without a Rust toolchain, which is not something to
# require of a machine that is only trying to separate a track. 4.0.1 depends on
# nothing compiled beyond PyTorch itself.
PACKAGES = ('demucs==4.0.1', 'soundfile')


def interpreter(venv_dir):
    """Path to the interpreter inside a virtual environment."""
    if os.name == 'nt':
        return os.path.join(venv_dir, 'Scripts', 'python.exe')
    return os.path.join(venv_dir, 'bin', 'python')


def run(cmd):
    print('$ ' + ' '.join(cmd), flush=True)
    return subprocess.call(cmd)


def pip(python, *args):
    return run([python, '-m', 'pip', 'install',
                '--disable-pip-version-check', '--no-input', *args])


def install_torch(python):
    torch_pin = 'torch==' + TORCH_VERSION
    audio_pin = 'torchaudio==' + TORCH_VERSION
    cpu_index = pip(python, torch_pin, audio_pin, '--index-url', TORCH_CPU_INDEX)
    if cpu_index == 0:
        return
    print('CPU index failed, retrying from PyPI', flush=True)
    if pip(python, torch_pin, audio_pin) != 0:
        raise SystemExit('could not install torch/torchaudio')


def main(argv):
    if len(argv) != 1:
        raise SystemExit('usage: provision.py <target-dir>')
    root = os.path.abspath(argv[0])
    venv_dir = os.path.join(root, 'venv')
    python = interpreter(venv_dir)

    if not os.path.exists(python):
        os.makedirs(root, exist_ok=True)
        print('creating virtual environment in ' + venv_dir, flush=True)
        try:
            venv.create(venv_dir, with_pip=True)
        except Exception as e:
            raise SystemExit(
                    'could not create a virtual environment: %s\n'
                    'On Debian/Ubuntu this usually means the python3-venv '
                    'package is missing.' % e)

    # ffmpeg is a real requirement, not a nicety: the muxer that writes the
    # .stem.mp4 container is itself an ffmpeg command, and demucs reads some
    # formats through it. It is not something pip can install, so say so now --
    # otherwise the failure surfaces minutes later, at the end of a separation,
    # looking unrelated to anything the user did.
    if shutil.which('ffmpeg') is None:
        print('warning: ffmpeg is not on PATH. Stem separation needs it: the '
              'container is written by an ffmpeg command. Install ffmpeg (any '
              'recent build) and make sure it is on PATH.', flush=True)

    install_torch(python)
    if pip(python, *PACKAGES) != 0:
        raise SystemExit('could not install ' + ', '.join(PACKAGES))

    print(python)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

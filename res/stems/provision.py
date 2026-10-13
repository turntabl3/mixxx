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
PACKAGES = ('demucs', 'soundfile')


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
    cpu_index = pip(python, 'torch', 'torchaudio', '--index-url', TORCH_CPU_INDEX)
    if cpu_index == 0:
        return
    print('CPU index failed, retrying from PyPI', flush=True)
    if pip(python, 'torch', 'torchaudio') != 0:
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

    # Separation reads and writes audio through ffmpeg, which demucs finds on
    # PATH; nothing to install for it, but say so clearly when it is absent,
    # because the failure is otherwise reported much later and looks unrelated.
    if shutil.which('ffmpeg') is None:
        print('warning: ffmpeg is not on PATH; separation will fail',
                flush=True)

    install_torch(python)
    if pip(python, *PACKAGES) != 0:
        raise SystemExit('could not install ' + ', '.join(PACKAGES))

    print(python)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

# Stem separation tooling

Offline stem separation for Mixxx: takes a track, produces a Native Instruments
Stems compatible `.stem.mp4` that Mixxx already knows how to play.

## Why offline

Serato Stems and djay Pro do not separate audio live. Both run separation ahead
of time and cache the result; the "realtime" behaviour is the deck mixing the
already-separated stems. This is the same shape: `separate.py` produces the
container once, and Mixxx's existing stem playback path does the rest.

Measured here: htdemucs on a 12-core arm64 laptop CPU is about 0.4x realtime
(~2.4 s of audio per second of wall clock), so a three minute track takes a few
minutes. Live separation of arbitrary audio in the engine thread is not on the
table -- the engine thread must not allocate or lock, and the decode path has no
windowed model inference.

## Pieces

`stemmp4.py` muxes a main mix plus four stems into the container. Mixxx detects a
stem file by probing the MP4 atom path `moov/udta/stem`, whose payload is a JSON
manifest (`src/track/steminfoimporter.cpp`):

```json
{"version": 1,
 "stems": [{"name": "Drums", "color": "#fd4a4a"},
           {"name": "Bass",  "color": "#ffff00"},
           {"name": "Other", "color": "#00e8e8"},
           {"name": "Vocals","color": "#ad65ff"}]}
```

Stream 0 of the container is the main mix; streams 1..4 are the stems in
manifest order. ffmpeg muxes the five streams but cannot write an arbitrary
atom, so the manifest is patched into `moov` afterwards. Patching is refused
when `moov` is not the last atom, because growing it would invalidate the `stco`
chunk offsets pointing into `mdat`.

`separate.py` runs demucs over a track and hands the four stems to `stemmp4.py`.

## Setup

Nothing to install by hand. Preferences > Stems has an "Install separation
environment" button; it creates an environment under Mixxx's settings directory
(`<settings>/stems/venv`) and installs PyTorch and demucs into it.

PyTorch is a few hundred megabytes, so it is not shipped with the application,
and this is a one-off download. If the machine has no Python at all -- the
normal state of a Windows install -- the application fetches a stand-alone
CPython build into its own directory first, using curl (or wget) and tar, which
both platforms already have. Nothing is installed system-wide and no
administrator rights are needed.

`stemmp4.py`, `separate.py` and `provision.py` are compiled into the executable
rather than shipped as files, so an installation is the application alone.
Copies are written out beside the environment when they are needed.

The CPU index matters: the default PyPI wheels pull the CUDA stack, several GB
of no use on a machine without an NVIDIA GPU.

To build an environment by hand instead, for debugging:

```sh
python3 -m venv ~/.venvs/stems
~/.venvs/stems/bin/pip install torch torchaudio --index-url https://download.pytorch.org/whl/cpu
~/.venvs/stems/bin/pip install demucs numpy soundfile
```

## Usage

Through the application:

- Right-click a track > Separate stems, for a single track.
- Library > Prepare Stems for Library, for every track that does not have stems
  yet. Once a track's container is written, its original is moved to the trash
  so the container takes its place; the cover art is carried over.

The scripts are written out beside the environment, so the same thing by hand is:

```sh
<settings>/stems/venv/bin/python <settings>/stems/separate.py /path/to/track.flac
# -> /path/to/track.stem.mp4
```

Either way the stem controls then appear as for any NI Stems file: per-stem
gain, mute, quick effects, per-stem waveform rows.

`demucs` weights are MIT licensed (`htdemucs`), so they are usable here. Check
the licence before substituting a different separation model -- several
popular UVR models are not redistributable.

## Verifying

Mixxx's own importer tests cover the format (`src/test/stemtest.cpp`,
`src/test/steminfotest.cpp`) and the fixtures under `src/test/stems/stem01/`.
A produced file can be checked the way Mixxx checks it without a full build --
`StemInfoImporter` only needs Qt6Core/Qt6Gui:

```sh
g++ -std=c++20 -Isrc harness.cpp \
    src/track/steminfoimporter.cpp src/track/steminfo.cpp \
    src/util/logger.cpp src/util/logging.cpp \
    $(pkg-config --cflags --libs Qt6Gui) -o harness
```

(plus a two-line stub for `CmdlineArgs::CmdlineArgs()`, since `logging.cpp`
references it but never calls it on this path).

## Status

Working end to end: the pipeline produces a container Mixxx loads, the
application provisions the environment it needs, and both the per-track and
library-wide actions drive it.

Done:

- `separate.py` / `stemmp4.py` produce a container Mixxx loads, carrying the
  source's cover art.
- The scripts are compiled into the executable; nothing is installed as a loose
  file, and `res/stems` is no longer part of the install.
- `mixxx::StemSeparator` runs the pipeline, validates the result and names the
  offending path when something is missing.
- `StemSeparationSettings` holds interpreter, script, model and output path, and
  falls back to the bundled defaults when a stored path has gone missing.
- A preference page (Preferences > Stems) exposes them, installs the
  environment, and checks the paths as you type.
- `mixxx::StemSeparationJob` separates one track on a worker thread.
- "Separate stems" in the track menu.
- "Prepare Stems for Library" in the Library menu: finds every track with no
  container, separates them one at a time with progress and a stop button, and
  moves each original to the trash once its container is written.
- `mixxx::StemProvisionJob` installs the environment, fetching a stand-alone
  Python when the machine has none.

Known limits:

1. Stopping a library-wide run takes effect between tracks. A separation in
   progress cannot be interrupted: the backend is a child process with no
   cancellation support.
2. Progress is a per-track count, not a percentage within a track.
3. The library needs a rescan after a library-wide run, because the files it
   points at have been replaced. The summary says so.

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

demucs is not a Mixxx build dependency, so keep it in its own environment:

```sh
python3 -m venv ~/.venvs/stems
~/.venvs/stems/bin/pip install torch torchaudio --index-url https://download.pytorch.org/whl/cpu
~/.venvs/stems/bin/pip install demucs numpy soundfile
```

`--index-url .../whl/cpu` matters: the default index pulls the CUDA stack, which
is several GB of no use on a machine without an NVIDIA GPU.

## Usage

```sh
~/.venvs/stems/bin/python tools/stems/separate.py /path/to/track.flac
# -> /path/to/track.stem.mp4
```

Load the result in Mixxx and the stem controls appear as for any NI Stems file:
per-stem gain, mute, quick effects, per-stem waveform rows.

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

The pipeline and the pieces that drive it are in place; Mixxx does not call it
yet.

Done:

- `tools/stems/` produces a container Mixxx loads.
- `mixxx::StemSeparator` runs that pipeline and validates the result, and
  passes the configured model through.
- `StemSeparationSettings` holds interpreter, script, model and output path.
- A preference page (Preferences > Stems) exposes them, with the paths checked
  as you type rather than only when a separation fails.

Still to do:

1. A job that runs `StemSeparator` on a worker thread when the user asks for a
   track. It must not touch the engine thread; the deck keeps playing the
   original file while separation runs.
2. Making the deck load the container. Once a container exists for the loaded
   track it has to be opened instead of the original file, so that
   `SoundSourceSTEM` hands the deck four stereo pairs and the existing per-stem
   controls appear. This needs a track -> container mapping: either a field on
   the track record, or a deterministic cache path derived from the track
   location. A deterministic path is preferable -- it leaves the library
   untouched and the original file is never replaced.
3. Waveform. Per-stem waveform rows come from `analyzerwaveform`, which derives
   the stem count from the channel count. The container therefore has to be
   analysed, not the original, or the waveform shows a single row.
4. A UI trigger. Nothing in the app invokes a separation yet, so today you run
   `separate.py` by hand and load the result as an ordinary library track.


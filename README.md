# Slice-able

A sampler plug-in that slices a sample on the musical grid, the way **Separate Clip at Grid** works in Pro Tools and **Slice at Grid** works in Logic Pro.

Load a loop and pick a grid division. The sample is cut at every grid line, and each slice is mapped to its own MIDI note. Play the slices from a keyboard or a MIDI clip, or drag any slice straight onto your DAW's timeline as an audio file.

Built with [JUCE](https://juce.com). Formats:

| Platform | Formats | Architectures |
|----------|---------|---------------|
| macOS 11+ | VST3, AU, Standalone | Universal (Apple Silicon + Intel) |
| Windows 10+ | VST3, Standalone | x64 |

The AU is there because Logic Pro only loads Audio Units, not VST3. Pro Tools needs AAX, which isn't built yet.

## Features

- **The grid follows your session.** By default the grid uses the host's tempo and time signature. You can also type a BPM, or use **Fit to Bars** for loops of unknown tempo: tell it the sample is N bars long and it works out the tempo.
- **Divisions:** 4 bars, 2 bars, 1 bar, and 1/2 through 1/64, including dotted and triplet values.
- **Grid Offset:** slides the grid so it lines up with the downbeat. Use the knob, or **Alt/Option-drag** on the waveform. If the sample starts before the first grid line, that part becomes its own pickup slice, just as in a DAW.
- **One slice per note:** slice 1 plays on the **Root** note (C1 by default), slice 2 on the next note up, and so on, up to 128 slices.
- **Playback:**
  - One Shot or Gate.
  - Poly or Mono (choke).
  - Reverse, Attack, Release, Pitch (±24 semitones), Gain.
  - Short anti-click fades at the slice edges.
- **Click a slice** to audition it.
- **Drag a slice out** of the waveform onto a DAW track. The slice is written as a WAV to `~/Music/Slice-able/Slices/<sample name>/`, so it stays put for DAWs that reference dropped files rather than copying them.
- **Export Slices…** writes every slice to a folder as WAV files named like `Loop_03_D1.wav`.
- **Sample formats:** WAV, AIFF, FLAC, MP3 and Ogg, plus anything Core Audio can read on macOS.

Sessions save the sample's file path. If the file moves, the plug-in shows the missing path so you can load it again.

## Download

Every push builds the plug-in in GitHub Actions. Open the **Actions** tab, pick the latest successful **Build** run, and download one of these artifacts:

- `Slice-able-macOS-Universal`
- `Slice-able-Windows-x64`

Install locations:

| Format | macOS | Windows |
|--------|-------|---------|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` |
| AU | `~/Library/Audio/Plug-Ins/Components/` | n/a |

The macOS builds are ad-hoc signed but not notarised. After downloading, clear the quarantine flag so Logic and other hosts will load the plug-in:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Slice-able.component ~/Library/Audio/Plug-Ins/VST3/Slice-able.vst3
```

## Building from source

Requirements:

- CMake 3.22+
- A C++20 compiler: Xcode 15+ on macOS, Visual Studio 2022+ on Windows
- Git

The first configure downloads JUCE automatically.

### macOS (Universal Binary by default)

Configure:

```bash
cmake -S . -B build -G Xcode
```

Build:

```bash
cmake --build build --config Release
```

To build for one architecture only, add `-DCMAKE_OSX_ARCHITECTURES=arm64` to the configure command.

### Windows

Configure:

```bash
cmake -S . -B build -A x64
```

Build:

```bash
cmake --build build --config Release
```

### Linux (development only)

Install the dependencies:

```bash
sudo apt-get install libasound2-dev libxi-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libfontconfig1-dev libfreetype-dev
```

Configure:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build:

```bash
cmake --build build
```

### Output and options

Built plug-ins end up in `build/SliceAble_artefacts/Release/`.

| Option | Default | What it does |
|--------|---------|--------------|
| `-DSLICEABLE_COPY_AFTER_BUILD=ON` | `OFF` | Installs the plug-ins into your system plug-in folders after each build |
| `-DSLICEABLE_BUILD_TESTS=OFF` | `ON` | Skips building the unit tests |
| `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` | n/a | Uses a local JUCE checkout instead of downloading one |

### Tests

These cover the grid maths, the playback engine and WAV export.

```bash
ctest --test-dir build -C Release --output-on-failure
```

In CI, the plug-ins are also checked with [pluginval](https://github.com/Tracktion/pluginval) on macOS and Windows, and with `auval` for the AU.

## Code layout

| Path | Purpose |
|------|---------|
| `source/SliceGrid.*` | Grid maths with no JUCE dependency: divisions, tempo, offset, slice boundaries |
| `source/SliceEngine.*` | Real-time voice engine that plays slices from MIDI |
| `source/SampleData.*` | Audio file loading |
| `source/SliceExporter.*` | Writes slices to WAV |
| `source/PluginProcessor.*` | Plug-in processor: host tempo, parameters, state, sample hand-off to the audio thread |
| `source/PluginEditor.*`, `source/WaveformView.*` | User interface |
| `tests/` | Unit tests |

## Licensing

Slice-able's own source is MIT licensed (see `LICENSE`). JUCE is dual-licensed under the AGPLv3 and a commercial licence. Before you distribute builds, check that your use complies with one of the two.

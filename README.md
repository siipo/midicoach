# MidiCoach

A practice tool that listens, watches and plays at the same time. It detects
the pitch of whatever comes in on the audio input, shows the notes you play on
a MIDI keyboard, draws both on a grand staff, and makes a piano sound when you
press a key.

It also writes its own sight-reading exercises, graded 1 to 8, and rehearses
them with you a note at a time - listening to your voice or your keyboard, and
waiting until you get each one right.

Standalone JUCE app for Windows, with optional ASIO audio and VST3 instrument
hosting.

> This is my first project, and it was wibecoded from start to finish with
> Claude - every line of it, across one long conversation. I decided what it
> should do and what was worth building; Claude wrote it, tested it, and argued
> back when something was a bad idea.

## What it does

- **Audio pitch detection** — YIN tracking of the audio input, with a cents
  readout and a tuner meter. Works from below the low E on a bass up to about
  2 kHz.
- **Chord detection** — a harmonic-salience layer that reports up to four notes
  sounding together. Approximate by nature; see the note below.
- **MIDI input** — every MIDI port found at startup is enabled automatically,
  so a controller that is already plugged in just works.
- **Grand staff notation** — a live snapshot of what is sounding right now,
  engraved with a SMuFL music font. Notes you play show in blue, the pitch
  heard on the audio input in orange, so you can see the two line up.
- **Scale selection** — root × scale type drives the key signature on both
  staves, the shading of in-scale keys, the enharmonic spelling of accidentals,
  and a scale-degree/interval readout.
- **Built-in piano** — no samples needed, playable from the MIDI input or by
  clicking (or typing on) the on-screen keyboard.
- **Generated sight-reading, graded 1 to 8** — short exercises built from
  statistics taken from 855 traditional folk tunes: real bar rhythms, real
  melodic tendencies, real cadences, arranged into phrases and four-bar groups
  that restate each other. A grade sets everything at once, the way a lesson
  works; independent dials for intervals, rhythm, key signature and length are
  still there underneath, along with separate switches for rests, upbeats, ties
  over the barline, syncopation and notes from outside the key. Metres run from
  2/4 to 12/8, keys from C to all twelve, minor in its natural, harmonic or
  melodic form, and length from four bars to sixteen. Everything is fitted to a
  range that follows the clef - treble C4-A5, bass E2-C4 - so nothing needs more
  than one ledger line.
- **Practised the way it is taught** — **Hear key** plays a cadence in the key
  and then your first note; **Look first** gives the exam's half a minute to
  read it through, with the checklist on screen; **Rhythm only** accepts any
  pitch so the line can be tapped or sung on one note before the pitches are
  added.
- **MIDI and PDF** — import a MIDI file as a tune, export any tune as MIDI, or
  print it as a PDF score.
- **VST3 instruments** — scan, load, and open a plugin's own editor. It is fed
  the same MIDI stream as the built-in piano and mixed alongside it.
- **Tunes** (in progress) — write short melodies by picking a duration and
  clicking the staff, and save them as JSON under `%APPDATA%\MidiCoach\Tunes`.
  There are two tools, as in any notation editor: **Select** for picking notes
  to transpose or delete, and **Write** for putting them in - `N` and `Esc`
  between them, arrows to move, shifted arrows to extend, `ctrl+Z` to undo. Up
  to 256 bars, and the staff scrolls at a readable size rather than shrinking
  to fit. The engraver handles bar lines, time signatures, note values, dots,
  rests, ties, accidentals and beams - eighths and shorter are joined under a beam,
  grouped by the beat the metre is felt in, so 6/8 gets three to a beam and 4/4
  gets two. **Rehearse** then steps through the tune one note at a
  time, waiting for each note to be played or sung before moving on, with
  playback and note cueing through the built-in piano. **In time** mode instead
  runs the tune against a metronome with a count-in and grades how close each
  note was; step by step remains the default. See
  [docs/melody-mode.md](docs/melody-mode.md).

## Requirements

To **run** the built exe:

- **Windows 10 or 11, 64-bit.**
- Nothing else. The MSVC runtime is linked statically, so there is no Visual C++
  Redistributable to install - the exe imports only stock system DLLs
  (`KERNEL32`, `USER32`, `GDI32`, `ole32`, `WINMM`, `dxgi`, ...). The music
  font is compiled into the binary too, so it is a single self-contained file you can
  copy to another machine and double-click. No installer, no admin rights.
- An audio **output** device (WASAPI or DirectSound works out of the box).
  Audio **input** is only needed for the pitch-detection half; without it the
  app runs fine and the tuner stays quiet.

Optional:

- **ASIO** - needs an ASIO driver *and* a rebuild with the SDK (see below).
- **MIDI controller** - class-compliant USB keyboards need no driver. Without
  one you can still play with the mouse, or type on the computer keyboard
  (`awsedftgyhujk` walks up from C).
- **A VST3 instrument**, only if you want to load one.

To **build** it: CMake 3.22+ and Visual Studio 2022 with the Desktop C++
workload. First configure needs internet to fetch JUCE, unless you point
`MIDICOACH_JUCE_PATH` at a checkout you already have. Python with `fontTools`
is needed only to regenerate the font (see below).

## Building (Windows)

Requires CMake 3.22+ and Visual Studio 2022 (Desktop C++ workload). JUCE is
fetched automatically on first configure.

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
```

```bash
cmake --build build --config Release
```

The binary lands in `build/MidiCoach_artefacts/Release/`.

The MSVC runtime is linked statically (`CMAKE_MSVC_RUNTIME_LIBRARY`, set at the
top of `CMakeLists.txt` before JUCE's targets are created, which is required -
setting it later leaves JUCE compiled against the DLL runtime and the link
fails on the mismatch). That costs about half a megabyte and buys an exe that
runs on a clean Windows install. Delete that block for a smaller binary that
needs the VC++ 2015-2022 Redistributable instead.

To build against a JUCE checkout you already have instead of cloning a second
copy:

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DMIDICOACH_JUCE_PATH=C:/path/to/JUCE
```

### Enabling ASIO

Steinberg's licence doesn't let JUCE bundle the ASIO SDK, so download it from
<https://www.steinberg.net/developers/> and point CMake at the extracted folder:

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DMIDICOACH_ASIO_SDK_PATH=C:/asiosdk
```

Without it the app still builds and runs on WASAPI and DirectSound — you just
won't see ASIO devices listed under **Audio / MIDI Settings**. No code changes
are needed to switch it on later; reconfigure and rebuild.

Smaller buffer sizes mean lower latency for the piano sound, at a higher risk
of dropouts.

## How it fits together

- **[`MainComponent`](Source/MainComponent.cpp)** implements
  `AudioIODeviceCallback` directly. One callback does all the real-time work:
  input goes to the analyser, output is the built-in piano plus any hosted
  plugin, both driven by the same `MidiBuffer`, so the on-screen keys, a
  hardware controller and a VST3 all stay in step.
- **[`AudioAnalyser`](Source/Analysis/AudioAnalyser.cpp)** is the boundary
  between threads. The audio callback only ever writes into a lock-free ring
  buffer; a background thread pulls the most recent window out and runs
  detection, then publishes a smoothed snapshot the UI picks up on a timer.
  Nothing expensive and nothing allocating happens on the audio thread.
- **[`PitchDetector`](Source/Analysis/PitchDetector.cpp)** runs YIN with the
  difference function evaluated through the FFT, which brings it down from
  O(N²) to O(N log N), plus parabolic interpolation for the cents readout.
- **[`MusicTheory`](Source/Theory/MusicTheory.cpp)** owns spelling. The same
  MIDI note is written differently in different keys — 61 is C♯ in D major and
  D♭ in A♭ major — so every note is spelled against the current key before
  anything draws it.
- **[`NotationComponent`](Source/UI/NotationComponent.cpp)** draws the staves.
  Positions come from diatonic steps rather than pitches, which is what makes
  ledger lines, seconds and accidental columns fall out correctly.
- **[`PluginHost`](Source/Audio/PluginHost.cpp)** holds the VST3. The audio
  thread takes a try-lock and skips a block rather than waiting if the message
  thread is mid-swap.

## About the font

`Resources/MidiCoachMusic.ttf` is generated from `Resources/Bravura.otf` by
[`Tools/otf_to_ttf.py`](Tools/otf_to_ttf.py), and the conversion is required,
not cosmetic.

JUCE embeds fonts on Windows through GDI's `AddFontMemResourceEx`, which cannot
load OpenType/CFF (`OTTO`) fonts. It doesn't fail loudly — it silently
substitutes a different font, and every SMuFL codepoint then comes back as
"glyph not found" (`0xFFFF`) while the staff lines, drawn as paths, keep
rendering fine. Converting the outlines to TrueType makes the same font load
properly.

The conversion also renames the font, and that part is a licensing requirement
rather than a preference: Bravura carries the Reserved Font Name "Bravura", and
the OFL does not let a modified version keep it. The tool rewrites the name
table and refuses to write a file that still says Bravura anywhere the reserved
name would count. Steinberg's copyright and licence entries are left in place,
which the same licence requires.

Regenerate after updating Bravura:

```bash
python Tools/otf_to_ttf.py Resources/Bravura.otf Resources/MidiCoachMusic.ttf
```

Bravura is © Steinberg Media Technologies, under the SIL Open Font License 1.1
(`Resources/OFL.txt`). `Resources/Bravura.otf` is kept unmodified as the
conversion input.

## Tests

The melody model keeps one invariant - every bar full, nothing across a bar
line, every event a legal note value - which is worth checking directly:

```bash
cmake --build build --config Release --target MelodyTests
```

```bash
./build/MelodyTests_artefacts/Release/MelodyTests.exe
```

The rehearsal engine has its own suite, covering which note an attack is
attributed to and whether a sung note is graded from when it was sung rather
than when the detector admitted to it:

```bash
cmake --build build --config Release --target RehearsalTests
```

```bash
./build/RehearsalTests_artefacts/Release/RehearsalTests.exe
```

The PDF export is checked by counting dark pixels on the rendered page: print
mode swaps the palette to ink on paper, and getting that wrong yields a
perfectly valid PDF of a blank sheet.

```bash
cmake --build build --config Release --target ExportTests
```

```bash
./build/ExportTests_artefacts/Release/ExportTests.exe
```

The staff editor has its own suite, driving the real key handler with real
key presses - selection, transposition, undo and the tool switching:

```bash
cmake --build build --config Release --target EditorTests
```

```bash
./build/EditorTests_artefacts/Release/EditorTests.exe
```

The exercise generator is checked by asserting musical invariants over 25,536
exercises - every combination of the four dials and every grade - since the
interesting failures are the rare seeds. Each feature is checked both to appear
when it is switched on and to stay away when it is not:

```bash
cmake --build build --config Release --target ExerciseTests
```

```bash
./build/ExerciseTests_artefacts/Release/ExerciseTests.exe
```

## Licence

MidiCoach is free software under the **GNU General Public License v3** - see
[LICENSE](LICENSE). You may use it, study it, change it and pass it on; anything
you distribute that is built from it stays under the same licence.

Copyright (C) 2026 siipo

Three things in or around this repository belong to other people, and GPLv3 is
the licence under which all of them line up:

- **JUCE 7.0.12** is not included here - CMake fetches it on first configure. It
  is tier-licensed by Raw Material Software, with the GPLv3 as the alternative
  to its paid tiers, and that is the path taken here. Building this yourself
  means agreeing to JUCE's terms yourself.
- **The VST3 SDK**, which ships inside JUCE and is what makes plugin hosting
  work, is licensed by Steinberg under either the proprietary Steinberg VST3
  licence or the GPLv3.
- **The music font** in `Resources/MidiCoachMusic.ttf` is converted from
  Bravura, (C) Steinberg Media Technologies, under the SIL Open Font License 1.1
  (`Resources/OFL.txt`). Bravura carries the Reserved Font Name "Bravura", so
  the converted font is renamed - OFL clause 3 forbids a modified version from
  using the reserved name. `Resources/Bravura.otf` is the upstream file,
  unmodified, kept as the conversion input.

`Source/Practice/CorpusData.cpp` is derived from the **Nottingham Music
Database** of traditional folk tunes. No tune, phrase or ABC file is
redistributed: what ships is aggregate counts - how often each bar-rhythm
occurs, how likely one scale degree is to move to another, which formulas tunes
cadence with. See [`Tools/extract_corpus_stats.py`](Tools/extract_corpus_stats.py)
for exactly what is measured.

## Known limitations

- **Chord detection is a heuristic.** It sums harmonics per candidate note and
  greedily suppresses anything landing on an accepted note's overtones. Clean
  triads work well; dense or low voicings get less reliable, and a very rich
  single note can occasionally read as two. The monophonic YIN reading is the
  trustworthy one — that's why the chord layer is drawn in a weaker colour and
  can be switched off.
- The staff is a live snapshot, not a score: no rhythm, no note lengths, no
  scrolling history.
- Accidental columns are stacked left of the chord without the collision
  avoidance a real engraver applies to dense clusters.
- VST3 only. VST2 needs a Steinberg SDK that is no longer distributed.
- Plugin choice isn't remembered between runs — the scanned plugin *list* is,
  but you re-pick the instrument each launch.
- No preset save/load.

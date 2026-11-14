##![vibecoded] - all credit goes to Claude

# Decoherence 

A sound-densification effect for drones, ambient beds and soundscapes. Every sound you feed it is
repeated by a feedback delay and spread into up to 6561 overlapping copies over several seconds,
so a plucked note dissolves into a sustained drone that grows out of it. VST3 + Standalone,
built with JUCE 9.

## Build and install

```
build.bat
install.bat
```

`build.bat` needs Visual Studio 2022 (or Build Tools) with the C++ workload. It produces:

- `build\Decoherence_artefacts\Release\VST3\Decoherence.vst3`
- `build\Decoherence_artefacts\Release\Standalone\Decoherence.exe`

`install.bat` copies the VST3 to `C:\Program Files\Common Files\VST3\` (asks for admin
rights once). Close your DAW first, then rescan plugins.

**FL Studio note:** FL only looks for VST3 plugins in `C:\Program Files\Common Files\VST3\`.
Extra folders such as `D:\VSTI\` are registered as VST2 folders and are searched for `.dll`
files only, so a `.vst3` placed there is never seen. After installing, run
Options > Manage plugins > Start scan; Decoherence shows up under Effects > VST3 (star it to
get it in the mixer's plugin menu).

## How it works

```
input --> (+) --> [ feedback delay: Time ] --+--> diffuser --> taps A --> taps B --> taps C --+--> damping --> wet
           ^                                 |   16 allpass   8 taps     8 taps     8 taps    |   (6.5 kHz)
           +--- Feedback x Tone focus <------+   (smear)      (seconds)  (fill-in)  (> 50%)   +--> bloom (> 50%)
                                                                                                 16-line FDN, 1.5-30 s
```

- **Feedback delay**: repeats the input every *Time* (20 ms - 20 s). Changing Time crossfades
  to the new length (80 ms) instead of stretching the delay, so the drone never bends in pitch.
  *Feedback* sets how long the repeats last: repeat gain = sqrt(Feedback), so each knob position
  sustains twice as long as a linear knob would; 100% = forever. *Freeze* sets the loop to
  100% and shuts the input.
- **Tone** is a smooth dark <-> bright tilt (neutral in the centre, no resonances), applied in
  three places: cut-only filters in the feedback delay's loop (dark closes a low-pass, bright
  raises a high-pass; bypassed while frozen), the bloom's damping (2 kHz dark .. open bright),
  and a +/-6 dB tilt around 800 Hz on the output. (An earlier note-centred "focus" version
  sounded boxy and pulled the drone toward a fixed pitch.)
- **Diffuser**: 16 allpass sections (3-60 ms) smear every repeat into a soft swell. Allpass
  filters spread sound in time without changing its spectrum, so there is no grain and no
  ringing resonance. The sections always drift slightly so their pattern never sits still.
  (Earlier versions used short multi-tap stages here, which sounded grainy and bell-like; a
  small feedback network was also tried and measured rougher.)
- **Tap stages**: stage A spreads 9 copies over up to 3 s, stage B fills the gaps with 9 more
  each (81 overlapping, smeared copies). As Density rises, stage C (3x stage A's span, up to
  9 s) fades in over 0-50% and stage D (6x, up to 18 s) over 50-100%: up to 6561 copies. Gains
  are energy-normalised and every tap drifts in pitch (*Spin*: gentle in the lower half, up to
  8 ms / ~1.2 Hz at the top).
- **Bloom**: a large 16-line feedback-delay network with damped, constantly modulated lines.
  Its decay grows with Density from 1.5 s to 60 s (about 9.5 s at 50%).
- **Damping**: a gentle one-pole high cut keeps the cloud warm.
- **Density** (0-100%) stretches the drone: 0% = taps A and B plus the full diffuser (one pluck
  lasts about 6 s on Drift Large); 100% = plus stages C and D and a 60 s bloom. Output level is
  calibrated to match the dry signal at 0%, 50% and 100%. The engine can also go below 0%
  (plain repeats), which only the tests use.

## Controls

| Control  | What it does |
|----------|--------------|
| Input    | Level feeding the effect (-24 to +12 dB) |
| Feedback | How long the input delay keeps repeating; 100% = forever |
| Freeze   | Loop the input delay forever, block new input (rings toward the Tone note) |
| Density  | How far the cloud stretches, 0-100% (hover shows copies, diffusers, bloom length) |
| Time     | Input delay length, 20 ms - 20 s (logarithmic knob); crossfades, never bends pitch |
| Spin     | Pitch drift on every delay: subtle low, deep and wobbly near the top |
| Model    | Cascade shape (below) |
| Tone     | Dark (warm) .. Neutral .. Bright (airy) tilt on the repeats, bloom and output |
| Mix      | Equal-power dry/wet; defaults to 100% wet |
| Output   | Final level |
| Width    | Stereo width of the wet signal (0 to 150%) |
| ON       | Bypass (also exposed as the host's bypass) |

Double-click any knob to reset it. Knobs have a little drag resistance and glide with a short
motion trail. Hover any control for help in the status bar. The echo-pattern display shows
when the copies arrive on a seconds grid (the amber T marks the Time); it morphs smoothly as
you turn knobs.

### Models

| # | Model        | Character |
|---|--------------|-----------|
| 1 | Drift Large  | Builds slowly into a vast cloud (~3.4 s) |
| 2 | Drift Medium | Builds slowly, ~1.8 s cloud |
| 3 | Drift Small  | Builds slowly, compact ~0.8 s cloud |
| 4 | Surge Large  | Swells up fast, then streams out over several seconds |
| 5 | Surge Medium | Swells up fast, medium length |
| 6 | Surge Small  | Swells up fast into a short, dense shimmer |

Drift models switch on the long taps first as Density rises (distinct reflections at low
Density); Surge models switch on diffusion and the short taps first and front-load the energy.

## Presets

14 factory presets (also visible as programs in the host). User presets are saved as
`.dcpreset` files in `Documents\Decoherence\Presets`. Projects and presets saved by earlier
versions are converted automatically so they keep their sound (Density, Feedback and Spin
scales changed). Projects saved with the first (reverb-network) version of the engine load the
"First Light" preset instead.

## Tests

- `build\EngineTest.exe [folder]` checks the engine on its own: pluck-to-drone behaviour and
  level per model, clean echo at Density 0, feedback repeat levels, freeze, 100%-feedback
  worst case, random automation, CPU, and smoothness (spectral coloration in 1/3-octave bands,
  and millisecond loudness jitter vs. smooth noise). Given a folder, it renders demo WAVs (see
  `renders\`, including the dry input `00_dry_plucks.wav` for comparison).
- `build\HostTest_artefacts\Release\HostTest.exe "<path to .vst3>"` loads the built VST3
  through JUCE's VST3 hosting code and checks sample rates 44.1-192 kHz, block sizes
  1-4096, automation, state save/restore, programs, bypass and the editor.

## Marketing images

`marketing\` holds the product description (`description.md`), a 2x render of the interface
(`plugin-ui@2x.png`) and a 1280x720 YouTube thumbnail (`youtube-thumbnail.png`). The images
are drawn by `tools\Thumbnail.cpp`, which runs the real editor with audio playing through the
"Endless Drone" preset. To redraw them after a UI change, run from a VS x64 developer prompt:

```
cmake -S . -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCW_BUILD_TOOLS=ON -DCW_BUILD_TESTS=OFF
cmake --build build-tools --target DecoherenceThumbnail
build-tools\DecoherenceThumbnail_artefacts\Release\DecoherenceThumbnail.exe marketing
```

## Project layout

```
Source/dsp/CascadeEngine.*   the engine (plain C++, no JUCE)
Source/dsp/Models.h          the six cascade models
Source/PluginProcessor.*     parameters, presets, mixing, state
Source/PluginEditor.*        window layout
Source/ui/                   look and feel and custom widgets
Source/Presets.h             factory presets
tests/                       engine test and VST3 host test
tools/Thumbnail.cpp          renders the marketing images
marketing/                   description, UI render, YouTube thumbnail
JUCE/                        JUCE 9.0.5
```

## Licensing

JUCE is dual-licensed under AGPLv3 and the commercial JUCE licence
(https://juce.com/legal/juce-9-licence/). Personal use is fine, but distributing or selling
this plugin as closed source may require a commercial JUCE licence. Check the EULA before
releasing it. The VST3 SDK bundled with JUCE is MIT-licensed.

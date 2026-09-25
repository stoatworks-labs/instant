# instant

Instant film developing in front of you — an integral print, exposed by a Take and
developing over minutes — as an FFGL **effect** for Resolume Arena/Avenue. C++/GLSL,
CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. MIT.

Read `AGENTS.md` before changing a time constant, the activation energy, the stop,
the spread or the meter.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Universal (what ships): `cmake -B build-universal -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build --parallel`
- Install into Arena: `cmake --install build` — **not run from a session**, it writes
  into `~/Documents/Resolume Arena/Extra Effects`
- Render a frame offline: `./build/intest --out /tmp/f.png --size 1920x1080`
  (90 frames of the moving card at a synthetic 60 fps, then the last one; the first
  frame takes a print, so at the default 64x that is 96 film-seconds of development)
- Set anything by name: `--set "Film=2" --set "Temperature=0.3125" --set "Speed=0"`
  (0..1 for sliders, the element index for options; `--set "Take=1"` is one press
  before the first frame). Exact positions: Temperature 0.625 = 24 °C, 0.3125 = 14,
  0.9375 = 34; Speed 0 = 1x (honest), 0.75 = 64x; Interval 0.625 = 16 s.
- List parameters, kinds, defaults and ranges: `./build/intest --list`
- Other sources: `--source flat --level 0.5`, `--source white`, `--source black`
- The exact GLSL the plugin compiles: `./build/intest --dump-shaders DIR`
- Refit the activation energy: `python3 tools/arrhenius_fit.py` (`--check` compares
  it with `source/Model.h`)
- Footage through the real shaders — **`--pipe`**, raw RGBA frames in, raw RGBA frames
  out, with `--size WxH`, `--fps N` (frame n is clocked at n / fps) and an optional
  `--script` of `frame Parameter Name value` cues. A slider ramps linearly between
  cues; an option, a boolean and an integer STEP (they hold the last cue at or before
  the frame); an event fires on its cue frame only. A cue naming no parameter is refused
  with exit 2, a partial frame at the end of stdin ends the stream cleanly, a failed
  render or a closed stdout exits 1 (SIGPIPE is ignored, so `| head -c 1` is exit 1,
  not 141):
  `ffmpeg … -f rawvideo -pix_fmt rgba - | ./build/intest --pipe --size 1920x1080 --fps 30 [--script cues.txt] | ffmpeg …`

## Verify
- Everything: `tools/verify.sh` (bash; fresh universal build + the activation-energy
  fit + glslc + the reserved-word grep + every check at 320x180 AND 1280x720 + every
  check at 320x180 on the software renderer + --pipe + the sweep + the bundle; about
  8 minutes on this Mac, most of it the software renderer)
- Each dye layer follows its first-order law with the stated tau: `./build/intest --develop`
- Cyan leads at 240 s; the balance then only moves toward neutral: `./build/intest --order`
- tau at 14 and 34 °C in the Arrhenius ratio, every layer: `./build/intest --arrhenius`
- Each row starts at distance / front speed: `./build/intest --front`
- The roller repeats at its circumference, whole-pixel and fractional: `./build/intest --roller`
- The print develops from the capture as the clip changes; a resize keeps it: `./build/intest --take`
- The camera's meter: `./build/intest --meter`
- The checks can fail: `./build/intest --negative`; one perturbation verbosely:
  `./build/intest --perturb BITS --order` (bits in `Model.h`)
- Every check takes `--size WxH`; CI runs them at 320x180
- CI's renderer, on this Mac: `INTEST_RENDERER=software ./build/intest --take --size 320x180`
  (Apple's software renderer, what GitHub's macOS runner has; about 40x slower)
- No name over 16 characters, none duplicated, no trademark: `./build/intest --names`
- No dead controls: `python3 tools/sweep.py` (`--size WxH`, `--jobs N`)
- Render cost and the state held: `./build/intest --bench` (best of three; the GPU here is shared)
- What a host sees: `~/Projects/resolume/oxbow/build/oxbow probe build-universal/Instant.bundle`

## Notes
- **The print is a process, not a grade.** Capture at the take → meter → each texel's
  dye and stop doses, accumulated every frame from the film-seconds after the reagent
  front arrived, each weighted by an Arrhenius rate → per layer D = Dinf (1 − e^(−A/τ)),
  the opacifier O0 e^(−S/τop) → the white pigment through both. `Model.h` holds the
  numbers; the `kModel` GLSL library in `Shaders.cpp` holds the spread and the curve,
  after a constants block written from `Model.h` at assembly time.
- **Time is reduced in double on the CPU.** The age since the take, the take itself,
  the Arrhenius factors: all decided here. The shaders see the age at the frame's
  start and the frame's film-seconds, both relative to the take.
- **Speed scales film time; 1x is a real print** (ten minutes to the stop). The default
  64x brings a picture up in about a second and ends development 9.4 s after the take.
- **The print survives a resize by being resampled, not reallocated.** A reallocated
  `PassBuffer` is a cleared one. `--take` checks it.
- **Take is edge-triggered** on the value crossing 0.5. In Take mode the plugin is a
  viewfinder (the clip, untouched) until the first press; in Continuous mode the first
  frame takes, and so does a press.
- **Output alpha:** 1 over the print, 0 outside it at Border above 0 (colour 0 too, so
  straight and premultiplied readers agree); Mix fades to the source's alpha.
  Resolume's demo clips carry alpha; the capture premultiplies by it (no light from a
  transparent pixel).
- **`Perturb` bits and `Probe` are test hooks**, always 0 in the plugin.
- **Parameter names must be unique** — `--set` and the sweep find them by name. No
  "Polaroid" or "Instax" in any name or option: `--names` checks.
- `SetParamInfo` clamps a STANDARD default into 0..1 before `SetParamRange` can widen
  it, so every slider is 0..1 and `Controls.cpp` holds the units, with inverses. Options
  are mapped by index.
- Override `SetTextParameter` to return FF_SUCCESS for the About block, or no host can
  instantiate the plugin at all.
- `instant_core` is an OBJECT library, not STATIC — the plugin registers itself from a
  file-scope constructor nothing references by name.
- `FFGLScopedFBOBinding.h` is not in the umbrella header; include `<ffglex/FFGLScopedFBOBinding.h>`.
- macOS build must be universal. Verify with `lipo`, never the build log.
- GLSL 4.10 reserved words are not identifiers: `patch sample input output filter
  common active half layout flat packed` and the rest of s3.6; `verify.sh` greps the
  dumped shaders for them. MSVC has no `M_PI` and `far`/`near` are macros there.
- FFGL id is `IN01`, display name `SW Instant`.
- This repo is `~/dev/instant`, outside `~/Projects`. The worktree guard keys on a
  session's starting directory; git here was run as `CLAUDE_WORKTREE_EXEMPT=1 git -C ~/dev/instant …`.

## Not done yet
- **Never loaded into Resolume.** Everything numeric is measured offline on macOS
  against the real plugin class in a headless CGL context, plus an `oxbow` load.
- Seen on the synthetic card and on Resolume's bundled demo clips through `--pipe`;
  never on camera footage.
- No OpenFX port, no browser demo, no presets, no user guide (`guide=""` in the
  provisional `StoatworksAbout.h`). `StoatworksAbout.h` and `ATTRIBUTIONS.md` are
  provisional hand copies until the backend's sync scripts know this repo.

## Diagnostics

`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/instant/instant.YYYY-MM-DD.log        (macOS)
    %LOCALAPPDATA%\instant\logs\instant.YYYY-MM-DD.log   (Windows)

# AGENTS.md — Instant

Onboarding for whoever (or whatever) picks this up next. `CLAUDE.md` is the short
command reference; this is the *why*. Read "What is actually verified" before you
tell anybody this works.

---

## What the plugin is

Instant film developing in front of you, as an FFGL 2.1 effect (`IN01`, shown as
`SW Instant`) for Resolume Arena and Avenue. C++17 + GLSL 4.10, CMake, universal
macOS `.bundle` and a Windows `.dll`. MIT, public at
[github.com/stoatworks-labs/instant](https://github.com/stoatworks-labs/instant), released
at v0.1.0 on 2026-09-25 with a user guide, a site page, a browser demo and a video (see
"The release, v0.1.0" at the end). Never loaded into Resolume on macOS.

Built 2026-09-25 in one session (tranche five, Allan's pick) from the fleet's
templates and `specs/SPEC-instant.md`: wetplate for the plugin shape, the Take event,
the harness, `--pipe`, verify and the negative controls; rebate for a photochemical
process with a characteristic curve; toner for a process with a direction and
rollers; photofinish for the resize trap; tinsel for `PassBuffer`, the sweep and CI.

---

## The one idea

**An integral instant print develops in the open, over minutes, and the picture
arrives in a known order.** Not a grade that fades in: the process, stage by stage.

| the stage | what comes out |
| --- | --- |
| a Take captures the clip's frame; the camera's averaging meter sets the exposure | **the print is of that moment**, and keeps developing from it whatever the clip does next |
| a positive curve: where silver develops, the dye developer is held back | **a positive print** with instant film's short latitude and soft highlights |
| a reagent front leaving the pod edge at a stated speed | **the print develops bottom-first**, by distance ÷ speed |
| each dye layer first-order, cyan fastest, yellow slowest | **pale and blue-cyan first, warming** as magenta and yellow arrive |
| an opacifier clearing with the timing layer | **a dark green-grey frame** that the picture comes up through |
| the timing layer's pH drop ending development, with its own lower activation energy | **cold film stops short: cyan, pale, low contrast; hot film finishes: warm** |
| too little paste, or paste dried with age | **dark corners** where the reagent never reached |
| dirt on a roller | **a mark repeating at the roller's circumference** |

### The pipeline, in order

1. **Capture** (at a Take only). The host frame, sRGB-decoded, premultiplied by its
   alpha (no light from a transparent pixel), into an RGBA16F buffer.
2. **Meter** (at a Take only). The capture's BT.709 luminance over the image window
   onto a 64 × 64 R32F grid, averaged to one texel by its mip chain. Gain
   = 0.18 / mean, clamped to −2..+3 stops; `Exposure` (±2 stops) rides on top.
3. **Develop** (every frame, per texel of the source raster). With A the dye dose and
   S the stop dose (seconds at 24 °C):
   `overlap = the part of this frame's film-seconds after the front arrived at t0 = s / v`,
   `S += k_stop overlap`, `A += k_dye overlap × (the fraction of it before S reached the stop)`,
   k = exp(−Ea / R (1/T − 1/Tref)). First-order development is then exact in A,
   whatever the temperature did, and the stop is exact to the partial frame.
4. **Print** (to the host). Layout (print, frame, square window, or full frame at
   Border 0); per layer `Dinf = Balance × depth × (Dmin + (Dmax − Dmin)(1 − c(log10 H)))`
   with rebate's softplus coverage curve; `D = Dinf (1 − e^(−A/τ))`;
   `O = O0 e^(−S/τop)`; reflectance `White × 10^(−(D + O))`, or the dark negative
   where the reagent never reached; the frame, a little texture and shine; sRGB; Mix.

`Balance_i = 1 / (1 − e^(−stop/τ_i))`, so a neutral grey comes out exactly neutral at
the stop at 24 °C — the film is balanced for a warm room, which is what makes the
cold and hot casts fall out rather than be painted on.

### What falls out, and what does not

- **The colour drift is not painted.** At 240 s a neutral grey reads density R 0.67,
  G 0.60, B 0.51 — a blue-cyan print — from the three time constants alone, and moves
  monotonically to neutral (3e-7) at the stop.
- **Cold and hot casts are not painted.** They are two activation energies: the dye's
  (fitted from a published chart) and the timing layer's (assumed half of it). At 6 °C
  development ends with yellow at roughly 3/4 of its asymptote; at 34 °C all three
  finish. Nothing measures the cast itself, only the rates (`--arrhenius`); the casts
  were judged by eye on the demo clips.
- **Not modelled:** spectral dye curves and unwanted absorptions (each dye absorbs only
  its channel), interimage effects, the negative's own grain (sub-pixel at video
  rasters), a real fluid spread (the front is a line moving at one speed, the reach a
  stated profile), the reagent trap at the top, flash, double exposure, the print
  going on developing after the pH drop (it does, slightly), fading over years, light
  fogging a print pulled from the camera early.

---

## The shape of the code

| File | What it is |
| --- | --- |
| `source/Model.h` | Every number, each marked as fitted, sourced or ASSUMED; the stocks; the `Perturb` and `Probe` hooks. |
| `source/Controls.{h,cpp}` | What a 0..1 slider means, with inverses; the print layout in millimetres; the Arrhenius factor. Exact positions land exactly in binary. |
| `source/Shaders.{h,cpp}` | A constants block written from `Model.h`, the `kModel` GLSL library (sRGB, hashing, the spread, the curve) and the five pass bodies, assembled at run time. |
| `source/PassBuffer.*` | tinsel's FFGLFBO with the leak fixed, plus wetplate's `Swap`. |
| `source/Instant.{h,cpp}` | The plugin: parameters, the clock, the take, buffers, the passes. |
| `source/Diag.{h,cpp}` | A log file, for the shader that will not compile. |
| `tools/arrhenius_fit.py` | The ILFORD chart's rows and the least-squares fit of Ea; `--check` for verify. |
| `tools/intest/` | The offline harness: renders, measures, benchmarks, pipes, dumps shaders. |
| `tools/sweep.py` | No control is silently dead. |
| `tools/verify.sh` | All of it, at two rasters and on the software renderer, plus the release-time checks. |

State held across frames: the capture (RGBA16F), two dose buffers (RG32F,
ping-pong) — 24 bytes a texel — and the 64 × 64 meter. The capture and the current
dose buffer are resampled, not reallocated, on a resize.

---

## Where the chemistry's numbers come from

Said plainly, because the brief asked for it and because most of them are choices.

| number | value | status |
| --- | --- | --- |
| dye activation energy | **67.41 kJ/mol** | **fitted** by `tools/arrhenius_fit.py` to 40 cells of ILFORD's film development time/temperature compensation chart (2002), standard error 0.62 kJ/mol. That chart is B&W silver development; using it for dye diffusion transfer is an **assumption**. |
| timing-layer activation energy | 33.7 kJ/mol | **assumed**: half the dye figure (diffusion through a polymer, not a chemical step). No source. |
| dye τ at 24 °C (C, M, Y) | 70, 120, 200 s | **assumed**; ordered as the process is known to arrive (blue-cyan first, warming) and sized so the print is done in the 10–15 minutes Polaroid states for its colour film. |
| B&W τ; vintage τ | 90 s; 90, 170, 230 s | **assumed**. |
| opacifier τ | 45 s (stop-seconds) | **assumed**: the picture is unreadable for about a minute, readable by two. |
| the stop | 600 s | **assumed**: the short end of the stated 10–15 minutes. |
| reference temperature | 24 °C | a choice inside Polaroid's stated 13–28 °C working range. |
| front speed | 1 image height / s | **assumed**: a camera ejects a print in about a second. |
| roller circumference | 5/16 of the image height (24.7 mm of 79) | **assumed**; 5/16 so the whole-pixel and fractional cases are both easy to set up. |
| curve, Dmin/Dmax, latitude, opacifier colour, white, frame | Model.h | chosen by eye inside the published picture of instant film (short latitude, soft highlights); not measured. |
| meter | mid grey 0.18, −2..+3 stops | the target is the photographic mid grey; the range is **assumed**. |

The qualitative facts the temperature model was built against are Polaroid's support
article "How does temperature affect Polaroid film?". The build session had it only
as quoted by search results (the live page answers 403 to scripts), which said a
*cyan* tint in the cold. **Re-read at release (2026-09-25) through the Internet
Archive's copies of 2023-02 and 2026-04**, the page says: a working range of 13–28 °C;
below it, photos "emerge over-exposed, lacking color contrast and with a green tint";
above it, colour photos "develop with a yellow/red tint". So the model agrees on the
light, low-contrast cold print and on the warm hot one, and **disagrees on the cold
hue**: its cold cast is blue-cyan, because the yellow dye is the slowest and is the one
cut short; a green tint would need the magenta to lag instead. That was not changed at
release (it would reorder the development the whole plugin is built on); it is stated
in the guide. No number in the plugin is taken from the page.

---

## Traps

Roughly in the order they will bite.

### ☠️ Resolume's demo clips are dark, and a print at a fixed exposure is nearly black

The first survey of the bundled clips through `--pipe` (Trinity, Enter5, the tank) came
out as black squares with faint lines: the clips are CG loops on black, and the film
was exposed at the clip's own levels. A camera meters. The meter (pass 1b) brings the
image window's mean linear luminance to mid grey at the take; the first range, +4
stops, then flattened the tank's mid-tones to white on a mostly-black frame, so it is
+3. `--meter` measures it and its negative control removes it.

### ☠️ A shader stage that is never dumped is never compiled by glslc

The meter was added after `--dump-shaders` was written, and verify said "all 5 shaders
compile" while the plugin compiled six. Nothing had checked the meter's GLSL outside
this Mac's driver. verify now fails unless exactly six stages are dumped.

### ☠️ A copied verify.sh checks the template's identity

The sed from wetplate renamed `Wetplate` but not `WT01`, and the first full verify
failed at the very last step: oxbow saw `IN01` where the script wanted `WT01`. The
bundle was right; the check was the template's. Grep for the template's id, not just
its name, after copying.

### ☠️ A tolerance whose comment lists a term the code leaves out

`--meter`'s clamped case failed by 1.1e-4 on the first run. The tolerance's comment
said the RGBA16F capture rounds a patch to 2^−11 relative and that the gain divides
it out only when the meter is in range — and then the code used the in-range
tolerance for the clamped case too. The term is now in the code: 2^−11 / ln 10 times
the curve's steepest slope, 3.4e-4. The measurement (1.1e-4) was not used to set it.

### ☠️ Transparent is colour 0 as well as alpha 0

The first print pass wrote the frame's colour with alpha 0 outside the print. A host
reading the output as premultiplied would add that colour over the layer below. The
colour outside is now 0 too, and the antialiased edge is premultiplied, so straight
and premultiplied readers agree.

### ☠️ The worktree guard keys on the session's starting directory

Every `git add` in `~/dev/instant` was refused as "a shared checkout" because the
session began in `stoatworks-backend`. This repo is new, private to the session and
outside `~/Projects`, so git ran as `CLAUDE_WORKTREE_EXEMPT=1 git -C ~/dev/instant …`.

### A clip's first frame is often black

In Continuous mode the first frame takes a print, and a clip that fades in from black
gives a black first print (the meter cannot lift black). The survey starts each clip
two seconds in; in a host the next print comes Interval seconds later. See Open
questions.

### The software renderer is forty times slower

Apple's software renderer (`INTEST_RENDERER=software`, what GitHub's macOS runners
have) takes about five minutes of CPU for one 320×180 pass of every check, most of
verify's eight minutes. Every check passes there with the tolerances unchanged.

### Inherited from the fleet, and all still true here

`ScopedFBOBinding` does not restore the viewport (the host's is captured first and put
back before the print pass); every `ffglex::Scoped*` clears to 0 on exit, so every
`Ensure()`, the resize resample and the dose clears at a take happen before anything
binds a texture; `FFGLFBO::Release()` leaks the colour texture (`PassBuffer::Destroy()`
deletes it first); `SetParamInfo` clamps a STANDARD default into 0..1; the core is an
**OBJECT** library; `SetTextParameter` must return `FF_SUCCESS` for the About block;
the harness drives a synthetic clock; `nm | grep -q` fails under pipefail; an option's
range reads back 0..1; Resolume's clock overflows a float, so every time here is reduced
in double and the shaders see only the age at the frame's start and the frame's length,
relative to the take; a reallocated buffer is a cleared buffer; InitGL must not clear a
take pressed before the context existed (wetplate's sweep trap, kept); `--pipe` ignores
SIGPIPE so a closed stdout is exit 1; zsh has no `PIPESTATUS`, so verify is bash;
`packed` and the rest of GLSL 4.10's reserved words are not identifiers; no `M_PI`,
`far` or `near`.

---

## Would this hold on another rasteriser, at another raster?

One line per check. Every tolerance is derived, not fitted; every check ran at 320×180
and 1280×720 and at 320×180 on Apple's software renderer in `verify.sh`. kU = 2^−24,
half a float ULP at 1. The density checks read the print pass's own densities through
the `Probe` hook (the same shader, returning before the presentation); `--order` reads
the picture.

**The density bound**, used throughout: a sample on frame n is good to
**Dinf (n + 24) kU** — n float adds to the dose (relative n kU, which through
1 − e^(−A/τ) moves D by at most Dinf n kU / e), exp's (3 + 2|x|) ULP (GLSL 4.10 §8.2),
and four roundings in the products; rounded up. The opacifier is good to
(8 + 4x + x n) 2 kU relative.

| check | what it measures | tolerance and where it comes from | raster dependence |
| --- | --- | --- | --- |
| `--develop` τ | τ per layer from ln(successive differences) against t: slope −1/τ whatever Dinf and t0 are | the slope's worst case, Σ \|w_j\| ε_j, with ε_j the two samples' density bounds over the difference; samples used only where ε ≤ 1% | none: a flat patch, one pixel at the centre |
| `--develop` law | every difference on the fitted line | its own ε plus the hat-matrix row times the ε's | none |
| `--develop` clock | film age = frame × Speed / 60 | 1e-9 × frames (double) | none |
| `--develop` opacifier | τop from ln O against t | the same, with the opacifier's relative bound | none |
| `--develop` B&W | the three channels are one image | the density bound at Dmax 2 | none |
| `--arrhenius` | τ(14 °C) / τ(34 °C) per layer and the opacifier, against exp(Ea/R (1/T1 − 1/T2)) | the ratio times the sum of the two fits' relative bounds | none |
| `--order` lead | at 240 s the picture's densities R > G > B | coarse by design: the spec's claim as an inequality (margins ~0.07) | the pixel is at 1/10 of the frame from the bottom-left, whatever the raster |
| `--order` monotone, neutral | the balance (max − min channel density) never rises; at 640 s it is ≤ tol | 2× the density bound at Dmax + the encode's pow (32 kU / ln 10) + the shine's stated value at the pixel over the darkest channel; at the end plus the opacifier's stated residue (O0,R − O0,G) e^(−S/τop) | the shine is computed at the pixel from the stated band |
| `--front` | each row's start from its opacifier, t0 = age − S, against distance / speed | τop × the opacifier's bound + (n + 8) kU of the age (the arrival's rounding) = 6.8e-5 s | every row of the column, s measured in image heights at the pixel centre; the dose is linear in s, so a bilinear read at a texel centre is exact |
| `--roller` whole | D(s + C) = D(s) on every row, C = 5H/16 integral | 2× the density bound | run at the multiple of 16 at or below H (176, 720): the mark is computed in coordinates local to its centre, so every repeat is the same arithmetic on the same numbers — measured 0 |
| `--roller` fractional | each mark's centroid against s0 + kC; the spacing against C | **1 / (8w)** px per centroid: under exact area coverage each partial edge pixel's first moment is off by cov(1 − cov)/2 ≤ 1/8, and the edges err in opposite senses; + the reads Σ\|x − c\| 2 tol / mass; the spacing twice that + 0.01 | run at H (180) or H + 4 (724) so C ends in .25; w = 0.018 H is 3.2 px at 180 |
| `--take` capture | the print with the clip changing after the take = the print with it held | the density bound | none |
| `--take` resize | at 1.5× mid-development, against the unresized print interpolated in the row; after resizing back, pixel for pixel | the density bound + the resample's bilinear weight quantisation: 2^−9 of a row's dose step (1/(vH) s), times Dinf/τ_min, twice; 4 px clear of edges and the patch boundary | the dose is linear in s and the capture flat, so bilinear resampling is exact to the weights' precision on any hardware with ≥ 8-bit sub-texel weights |
| `--take` viewfinder | Take mode before a press: output = input | exact (a passthrough of a float texture read at texel centres) | none |
| `--meter` | two in-range levels print the same, at the stated density of 0.18 | the density bound ×2; for the stated value + 64 kU of Dmax for the curve's float evaluation | none: the meter reads a flat patch, and a mip chain of equal values averages exactly |
| `--meter` clamped | a level needing 6.3 stops prints as the stated curve at +3 | + the RGBA16F capture's 2^−11 / ln 10 in log H, times the curve's steepest slope | none |

The tolerances are worst cases and generous: the τ fits agree with the stated τ to
about 1e-5 relative where they allow 3e-3, because the bound charges every sample the
full accumulation error. They still sit 30–50× inside what the negative controls
produce.

Deliberately NOT relied on: `pow(1, x) == 1`, `mix(a, b, 1) == b` (the print pass
returns early at Mix 1), exact cancellation (the balance check compares against a
tolerance, not zero), implicit derivatives, or the input texture's filter (the capture
uses `texelFetch`).

What might still differ on another rasteriser: the bilinear weight precision (bounded
above), the meter's mip averaging of a textured frame (exact only on a flat one), and
the lanes, texture and shine, which are hashed or analytic and so identical everywhere
but checked only by the sweep.

### The negative controls

`intest --negative` runs eight, and `--perturb BITS` runs any check verbosely against
one. Each perturbs the *plugin's* model — a `Perturb` bit the shipped plugin carries at
zero — never the harness's expectation. Measured at 320×180 and 1280×720:

| perturbation | what fails |
| --- | --- |
| one τ (magenta's) for every dye layer | `--order`, 4 of 4: at 240 s R 0.53 < G 0.53 < B 0.56 — yellow leads, not cyan; the balance rises on 18 of 46 steps; 3.1e-2 off neutral at the end |
| every point starts at t0 = 0 | `--front`, 2 of 2: every row reads t0 = 0 |
| temperature changes no rate | `--arrhenius`, 4 of 8: every ratio reads 1 against 6.287 and 2.507 |
| cyan's τ × 1.1 in the shader | `--develop`, 3 of 13: cyan 77.0 against 70 (tolerance 0.24); B&W 99 against 90; its channels disagree |
| the roller 2% larger than stated | `--roller`, 13 of 17 |
| a resize that clears the print | `--take`, 2 of 7: 0.73 off in density |
| a capture that follows the clip | `--take`, 5 of 7 |
| the camera's meter ignored | `--meter`, 3 of 3 |

### The mutation

One character of the shipped GLSL, on a clean committed tree: in the develop pass,
`clamp( ( StopDose - dose.y ) / dStop, 0.0, 1.0 )` → `( StopDose + dose.y )` — a stop
that never comes, so the dye keeps moving after the timing layer has run out. Caught by
`--order` (2 of 4: the balance rose on 4 of 46 steps; 5.3e-3 off neutral past the stop,
against 1.3e-4) and `--meter` (1 of 3: 6.5e-3 off the stated density of mid grey).
`--develop` and `--arrhenius` did not catch it, correctly: they sample only before the
stop. Reverted with `git checkout source/Shaders.cpp`; the tree was clean before and
after. It shows a gap: no check measures the stop itself directly, only its
consequences.

---

## The browser demo

`demo/` is the page at **instant-demo.stoatworks-labs.com** (2026-09-25), on the fleet's
kit (`stoatworks-backend/resolume-demo`, vendored by its `sync.sh`; `instant` is not yet
in that script's repo list, so `sync.sh --check` does not see it).

**What is the plugin's.** Every GLSL string of `Shaders.cpp` — the version line, the
vertex body, `kModel` and the capture, meter, develop, resample and print bodies — is
spliced into `demo/plugin.js` by `demo/tools/sync_shaders.py`, tabs and comments
included, with `K_CONSTANTS`: the text `constants()` writes from Model.h at run time
(each `put` evaluated from Model.h, a float through binary32, printed with `%.9g`), plus
every Model.h constant, the stocks and the Controls lists. `demo/tools/check_shaders.py
--dump DIR` holds all of it to the C++ and compares every stage the page assembles with
what the plugin compiled (`intest --dump-shaders DIR`), byte for byte; `tools/verify.sh`
runs it. A shader change here means re-running the sync script, never an edit of the page.
The buffers are the plugin's (RGBA16F capture, RG32F dose ping-pong, 64 × 64 R32F meter
with its mip chain); the page refuses to start without `EXT_color_buffer_float` and
`OES_texture_float_linear`.

**One spelling is the page's.** `%.9g` prints `kFrontSpeed`, `kKnee` and `kMeterMaxGain` as
`1`, `6` and `8`, and GLSL ES 3.00 has no implicit int-to-float conversion: ANGLE refuses
`const float kKnee = 6;` ("cannot convert from 'const int' to 'const highp float'"). The
page compiles the exact text once per load and reports the verdict under the picture,
then compiles with lines of exactly the shape `const float kName = <integer>;` spelt
`.0`. Same floats; no other character changes. (A `%.9g` that always printed a decimal
point in `constants()` would make the exact text portable; not done here, it is a plugin
change.)

**What is a hand port, checked by nobody but a reader:** Controls.cpp (every law,
`PrintLayout`, `ArrheniusFactor`, `OptionIndex`), the defaults from `Instant::Instant()`,
and from `ProcessOpenGL` the clock (dt clamped to [0, 0.25 s], the nominal first frame),
the take (edge trigger, Continuous interval, the backward-clock rule), the film age in
double, the buffer ensures and clears, `rescale`, the crop, the stock arithmetic and
every uniform. Change one of those here and change the page by hand.

**What differs, each said on the page:** Take is a button under the picture (the kit has
no event type); the clock is the kit's (no unit vote; a paused page renders dt = 0 frames,
so a paused print holds; Restart is a backward clock, so the interval restarts and the
print is kept; the kit caps a delta at 0.1 s); no About block; `Perturb` and `Probe` are
0; the meter's mip average is the browser's; the reagent front is modelled but cannot be
watched (it crosses in about a film-second under an opacifier that takes a minute).
Clips are the kit's generated ones (moving scene first), never Resolume's.

**Measured once (2026-09-25).** The page driven frame by frame at n / 60 from a fresh
instance (`window.__instantDemo.hooks`: `fresh()`, and `afterRender` to read the canvas
and the input inside the frame) on the Synthetic scene clip at 320x180, 120 frames,
against `intest --pipe --fps 60` on the same input frames read back from the page, with
the same values `--set`: at the defaults; at Vintage, 14 °C, Expired 0.5, Interval 1 s
(a second take at frame 60), Exposure 0.7, Uneven, Dirty Roller 1, 256×, Border 0.6, Mix
0.8; and in Take mode, Black & White, Short, 34 °C, 128×, Border 0, with Take pressed at
frames 10 and 70 (`--script`). Through ANGLE on Metal: 8, 16 and 0 channel values of 27.6
million differ, each by 1. Through SwiftShader: 22 518, 25 232 and 100 031, each by 1.
It can fail, but by count more than by size, because the print is smooth: Temperature
0.63 against 0.625 (24.16 against 24 °C) makes 1.1 million values differ, by 1; Exposure
0.51 against 0.5 makes 1.95 million differ, up to 2/255 (3 on SwiftShader). A Take on a
paused page moves the picture by a mean 13.4 levels, a Film change by 1.3, a resize from
960x540 to 1280x720 mid-print keeps it (mean 52.87 → 52.83, one resample, no take).
Driving trap: the kit redraws a paused page on every parameter change, so a set made after
`fresh()` renders a stray frame at the old clock and takes a print there; set everything,
let that frame render, then `fresh()`.

Deploy: `cf-run npx wrangler deploy` from the repo root, or push to main
(`.github/workflows/deploy.yml`). The host is a Worker **route** over a proxied
`AAAA 100::` record made through the API on 2026-09-25, not a custom domain: the zone
is at Cloudflare's limit of 100. Delete that record and the page goes dark while deploys
stay green. Verify by content:
`curl -s 'https://instant-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'`.

## Decisions taken without asking

- **Time is the host's.** Film age accumulates the host clock's frame deltas (clamped
  to 0..0.25 s, the first frame 1/60) × Speed, in double. A paused host pauses the
  print; a real print would go on developing. Same contract as the fleet.
- **Speed 1× is honest; the default is 64×.** At 64× the picture comes up in about a
  second and development ends 9.4 s after the take; with the default Interval of 16 s a
  finished print holds for six seconds before the next. Speed runs 1–256×, geometric.
- **Everything scales with Speed**, the front included: at 64× the spread is one frame.
  **The front is not visible at any Speed** (found filming the release video): it
  crosses the frame in one film-second and the opacifier clears over a minute, so on a
  flat grey at 1× the bottom leads the top by a level or two at most, less than the
  shine's own gradient. `--front` measures it; the video and guide do not claim it.
- **Default Mode is Continuous**, so dropping the effect on a clip does something. In
  Take mode the plugin is a viewfinder (the clip, untouched) until the first press —
  switching modes keeps the print that exists. A press in Continuous takes at once and
  restarts the interval.
- **The camera meters** (see the first trap). Averaging, linear, over the image window,
  at the take, −2..+3 stops; `Exposure` is the lighten/darken wheel, ±2 stops.
- **Controls stay live after the take.** Film, Exposure, Expired, Spread, Dirty Roller
  and Temperature change the developing print, though physically only Temperature
  could. A VJ tool whose sliders did nothing after a take would read as broken. The
  capture is the scene's light, so this is honest about what is stored.
- **Output alpha:** 1 over the print; outside it (Border > 0), colour and alpha 0, so a
  print floats over the layer below; Mix fades to the source's alpha. Resolume's demo
  clips carry alpha: the capture premultiplies by it.
- **The state lives in the source frame's coordinates**, and the film coordinates are the
  current crop's. A Border change mid-development therefore moves the reagent front's
  geometry under a dose already accumulated — visible only in the first film-second or
  at a short spread's edge. A Border change is not a resize and resamples nothing.
- **Where the reagent never reached**, the negative shows: dark blue-black. Defect guides
  say the colour varies with the chemistry.
- **The dirty roller lightens**: a mark takes up to 60% of the dye, uneven along the
  roller, the same at every repeat (it is the same dirt).
- **No trademarks** in any name, option or on screen (`--names` checks). The README
  says what the process is.
- **Test hooks live in the shipped plugin** (the `Perturb` bits, `Probe` in the print
  pass), always zero.
- **The About block and ATTRIBUTIONS.md are generated** by the backend's sync-about and
  sync-attributions since registration (they were provisional hand copies before it).
- **No OpenFX port and no presets** for 0.1.0. (The user guide and the browser demo came
  with the release.)

---

## What is actually verified, and what is assumed

### Verified by measurement, on an M4 Max running macOS 26.4.1 (2026-09-25)

Every number is `tools/verify.sh` against a fresh universal Release build, at 320×180
and 1280×720, and again at 320×180 on Apple's software renderer.

- **Develop.** Colour: cyan τ 70.0000, magenta 120.0004, yellow 200.0016 s against 70,
  120, 200 (tolerances 0.20, 0.52, 1.3 s); every difference on its first-order line;
  B&W 90.0000 against 90; opacifier 44.9997 against 45 (tolerance 3.5e-3).
- **Order.** At 240 s density R 0.676 > G 0.607 > B 0.516; the balance falls on every
  one of 46 steps to 3.0e-7 past the stop (tolerance 1.3e-4).
- **Arrhenius.** τ(14 °C)/τ(34 °C) = 6.28707, 6.28710, 6.28716 for cyan, magenta and
  yellow against 6.28703 at 67.41 kJ/mol; the opacifier 2.50736 against 2.50739 at
  33.7 kJ/mol.
- **Front.** Every row began at distance / speed to 4.1e-6 s (180 rows) and 4.5e-6 s
  (720 rows), tolerance 6.8e-5; the speed out of the picture 1.000001 image heights / s.
- **Roller.** Whole-pixel (55 and 225 px): D(s + C) = D(s) exactly on every row; marks at
  exactly their stated centres. Fractional (56.25 and 226.25 px): centroids within
  0.028 and 0.0009 px of stated (tolerance 0.097 and 0.245); periods 56.23/56.31 and
  226.249/226.252 px.
- **Take.** The viewfinder exact; the print from the capture identical (0) whether or
  not the clip changes; developing (cyan 0.18 → 0.60 → 0.99); resized to 1.5× and back
  mid-development, the same print to 2.3e-7 (tolerance 1.3e-5).
- **Meter.** sRGB 0.30 and 0.70 print the same to 6e-8, at the stated 0.68657; sRGB 0.03
  prints 1.60204 against the stated 1.60200 at the +3-stop limit.
- **Negative controls** all eight fail; **the mutation** is caught (above).
- **No dead controls**, all 12, with the four About buttons skipped.
- **Every shader** (six stages) compiles through `glslc` as the plugin assembles it; no
  reserved word as an identifier. **The activation energy** refits from the chart.
- **`--pipe`** gives exactly two frames for two and a half, refuses an unknown cue, exits
  1 on a closed stdout (`| head -c 1`) and on a failed render, steps an option cue and
  fires an event cue on its frame.
- **The bundle** is universal (`x86_64 arm64`), exports `_plugMain`, carries
  `com.stoatworks.ffgl.instant`, ad-hoc signs; `oxbow probe` reports `SW Instant` /
  `IN01` / `effect`, and `oxbow selftest` renders 120 frames through `plugMain`.
- **Render cost** at the defaults, best of three runs of 60 frames after a warm-up,
  `glFinish` both sides, on a shared GPU:

  | | ms/frame | % of a 60fps frame | state held |
  | --- | --- | --- | --- |
  | 1280×720 | 0.051 | 0.3% | 21 MB |
  | 1920×1080 | 0.071 | 0.4% | 47 MB |
  | 3840×2160 | 0.374 | 2.2% | 190 MB |

  A take adds one capture pass and the meter's 64 × 64 pass and mip chain.

### Assumed, or not done

- ☠️ **Never loaded into Resolume on macOS** (on Windows, see "The release, v0.1.0"). Everything was compiled,
  rendered and measured offline against the real plugin class in a headless CGL
  context, plus an `oxbow` load. How Resolume's clock behaves across a long session,
  what a paused composition does to a print, and how the controls present are untested.
- **Windows** builds in CI (first run green 2026-09-25) and passed the fleet's Arena gate on
  software rendering; never run on a GPU.
- **Seen on Resolume's bundled demo clips** (Trinity, Enter5, IntoTheGlow, the tank, Beat
  001) through `--pipe`, never on camera footage — skin, skies, a real scene metered by
  a real camera's rules.
- **The chemistry's numbers** are the table above: one fitted from a chart for a
  different process, the rest assumptions.
- **Nothing measures the casts** Temperature produces, nor Expired, Vintage, the spread
  shapes, the texture or the shine — only the sweep says they change the picture.
- **No check measures the stop time directly** (the mutation found that gap).
- **Not verified at 4K**, only benchmarked there.

---

## Open questions

- **Should the first print wait for a non-black frame?** A clip that fades in gives a
  black first print in Continuous mode. A meter reading under a threshold could defer
  the take a frame at a time.
- **Should controls other than Temperature freeze at the take?** Physically they would.
- **Is 64× the right default?** It suits a VJ set; a slow ambient piece wants 8–16×,
  (the spread's front is not visible even there; see Decisions).
- **A beat-synced Interval** (bars, from Resolume's transport) would suit live use better
  than seconds.
- **Should the unreached negative be dark?** The guides say it depends on the chemistry.
- **Real numbers.** A timed series of photographs of real prints developing at three
  temperatures would replace every assumption in the table with a measurement.

---

## Siblings

- **wetplate** — the Take event, the harness, `--pipe`, verify, the negative controls.
- **rebate** — a photochemical process as a process; the softplus curve.
- **toner** — a process with a direction and rollers; `--pipe` with SIGPIPE ignored.
- **photofinish** — the resize-mid-run trap.
- **tinsel** — `PassBuffer`, `sweep.py`, and the fleet's trap list.
- **oxbow** — `oxbow probe` and `oxbow selftest` are what load this bundle as a host.

---

## The release, v0.1.0 (2026-09-25)

Moved from `~/dev/instant` to `~/Projects/resolume/instant`, public at
`stoatworks-labs/instant`, registered in the website's projects.json, the backend's
sync-about TARGETS and the attribution tables; the About block and ATTRIBUTIONS.md are
generated since. verify.sh green on the About build; CI green on GitHub (macOS physics on
the runner's software renderer, and the first Windows MSVC build, which compiled first time).

- **Arena (win-lab, 7.27.1, Mesa llvmpipe, no GPU): 9 of 9** in the fleet gate
  (`plugin-bench/arena/expect/instant.json`, 9f98ff8). 18 host controls match. Live: Opacity,
  Film, Temperature (weak, 1.49 on the ratio test), Expired, Exposure, Spread, Dirty Roller
  (weak, 1.31), Border, Mix. Speed inconclusive: it acts only while a print develops, and the
  gate's print is done 9.4 s after its take. Mode and Interval are marked `inert`: they act
  only at the next take, and the next print of a still is the same print.
- **Filming found the reagent front invisible** at any Speed (see Decisions). The video's
  1× beat is seven seconds of real time under the opacifier, then a ramp to 64× on the same
  print; no beat claims the front.
- **Measured casts** (mid grey through `--pipe` at 256×, after the stop, 8-bit sRGB):
  6 °C (119, 128, 139), 14 °C (117, 120, 125), 24 °C (117, 117, 116), 34 °C (117, 116, 112).
  The cold cast is clear; the hot one slight.
- **The temperature source, re-read**: the Polaroid support article (115012361067) through the
  Internet Archive (2023-02-01 and 2026-04-06 copies; the live page answers 403 to scripts)
  says below 13 °C prints come out "over-exposed, lacking color contrast and with a green
  tint", above 28 °C colour prints "a yellow/red tint". The model's cold cast is cyan. Left as
  it is and stated in the guide, README, ATTRIBUTIONS and derived.json. A green cold tint
  would need magenta to be the layer cut short.
- **IntoTheGlow_02 flashes every half second**, so a take on it prints whichever phase it
  caught; the first video beat moved to Metalive's steady gold.


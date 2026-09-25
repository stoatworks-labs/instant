# Attributions

Instant is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Plugin shape, harness, --pipe contract and verify — Stoatworks wetplate, rebate, toner

<https://github.com/stoatworks-labs/wetplate>  
Licence: MIT  
Copyright: Stoatworks Labs

The plugin's shape (the OBJECT core, the clock-unit voting, the About block, the Diag logger), the Take event and its edge trigger, the harness's session, PNG writer, parameter lookup, bench and --pipe mode with SIGPIPE ignored, the verify script and the negative-control pattern are wetplate's, which had them from rebate and toner; the characteristic curve's softplus form is rebate's; the host clock-unit voting is readout's by way of all of them.

### PassBuffer — Stoatworks tinsel

<https://github.com/stoatworks-labs/tinsel>  
Licence: MIT  
Copyright: Stoatworks Labs

PassBuffer is tinsel's, with wetplate's Swap, so the print can change raster without being cleared.

### The resize-mid-run guard — Stoatworks photofinish

<https://github.com/stoatworks-labs/photofinish>  
Licence: MIT  
Copyright: Stoatworks Labs

The trap that a reallocated buffer is a cleared buffer, and the check that guards it, are photofinish's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The activation energy — ILFORD PHOTO, film development time/temperature compensation chart (April 2002)

<https://www.ilfordphoto.com/wp/wp-content/uploads/2017/03/Temperature-compensation-chart.pdf>

The source of the yellow dye layer's activation energy (and the black-and-white image's), 67.41 kJ/mol: five of the chart's rows (40 cells) are transcribed in tools/arrhenius_fit.py, which fits ln t against 1/T with a shared slope (standard error 620 J/mol) and which verify.sh re-runs. The chart is for black-and-white silver development in a tank; applying it to the dye layers of an integral print is an assumption. The timing layer's activation energy, half of it, has no source. Since v0.1.1 the cyan and magenta layers have their own, fitted to the manufacturer's description below, not to this chart.

### What temperature does to an integral print — Polaroid support, "How does temperature affect Polaroid film?"

<https://support.polaroid.com/hc/en-us/articles/115012361067>

A working range of 13-28 C; below it prints emerge over-exposed, lacking colour contrast and with a green tint; above it colour prints develop with a yellow/red tint. Read on 2026-09-25 through the Internet Archive's copies of 2023-02 and 2026-04 (the live page answers 403 to scripts). The page gives no numbers. Since v0.1.1 the cyan and magenta layers' activation energies (103.6 and 106.85 kJ/mol) are fitted to its description by tools/cast_fit.py: at 6 C a mid grey comes out with red equal to blue, a green, with a cast as strong as v0.1.0's. They are a fit to words, not a measurement; no published per-layer figure was found. The hot print's warm cast falls out of the model unfitted.

### Roller marks and undeveloped patches — Instant-film troubleshooting guides (Dan Finnen, danfinnen.com; Polaroid support)

That dirty rollers leave a series of repeating marks down the length of an image, and that undeveloped patches come from paste that did not spread, most often at the edges and corners and more in old film. The appearance of an unreached area varies with the chemistry; the plugin's dark blue-black is a choice.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The integral instant print

Built from what the process is rather than from anyone's implementation: an exposure through the front of the film, a reagent pod at the wide bottom border burst by the camera's rollers and spread up the frame, dye developers released from the negative's three layers and held back where silver develops, reaching the image layer at their own rates under an opacifier that shields the negative until a timing layer drops the pH and ends development. Edwin Land's integral film (1972) is the process; the idea of doing a photochemical process as a process is rebate's. No code, assets or binaries from any product were used or examined.

## Standards and published specifications

What the implementation is measured against.

- **CODATA 2018** — the molar gas constant, 8.314462618 J / (mol K).
- **PCG (M. E. O'Neill, Harvey Mudd College, 2014)** — the pcg_hash output mix used for the spread's lanes, the roller's dirt and the paper texture, written out rather than copied from anyone's source.
- **IEC 61966-2-1:1999** — the sRGB transfer function, both ways.
- **ITU-R BT.709** — the luminance weights the black-and-white stock and the meter use.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.

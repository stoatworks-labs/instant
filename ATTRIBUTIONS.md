# Attributions

Instant is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is a **provisional hand copy**, written 2026-09-25 in the shape the backend's
`scripts/sync-attributions.py` generates, because Instant is not registered in the
backend's master lists yet. When it is, the sync overwrites this file.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Plugin shape, harness, --pipe contract and verify — Stoatworks wetplate, rebate, toner

<https://github.com/stoatworks-labs/wetplate>  
Licence: MIT  
Copyright: Stoatworks Labs

The plugin's shape (the OBJECT core, the clock-unit voting, the About block, the Diag logger), the Take event and its edge trigger, the harness's session, PNG writer, parameter lookup, bench and `--pipe` mode with SIGPIPE ignored, the verify script and the negative-control pattern are wetplate's, which had them from rebate and toner; the characteristic curve's softplus form is rebate's; the host clock-unit voting is readout's by way of all of them.

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

Vendored as a git submodule at external/ffgl, pinned to b1afaf9 like the fleet.

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

### The activation energy — ILFORD PHOTO, film development time/temperature compensation chart, 2002

The source of `kDyeActivation` in source/Model.h, 67.41 kJ/mol. ILFORD PHOTO, "Film development time/temperature compensation chart" (April 2002), <https://www.ilfordphoto.com/wp/wp-content/uploads/2017/03/Temperature-compensation-chart.pdf>: development times at 18–27 °C for a given time at 20 °C, rounded to 15 s. Five of its rows (40 cells) are transcribed in tools/arrhenius_fit.py, which fits ln t against 1/T with a shared slope (standard error 620 J/mol) and which verify.sh re-runs. **The chart is for black-and-white silver development in a tank. Applying it to the dye layers of an integral print is an assumption**, stated here, in the script, in Model.h and in AGENTS.md: no published activation energy for dye-developer diffusion transfer was found. The timing layer's activation energy, half of it, has **no source**; it is an assumption.

### What temperature does to an integral print — Polaroid support pages

The qualitative behaviour the temperature model was built to reproduce. Polaroid's support article "How does temperature affect Polaroid film?" (support.polaroid.com, article 115012361067) states a working range of 13–28 °C, that below it prints come out over-exposed, lacking colour contrast and with a cyan (blue) tint, and that above it colour prints develop with a yellow/red tint; Polaroid's product pages give the colour film's development time as about 10–15 minutes. The support page answered 403 to the session that wrote this, so these statements are as quoted by search-engine summaries of it, and should be re-read before a release. **No number in the plugin is taken from them.** The dye time constants (70, 120, 200 s at 24 °C), the opacifier's (45 s), the stop (600 s), the front's speed and the roller's circumference are all assumptions chosen to land inside that description, and Model.h says so beside each one.

### Roller marks and undeveloped patches — instant-film defect guides

That dirty rollers leave "a series of repeating marks down the length of an image", and that undeveloped patches come from paste that did not spread, most often at the edges and corners and more in old film, are from photographers' troubleshooting guides (Dan Finnen, "How to create the Polaroid 'look'", danfinnen.com; Polaroid's support article on undeveloped patches). The appearance of an unreached area varies with the chemistry; the plugin's dark blue-black is a choice.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The integral instant print

Built from what the process is rather than from anyone's implementation: an exposure through the front of the film, a reagent pod at the wide bottom border burst by the camera's rollers as the print is ejected and spread up the frame, dye developers released from the negative's red-, green- and blue-sensitive layers and immobilised where silver develops, so the print is a positive, reaching the image layer at their own rates under an opacifier that shields the negative until a timing layer drops the pH and ends development. Edwin Land's integral film (1972) is the process; the idea of doing a photochemical process as a process is rebate's.

## Standards and published specifications

What the implementation is measured against.

- **CODATA 2018** — the molar gas constant, 8.314462618 J / (mol K).
- **Melissa E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation" (Harvey Mudd College, 2014)** — The pcg_hash output mix used for the spread's lanes, the roller's dirt and the paper texture, written out rather than copied from anyone's source.
- **IEC 61966-2-1** — The sRGB transfer function, both ways.
- **ITU-R BT.709** — the luminance weights the black-and-white stock and the meter use.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.

#!/usr/bin/env python3
"""The cyan and magenta layers' activation energies, fitted to what the
manufacturer says a cold print looks like.

    python3 tools/cast_fit.py            the fit, and the cast it gives from 4 to 36 degC
    python3 tools/cast_fit.py --check    exit 1 unless source/Model.h carries this fit

------------------------------------------------------------------- the source

Polaroid support, "How does temperature affect Polaroid film?",
https://support.polaroid.com/hc/en-us/articles/115012361067 (read through the
Internet Archive, 2026-04-06 copy; the live page answers 403 to scripts). It
gives a working range of 13-28 degC; below it prints come out light and low in
colour contrast "with a green tint"; above it colour prints take a yellow/red
tint. It gives no numbers. No published per-layer rate or activation energy
for an integral print's dye layers was found (2026-09-25: the diffusion
transfer patents describe the layers, not their temperature dependence).

------------------------------------------------------------ what is fitted, and to what

The development model is Model.h's: each dye layer i reaches, at the stop,

    r_i( T ) = Balance_i ( 1 - exp( -StopDose k_i( T ) / ( k_stop( T ) tau_i ) ) ),
    k( T ) = exp( -Ea / R ( 1 / T - 1 / Tref ) ),  Balance_i = 1 / ( 1 - exp( -StopDose / tau_i ) ),

of its balanced density, so a neutral grey's channel densities are
proportional to r_c, r_m, r_y (the colour stock's curve and dye range are the
same in every layer). A green tint is magenta deficient: r_m below r_c and r_y.

  - yellow keeps the ILFORD chart's figure (tools/arrhenius_fit.py), and the
    timing layer keeps half of it, as in v0.1.0;
  - at kCastFitC (6 degC, well inside "below 13"), r_c = r_y: red and blue come
    out equal, so the hue is green, not cyan-green or yellow-green;
  - at the same temperature, r_y - r_m equals v0.1.0's cast there, the max -
    min of r under one shared activation energy: the same strength of cast,
    turned from cyan to green.

Two conditions, two unknowns (Ea_c, Ea_m), each solved by bisection on a
function monotone in its unknown. The result is rounded to 10 J/mol, as the
chart's figure is. It is a fit to a DESCRIPTION: the hue is the page's, the
strength is v0.1.0's choice, the temperature is ours.

The hot side is not fitted. It falls out: every layer all but finishes before
the stop, and the slowest (yellow) over-reaches its balance most, so the print
is yellow-red, as the page says.
"""
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
R = 8.314462618  # J / ( mol K ), CODATA 2018


def model():
    text = (ROOT / "source" / "Model.h").read_text()

    def num(name):
        m = re.search(name + r"\s*=\s*([0-9.eE+-]+)\s*;", text)
        return float(m.group(1))

    tau = re.search(r"kTauColour\[ 3 \]\s*=\s*\{([^}]*)\}", text).group(1)
    layer = re.search(r"kLayerActivation\[ 3 \]\s*=\s*\{([^}]*)\}", text).group(1).split(",")
    dye = num("kDyeActivation")
    return {
        "tau": [float(x) for x in tau.split(",")],
        "stop": num("kStopDose"),
        "ref": num("kReferenceC"),
        "cold": num("kCastFitC"),
        "dye": dye,
        "stopEa": 0.5 * dye,  # Model.h: kStopActivation = 0.5 * kDyeActivation
        "layer": [float(x) if "kDyeActivation" not in x else dye for x in layer],
    }


def completion(m, ea, c, i):
    def k(e):
        return math.exp(-e / R * (1.0 / (c + 273.15) - 1.0 / (m["ref"] + 273.15)))

    tau = m["tau"][i]
    a = m["stop"] * k(ea) / k(m["stopEa"])
    return (1.0 - math.exp(-a / tau)) / (1.0 - math.exp(-m["stop"] / tau))


def bisect(f, lo, hi):
    flo = f(lo)
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if (f(mid) > 0) == (flo > 0):
            lo, flo = mid, f(mid)
        else:
            hi = mid
    return 0.5 * (lo + hi)


def fit(m):
    cold = m["cold"]
    shared = [completion(m, m["dye"], cold, i) for i in range(3)]
    strength = max(shared) - min(shared)
    ry = completion(m, m["dye"], cold, 2)
    ec = bisect(lambda e: completion(m, e, cold, 0) - ry, 20000.0, 400000.0)
    em = bisect(lambda e: (ry - completion(m, e, cold, 1)) - strength, 20000.0, 400000.0)
    return ec, em, strength, shared


def main():
    m = model()
    ec, em, strength, shared = fit(m)
    rc, rm = round(ec / 10.0) * 10.0, round(em / 10.0) * 10.0
    if "--check" in sys.argv:
        have = m["layer"]
        if abs(have[0] - rc) > 0.5 or abs(have[1] - rm) > 0.5 or have[2] != m["dye"]:
            print(f"source/Model.h says {have}; the cast fit gives cyan {rc:.0f}, magenta {rm:.0f}, yellow {m['dye']:.0f} J/mol")
            return 1
        print(f"kLayerActivation {have[0]:.0f}, {have[1]:.0f}, {have[2]:.0f} J/mol is the cast fit at {m['cold']:.0f} degC")
        return 0
    print(f"at {m['cold']:.0f} degC, one shared Ea (v0.1.0): r = " + ", ".join(f"{x:.4f}" for x in shared) + f"  (cast strength {strength:.4f})")
    print(f"Ea cyan {ec:.1f}, magenta {em:.1f}, yellow {m['dye']:.1f} J/mol; Model.h carries {rc:.0f}, {rm:.0f}, {m['dye']:.0f}")
    print(" degC    r_c     r_m     r_y    G over R  G over B  R over B   (in units of the grey's density)")
    eas = [rc, rm, m["dye"]]
    for c in (4, 6, 8, 10, 12, 13, 14, 18, 24, 28, 30, 34, 36):
        r = [completion(m, eas[i], c, i) for i in range(3)]
        print(f"  {c:2d}  {r[0]:.4f}  {r[1]:.4f}  {r[2]:.4f}   {r[0] - r[1]:+.4f}   {r[2] - r[1]:+.4f}   {r[2] - r[0]:+.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

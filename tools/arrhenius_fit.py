#!/usr/bin/env python3
"""The activation energy the plugin's development uses, fitted from a published
time/temperature compensation chart.

    python3 tools/arrhenius_fit.py            the fit, and what it means
    python3 tools/arrhenius_fit.py --check    exit 1 unless source/Model.h
                                              carries this fit's value

------------------------------------------------------------------- the source

ILFORD PHOTO, "Film development time/temperature compensation chart" (April
2002), https://www.ilfordphoto.com/wp/wp-content/uploads/2017/03/
Temperature-compensation-chart.pdf -- a one-page table of development times at
18, 19, 20, 21, 22, 24, 25 and 27 degC for a given time at 20 degC, "a useful
guide for all film/development combinations", rounded to the nearest 15 s.
Five of its rows are transcribed below (the ones with an entry in every
column), in minutes:seconds.

------------------------------------------------------------ what it is, and is not

A development TIME at temperature T is the time to reach the same density, so
t(T) is proportional to 1 / k(T), and with the Arrhenius law
k = A exp( -Ea / R T ),

    ln t = Ea / ( R T ) + const.

The slope of ln t against 1 / T is Ea / R. Every cell of the five rows goes
into one least-squares fit with a separate intercept per row (each row is a
different nominal time; the slope is shared).

This is a chart for BLACK-AND-WHITE silver development in a tank. An integral
instant print develops by a different chemistry -- dye developers diffusing
from the negative's layers to the image-receiving layer in a viscous alkaline
reagent. No published figure for that process's activation energy was found in
the session that wrote this, so the plugin ASSUMES the silver-development
figure applies to the dye layers: the rate-limiting step in both is a developer
reducing exposed silver halide, and the chart is the best-documented number for
how fast that step speeds up with temperature. The assumption is stated here,
in Model.h, in AGENTS.md and in ATTRIBUTIONS.md.

The timing layer (which ends development and clears the opacifier by dropping
the pH) is diffusion-controlled, and diffusion has a lower activation energy
than a chemical step. The plugin takes HALF this figure for it. That is an
assumption with no source; it is what makes cold film end development before
the slow yellow layer arrives (the cyan cast and low contrast Polaroid's own
support pages describe), and hot film finish it (the warm cast).
"""
import math
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

R = 8.314462618  # J / ( mol K ), CODATA 2018

TEMPERATURES_C = [18, 19, 20, 21, 22, 24, 25, 27]

# Five rows of the chart, the 20 degC column being the row's nominal time.
ROWS = [
    "8:00  7:15  6:30  6:00  5:15  4:30  4:00  3:30",
    "9:45  8:45  8:00  7:15  6:30  5:30  5:00  4:15",
    "14:45 13:15 12:00 10:45 9:45  8:15  7:30  6:30",
    "19:45 17:45 16:00 14:30 13:00 11:00 10:00 8:30",
    "29:45 26:45 24:00 21:45 19:30 16:45 15:00 13:00",
]


def seconds(cell):
    m, s = cell.split(":")
    return 60 * int(m) + int(s)


def fit():
    """Least squares with a shared slope and one intercept per row: centre
    each row's x and y on the row's own means, then one slope through all of
    them."""
    sxx = sxy = 0.0
    n = 0
    residuals = []
    rows = []
    for row in ROWS:
        cells = row.split()
        xs = [1.0 / (t + 273.15) for t in TEMPERATURES_C]
        ys = [math.log(seconds(c)) for c in cells]
        mx = sum(xs) / len(xs)
        my = sum(ys) / len(ys)
        rows.append((xs, ys, mx, my))
        for x, y in zip(xs, ys):
            sxx += (x - mx) ** 2
            sxy += (x - mx) * (y - my)
            n += 1
    slope = sxy / sxx
    for xs, ys, mx, my in rows:
        for x, y in zip(xs, ys):
            residuals.append(y - (my + slope * (x - mx)))
    rms = math.sqrt(sum(r * r for r in residuals) / (n - len(ROWS) - 1))
    stderr = rms / math.sqrt(sxx)
    return slope * R, stderr * R, n, max(abs(r) for r in residuals)


def model_value():
    text = (ROOT / "source" / "Model.h").read_text()
    m = re.search(r"kDyeActivation\s*=\s*([0-9.eE+-]+)", text)
    return float(m.group(1)) if m else None


def main():
    ea, err, n, worst = fit()
    rounded = round(ea / 10.0) * 10.0  # to 10 J/mol: the chart is rounded to 15 s
    if "--check" in sys.argv:
        have = model_value()
        if have is None:
            print("source/Model.h has no kDyeActivation")
            return 1
        if abs(have - rounded) > 0.5:
            print(f"source/Model.h says {have:.1f} J/mol; the chart fits {rounded:.1f}")
            return 1
        print(f"kDyeActivation {have:.0f} J/mol is the chart's fit ({n} cells, +-{err:.0f})")
        return 0
    print(f"{n} cells, 5 rows, shared slope")
    print(f"Ea = {ea:.1f} J/mol  (standard error {err:.1f}; worst residual in ln t {worst:.4f})")
    print(f"Model.h carries {rounded:.0f} J/mol")
    for tc in (4, 13, 24, 28, 36):
        k = math.exp(-rounded / R * (1.0 / (tc + 273.15) - 1.0 / 297.15))
        print(f"  at {tc:2d} degC the dye rate is {k:.3f} x its 24 degC rate")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Draw the GameCube controller's buttons as small inline SVGs for the README.

The controls tables used to name each button in backticks, which tells you the
letter but not which button it is - and on a GameCube pad the shape and colour
carry more than the letter does. A is the big green one, B the small red one,
X and Y the grey capsules standing and lying either side of it, Z the purple
shoulder. These are those shapes, at those relative sizes.

Every icon is drawn on a canvas of the same height and its own width, with the
shape centred inside. That is what keeps the relative sizes: the README sets one
height for all of them, so a button drawn smaller than the canvas stays smaller
on the page - A larger than B, the shoulder buttons wider than they are tall,
the way the pad is. Drawing each shape to fill its own box instead would make
them all the same size the moment a height was set. Everything is drawn with
presentation attributes rather than a stylesheet, because GitHub sanitises SVG
served from a repository and strips <style>. Every <img> in the README carries
the letter as alt text, so the raw file and any renderer that drops images still
say which button is meant.
"""
import pathlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "buttons"

# Colours from the standard indigo GameCube controller.
GREEN  = "#3f9e46"
RED    = "#c0322f"
GREY   = "#dedede"
DARK   = "#8f8f8f"
PURPLE = "#5b3fa0"
YELLOW = "#f5bf2b"
INK    = "#1a1a1a"
WHITE  = "#ffffff"

STROKE = 1.4
FONT = "Helvetica,Arial,sans-serif"

# The shared canvas height. A, the biggest button, fills it; everything else is
# centred inside it at whatever size it is relative to A.
CANVAS = 26


def svg(width, aria, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {CANVAS}" '
            f'width="{width}" height="{CANVAS}" role="img" aria-label="{aria}">'
            f'{body}</svg>\n')


def text(cx, cy, letter, colour, size):
    return (f'<text x="{cx}" y="{cy}" fill="{colour}" font-family="{FONT}" '
            f'font-size="{size}" font-weight="bold" text-anchor="middle" '
            f'dominant-baseline="central">{letter}</text>')


def rounded(letter, width, height, fill, ink, font, canvas_width=None):
    """One rounded rectangle, centred on the shared canvas. A circle is the
    case where width and height match and the radius is half of them."""
    cw = canvas_width if canvas_width is not None else width
    x, y = (cw - width) / 2, (CANVAS - height) / 2
    body = (f'<rect x="{x + STROKE / 2:g}" y="{y + STROKE / 2:g}" '
            f'width="{width - STROKE:g}" height="{height - STROKE:g}" '
            f'rx="{min(width, height) / 2 - STROKE / 2:g}" fill="{fill}" '
            f'stroke="{INK}" stroke-width="{STROKE}"/>'
            + text(cw / 2, CANVAS / 2, letter, ink, font))
    return cw, body


def round_button(letter, diameter, fill, ink, font):
    return rounded(letter, diameter, diameter, fill, ink, font, canvas_width=CANVAS)


def capsule(letter, width, height, font):
    """X stands tall and Y lies flat, which is how they sit around A."""
    return rounded(letter, width, height, GREY, INK, font, canvas_width=max(width, CANVAS))


def shoulder(letter, fill, ink, width=34, height=20, font=13):
    """L, R and Z live on the shoulder: wider than they are tall."""
    return rounded(letter, width, height, fill, ink, font)


def dpad():
    body = ('<path d="M9 1 h8 v8 h8 v8 h-8 v8 h-8 v-8 H1 V9 h8 Z" '
            f'fill="{DARK}" stroke="{INK}" stroke-width="{STROKE}" stroke-linejoin="round"/>')
    return CANVAS, body


def stick(letter, fill):
    body = (f'<circle cx="13" cy="13" r="12.3" fill="{fill}" stroke="{INK}" '
            f'stroke-width="{STROKE}"/>'
            f'<circle cx="13" cy="13" r="7" fill="none" stroke="{INK}" '
            f'stroke-width="1" opacity="0.4"/>')
    return CANVAS, body + (text(13, 13, letter, INK, 11) if letter else "")


ICONS = {
    # The control stick and the C-stick differ only in colour on the real pad.
    "stick":  ("Analog stick", stick("", GREY)),
    # A is the largest button on the pad and B the smallest of the four.
    "a":      ("A button",     round_button("A", 26, GREEN, WHITE, 15)),
    "b":      ("B button",     round_button("B", 21, RED, WHITE, 12)),
    "x":      ("X button",     capsule("X", 18, 26, 13)),
    "y":      ("Y button",     capsule("Y", 26, 18, 13)),
    "z":      ("Z button",     shoulder("Z", PURPLE, WHITE)),
    "l":      ("L trigger",    shoulder("L", GREY, INK)),
    "r":      ("R trigger",    shoulder("R", GREY, INK)),
    "start":  ("Start button", shoulder("START", DARK, WHITE, width=54, height=20, font=11)),
    "dpad":   ("D-pad",        dpad()),
    "cstick": ("C-stick",      stick("C", YELLOW)),
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (aria, (width, body)) in ICONS.items():
        (OUT / f"{name}.svg").write_text(svg(width, aria, body), encoding="utf-8")
    print(f"button icons: {len(ICONS)} written to {OUT}")


if __name__ == "__main__":
    main()

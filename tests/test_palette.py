#!/usr/bin/env python3
"""Contrast of every colour pair the UI actually draws.

The theme is light now, and inverting a dark palette does not produce a
working light one: a green tuned to read on near-black washes out on
near-white, and a colour that was a panel fill becomes invisible as text. The
pairs below are the ones the screens really put together, checked against
WCAG contrast ratios so a retune cannot quietly make something unreadable.

Thresholds are deliberately not all 4.5. Body text has to be comfortable;
secondary text can sit lower; and the "off" colour is meant to look off - it
only has to be legible enough to read the word it is drawn in.
"""
from __future__ import annotations
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "source" / "main.c"


def palette() -> dict[str, tuple[int, int, int]]:
    text = MAIN.read_text()
    found = {}
    for name, r, g, b in re.findall(
            r"static const GXColor (C_\w+)\s*=\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*\d+\s*\}", text):
        found[name] = (int(r), int(g), int(b))
    return found


def luminance(rgb: tuple[int, int, int]) -> float:
    def channel(v: int) -> float:
        c = v / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(v) for v in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a: tuple[int, int, int], b: tuple[int, int, int]) -> float:
    la, lb = luminance(a), luminance(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


# (foreground, background, minimum, what it is)
PAIRS = [
    ("C_TEXT", "C_PANEL", 7.0, "body text on a panel"),
    ("C_TEXT", "C_BG", 7.0, "body text on the background"),
    ("C_TEXT", "C_SELECT", 4.5, "the selected row"),
    ("C_TEXT", "C_HEADER", 4.5, "the title bar"),
    ("C_MUTED", "C_PANEL", 4.0, "secondary text on a panel"),
    ("C_MUTED", "C_BG", 3.5, "secondary text on the background"),
    ("C_MUTED", "C_SELECT", 2.5, "secondary text on the selected row"),
    ("C_ACCENT", "C_PANEL", 4.0, "a section heading"),
    ("C_ACCENT", "C_BG", 3.5, "a section heading on the background"),
    ("C_GREEN", "C_PANEL", 3.5, "a good status"),
    ("C_YELLOW", "C_PANEL", 3.5, "a warning"),
    ("C_RED", "C_PANEL", 3.5, "an error"),
    ("C_FAINT", "C_PANEL", 1.8, "an off or empty state"),
    ("C_FAINT", "C_BG", 1.5, "an off state on the background"),
    ("C_BADGE_TEXT", "C_GREEN", 3.5, "badge text on green"),
    ("C_BADGE_TEXT", "C_YELLOW", 3.5, "badge text on yellow"),
    ("C_BADGE_TEXT", "C_RED", 3.5, "badge text on red"),
    ("C_BADGE_TEXT", "C_ACCENT", 3.5, "badge text on the accent"),
    ("C_BADGE_TEXT", "C_BADGE_OFF", 3.0, "badge text on an unset badge"),
    ("C_BORDER", "C_PANEL", 1.5, "a panel edge"),
    ("C_BORDER", "C_BG", 1.2, "a panel edge on the background"),
]

# A fill is not a text colour. C_PANEL2 stood in for "off" on the dark theme
# and vanished on the light one, which is what C_FAINT is for.
FILLS_ONLY = ("C_PANEL2",)


def main() -> None:
    colors = palette()
    if len(colors) < 12:
        raise SystemExit(f"only parsed {len(colors)} colours from {MAIN}")

    problems = []
    for fg, bg, minimum, what in PAIRS:
        if fg not in colors or bg not in colors:
            problems.append(f"  {fg} or {bg} is not in the palette")
            continue
        ratio = contrast(colors[fg], colors[bg])
        if ratio < minimum:
            problems.append(f"  {what}: {fg} on {bg} is {ratio:.2f}:1, wanted {minimum}:1")

    text_calls = re.findall(r"gui_text[f]?\([^;]*?(C_\w+)", MAIN.read_text())
    for fill in FILLS_ONLY:
        if fill in text_calls:
            problems.append(f"  {fill} is a fill colour and is being drawn as text")

    if problems:
        print("palette contrast check: FAIL")
        print("\n".join(problems))
        sys.exit(1)
    worst = min((contrast(colors[f], colors[b]) / m, f, b)
                for f, b, m, _ in PAIRS if f in colors and b in colors)
    print(f"  {len(PAIRS)} colour pairs clear their minimum "
          f"(tightest: {worst[1]} on {worst[2]})")
    print("palette contrast check: PASS")


if __name__ == "__main__":
    main()

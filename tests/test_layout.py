#!/usr/bin/env python3
"""Static layout check for source/main.c.

Every screen draws into a 640x480 frame with a header across the top and a
footer panel at y=438. Nothing may be drawn outside that, and a scrolling list
must not run into the "n of m" counter printed under it - both mistakes are
invisible until the thing is on a television, which is an expensive way to
find out.

This reads the source rather than the binary: it cannot know what a runtime
value will be, so it checks the literal coordinates and the row loops whose
base, step and visible count are all constants.
"""
from __future__ import annotations
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "source" / "main.c"

CONTENT_TOP = 58.0
FOOTER_TOP = 438.0
SCREEN_W = 640.0
# A line of text is about this tall at the scales the UI uses.
TEXT_HEIGHT = 18.0

NUM = r'(-?[0-9.]+)f?'


def fail(problems: list[str]) -> None:
    for p in problems:
        print("  " + p)
    print(f"layout check: FAIL ({len(problems)} problems)")
    sys.exit(1)


def screens(src: str) -> dict[str, str]:
    out = {}
    for m in re.finditer(r'static void (show_\w+)\(void\) \{', src):
        start = m.start()
        end = src.find("\nstatic ", m.end())
        out[m.group(1)] = src[start:end if end > 0 else len(src)]
    return out


def check_literals(name: str, body: str, problems: list[str]) -> None:
    for m in re.finditer(r'gui_(?:text|textf|badge)\(\s*%s\s*,\s*%s\s*,' % (NUM, NUM), body):
        x, y = float(m.group(1)), float(m.group(2))
        if y < CONTENT_TOP or y + TEXT_HEIGHT > FOOTER_TOP:
            problems.append(f"{name}: text at y={y:g} falls outside {CONTENT_TOP:g}..{FOOTER_TOP:g}")
        if x < 0 or x > SCREEN_W:
            problems.append(f"{name}: text at x={x:g} is off screen")
    for m in re.finditer(r'gui_(?:panel|rect)\(\s*%s\s*,\s*%s\s*,\s*%s\s*,\s*%s\s*,' % (NUM, NUM, NUM, NUM), body):
        y, h = float(m.group(2)), float(m.group(4))
        if y < CONTENT_TOP or y + h > FOOTER_TOP:
            problems.append(f"{name}: panel {y:g}..{y + h:g} falls outside {CONTENT_TOP:g}..{FOOTER_TOP:g}")


def check_rows(name: str, body: str, defines: dict[str, int], problems: list[str]) -> None:
    """A row loop plus a counter under it must not overlap."""
    # Two shapes appear: a named `y` computed before the draws, and the same
    # arithmetic written straight into the gui_text call.
    rows = re.search(r'const float y = %s \+ (\w+) \* %s;' % (NUM, NUM), body)
    if not rows:
        rows = re.search(r'gui_text\w*\(\s*%s\s*,\s*%s \+ (\w+) \* %s\s*,' % (NUM, NUM, NUM), body)
        if not rows:
            return
        base, var, step = float(rows.group(2)), rows.group(3), float(rows.group(4))
    else:
        base, var, step = float(rows.group(1)), rows.group(2), float(rows.group(3))

    # How many rows the loop can draw: a local `visible` constant, or a #define.
    count = None
    vis = re.search(r'const unsigned visible = (\d+)u;', body)
    if vis:
        count = int(vis.group(1))
    else:
        bound = re.search(r'%s < (\w+)' % re.escape(var), body)
        if bound and bound.group(1) in defines:
            count = defines[bound.group(1)]
    if count is None:
        return

    last = base + (count - 1) * step
    bottom = last + TEXT_HEIGHT
    if bottom > FOOTER_TOP:
        problems.append(f"{name}: {count} rows from y={base:g} step {step:g} reach {bottom:g}, past the footer")

    # The classic mistake: a "n of m" counter printed just below a list that
    # is one row longer than its author thought. Two lines of text overlap
    # when each starts before the other ends.
    for m in re.finditer(r'gui_(?:text|textf)\(\s*%s\s*,\s*%s\s*,' % (NUM, NUM), body):
        y = float(m.group(2))
        if y >= base and y < bottom and y + TEXT_HEIGHT > last:
            problems.append(
                f"{name}: text at y={y:g} overlaps the last of {count} rows, "
                f"which occupies {last:g}..{bottom:g}")


def main() -> None:
    src = MAIN.read_text(encoding="utf-8")
    defines = {m.group(1): int(m.group(2))
               for m in re.finditer(r'#define (\w+) (\d+)u?\n', src)}
    problems: list[str] = []
    found = screens(src)
    if len(found) < 15:
        raise SystemExit(f"only found {len(found)} screens; the parser is probably broken")
    for name, body in found.items():
        check_literals(name, body, problems)
        check_rows(name, body, defines, problems)
    if problems:
        fail(problems)
    print(f"  {len(found)} screens draw inside the frame, with no row list over its counter")
    print("layout check: PASS")


if __name__ == "__main__":
    main()

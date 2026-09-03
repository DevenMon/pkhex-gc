#!/usr/bin/env python3
"""Check that every screen's footer matches the buttons it actually reads.

Two failures this catches, both of which shipped: a footer advertising a
button no handler looks at, and a handler acting on a button the footer never
mentions. The second is the worse one - it is how the event flags screen ended
up with four working controls and a footer listing two of them.

Screens are paired by the switch statements in main.c: `case UI_X: show_x()`
gives the footer, `case UI_X: handle_x(down)` gives the buttons read.
"""
from __future__ import annotations
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT / "source" / "main.c"

# Buttons the footers name, and the PAD_ constants each stands for. A footer
# may write STICK or DPAD or spell out a single direction; all mean the same
# reads, because the stick's directions are folded onto the D-pad's bits before
# a handler sees them. The two are separate inputs - the stick steps one and
# the D-pad ten - but a handler reads one set of bits either way.
FOOTER_ALIASES = {
    "STICK": {"BUTTON_UP", "BUTTON_DOWN", "BUTTON_LEFT", "BUTTON_RIGHT"},
    "DPAD": {"BUTTON_UP", "BUTTON_DOWN", "BUTTON_LEFT", "BUTTON_RIGHT"},
    "UP": {"BUTTON_UP"}, "DOWN": {"BUTTON_DOWN"},
    "LEFT": {"BUTTON_LEFT"}, "RIGHT": {"BUTTON_RIGHT"},
    "A": {"BUTTON_A"}, "B": {"BUTTON_B"}, "X": {"BUTTON_X"}, "Y": {"BUTTON_Y"},
    "L": {"TRIGGER_L"}, "R": {"TRIGGER_R"}, "Z": {"TRIGGER_Z"},
}
DIRECTIONS = {"BUTTON_UP", "BUTTON_DOWN", "BUTTON_LEFT", "BUTTON_RIGHT"}
# Named in footers but not read through the button mask.
NOT_BUTTONS = {"START", "CSTICK"}


# L and R are analog triggers: they have a partial-press region, they stick,
# and a press can hover at the digital threshold. Every job they ever held has
# moved to the D-pad, which is a digital direction control that the stick no
# longer shadows and is the right shape for stepping sideways. So the rule is
# now absolute rather than a vocabulary: nothing is on them at all.


def check_trigger_labels(text: str) -> list[str]:
    problems = []
    for tag in re.findall(r"\[([LR])\]", text):
        problems.append(f"a footer still offers [{tag}]: the analog triggers "
                        f"carry nothing, the D-pad steps sideways")
    for read in re.findall(r"PAD_TRIGGER_([LR])", text):
        problems.append(f"a handler still reads PAD_TRIGGER_{read}: the analog "
                        f"triggers carry nothing, the D-pad steps sideways")
    return problems


def check_save_is_confirmed(text: str, handles: dict[str, str]) -> list[str]:
    """Overwriting the save file goes through the confirmation screen."""
    problems = []
    for name, body in handles.items():
        if "save_in_place()" in body and name != "handle_confirm_save":
            problems.append(f"{name} writes the save without confirming; "
                            f"call request_save() instead")
    return problems


def check_b_goes_back(handles: dict[str, str]) -> list[str]:
    return [f"{name} never reads B, so there is no way back"
            for name, body in sorted(handles.items())
            if "PAD_BUTTON_B" not in body]


def bodies(text: str, prefix: str) -> dict[str, str]:
    out = {}
    for m in re.finditer(r"^static \w[\w ]*\*?(" + prefix + r"\w+)\(([^)]*)\) \{$",
                         text, re.M):
        start = m.end()
        depth = 1
        i = start
        while depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        out[m.group(1)] = text[start:i]
    return out


def screens(text: str) -> list[tuple[str, str, str]]:
    show = dict(re.findall(r"case (UI_\w+): (show_\w+)\(\); break;", text))
    handle = dict(re.findall(r"case (UI_\w+): (handle_\w+)\(down\); break;", text))
    return [(m, show[m], handle[m]) for m in show if m in handle]


def footers(body: str, all_bodies: dict[str, str], depth: int = 0) -> list[str]:
    """Footer strings drawn by a show function, following one level of helper."""
    found = re.findall(r'draw_footer\(\s*"((?:[^"\\]|\\.)*)"', body)
    found += re.findall(r'draw_footer\([^;]*?\?\s*"((?:[^"\\]|\\.)*)"\s*:\s*"((?:[^"\\]|\\.)*)"',
                        body) and []
    # Ternary footers hold two strings; grab every literal in the call.
    for call in re.findall(r"draw_footer\((.*?)\);", body, re.S):
        found += re.findall(r'"((?:[^"\\]|\\.)*)"', call)
    if not found and depth == 0:
        for helper in re.findall(r"\b(show_\w+)\(", body):
            if helper in all_bodies:
                found += footers(all_bodies[helper], all_bodies, depth + 1)
    return list(dict.fromkeys(found))


def advertised(footer_strings: list[str]) -> set[str]:
    out: set[str] = set()
    for s in footer_strings:
        for tag in re.findall(r"\[([A-Z]+)\]", s):
            if tag in NOT_BUTTONS:
                continue
            if tag not in FOOTER_ALIASES:
                raise SystemExit(f"unknown footer tag [{tag}] in {s!r}")
            out |= FOOTER_ALIASES[tag]
    return out


def read(body: str) -> set[str]:
    out = set(re.findall(r"PAD_(BUTTON_[A-Z]+|TRIGGER_[A-Z])", body))
    # nav_index() moves a wrapping selection: it reads up and down itself, so a
    # handler that calls it is reading the directions without naming them.
    if "nav_index(down" in body:
        out |= {"BUTTON_UP", "BUTTON_DOWN"}
    return out


def main() -> None:
    text = MAIN.read_text()
    show_bodies = bodies(text, "show_")
    handle_bodies = bodies(text, "handle_")

    problems: list[str] = []
    checked = 0
    for mode, show, handle in sorted(screens(text)):
        if show not in show_bodies or handle not in handle_bodies:
            problems.append(f"{mode}: could not find {show} or {handle}")
            continue
        checked += 1
        strings = footers(show_bodies[show], show_bodies)
        if not strings:
            problems.append(f"{mode}: {show} draws no footer")
            continue

        says = advertised(strings)
        does = read(handle_bodies[handle])

        # A footer saying DPAD covers whichever directions the screen uses, so
        # the directions are checked as a group: some must be advertised if any
        # are read, and none advertised if none are read. Every other button is
        # matched exactly.
        said_dir, did_dir = says & DIRECTIONS, does & DIRECTIONS
        if did_dir and not said_dir:
            problems.append(f"{mode}: {handle} moves on the D-pad, footer never says so")
        if said_dir and not did_dir:
            problems.append(f"{mode}: footer offers the D-pad, {handle} ignores it")

        for button in sorted(does - says - DIRECTIONS):
            problems.append(f"{mode}: {handle} acts on {button}, footer never says so")
        for button in sorted(says - does - DIRECTIONS):
            problems.append(f"{mode}: footer offers {button}, {handle} ignores it")

    problems += check_trigger_labels(text)
    problems += check_save_is_confirmed(text, handle_bodies)
    problems += check_b_goes_back(handle_bodies)

    if checked < 20:
        raise SystemExit(f"only paired {checked} screens; the switch parsing is wrong")

    if problems:
        print("control map check: FAIL")
        for p in problems:
            print("  " + p)
        sys.exit(1)
    print(f"  {checked} screens: footers match their handlers, B always goes back,")
    print( "    the analog triggers carry nothing at all, and saving is confirmed")
    print("control map check: PASS")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Patterns that only break on the compiler this project actually ships with.

devkitPPC's gcc is newer than the one on most development machines, and the
build treats warnings as errors, so a few constructs compile cleanly here and
fail in CI. Each one below has cost a red build at least once; grepping for
them is not elegant, but it catches them at the point they are written rather
than three commits later.
"""
from __future__ import annotations
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCES = sorted(p for d in ("source", "include", "tests", "gba-agent")
                 for p in (ROOT / d).rglob("*") if p.suffix in (".c", ".h"))

CHECKS = [
    (
        # -Werror=sign-compare: "comparison of promoted bitwise complement of
        # an unsigned value with unsigned". Naming the complement first is all
        # it takes.
        re.compile(r"[!=]=\s*\(uint\d+_t\)\s*\([^)]*\^\s*0x[Ff][Ff]"),
        "compares against an inlined complement; name the value in a const first",
    ),
    (
        # A float exponent, not a hex constant plus something: 0xAE+i and
        # 0x23E+8 both lex as malformed floating literals.
        re.compile(r"0x[0-9A-Fa-f]*[Ee][+-][0-9A-Za-z_]"),
        "hex literal ending in E followed by +/- lexes as a float exponent; add spaces",
    ),
]


def main() -> None:
    problems = []
    for path in SOURCES:
        for lineno, line in enumerate(path.read_text().splitlines(), 1):
            if line.lstrip().startswith(("*", "/*", "//")):
                continue        # the notes describing these patterns
            for pattern, why in CHECKS:
                if pattern.search(line):
                    rel = path.relative_to(ROOT)
                    problems.append(f"  {rel}:{lineno}: {why}\n      {line.strip()}")

    if problems:
        print("source hygiene check: FAIL")
        print("\n".join(problems))
        sys.exit(1)
    print(f"  {len(SOURCES)} files: no constructs that only devkitPPC's gcc rejects")
    print("source hygiene check: PASS")


if __name__ == "__main__":
    main()

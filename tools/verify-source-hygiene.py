#!/usr/bin/env python
"""
Higijena izvornog koda.

1. C++ izvori moraju biti CIST ASCII.

   Razlog nije estetika. MSVC podrazumevano cita izvore u lokalnoj code page,
   a mi gradimo i pod /utf-8; komentar sa dijakritikom se u tom rascepu tiho
   pokvari, a jednom se vec dogodilo da se u srpski tekst uvuce cirilicno
   "ено" umesto latinicnog "eno" - vizuelno neprimetno, i nista ga ne bi
   uhvatilo.

   Dokumentacija (.md) sme i treba da koristi pun pravopis; ovo vazi samo za
   kod.

2. Nijedan izvor ne sme ukljuciti hp3900 referencu.

   third_party/hp3900-reference/ je izvor istine za citanje, ne za
   kompajliranje. Ako se ikada nadje u #include-u, GPL provenance prica
   prestaje da vazi onako kako je zapisana u NOTICE-hp3900.md.

Izlaz 0 = sve cisto.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SOURCE_DIRS = ("native", "tests")
SOURCE_SUFFIXES = (".cpp", ".h", ".def")

# Generisani fajl se ne uredjuje rucno, ali i on mora biti ASCII.
SKIP_NAMES: set[str] = set()

FORBIDDEN_INCLUDE = "hp3900"


def sources() -> list[Path]:
    found: list[Path] = []
    for directory in SOURCE_DIRS:
        base = ROOT / directory
        if not base.is_dir():
            continue
        for suffix in SOURCE_SUFFIXES:
            found.extend(base.rglob("*" + suffix))
    return sorted(p for p in found if p.name not in SKIP_NAMES)


def check_ascii(path: Path) -> list[str]:
    problems = []
    text = path.read_text(encoding="utf-8", errors="replace")
    for number, line in enumerate(text.splitlines(), 1):
        offenders = sorted({c for c in line if ord(c) > 127})
        if offenders:
            rendered = " ".join("U+%04X (%s)" % (ord(c), c) for c in offenders)
            problems.append("%s:%d  ne-ASCII: %s" % (path.relative_to(ROOT), number, rendered))
    return problems


def check_no_reference_include(path: Path) -> list[str]:
    problems = []
    text = path.read_text(encoding="utf-8", errors="replace")
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.lstrip()
        if not stripped.startswith("#include"):
            continue
        if FORBIDDEN_INCLUDE in stripped.lower():
            problems.append("%s:%d  ukljucuje hp3900 referencu: %s"
                            % (path.relative_to(ROOT), number, stripped))
    return problems


def main() -> int:
    files = sources()
    if not files:
        print("nijedan izvor nije nadjen - da li je putanja tacna?")
        return 1

    problems: list[str] = []
    for path in files:
        problems.extend(check_ascii(path))
        problems.extend(check_no_reference_include(path))

    print("provereno fajlova: %d" % len(files))
    if problems:
        print("")
        for problem in problems:
            print("  " + problem)
        print("")
        print("NEUSPEH: %d problema" % len(problems))
        return 1

    print("OK: cist ASCII, nijedan izvor ne ukljucuje referencu")
    return 0


if __name__ == "__main__":
    sys.exit(main())

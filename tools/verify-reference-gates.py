#!/usr/bin/env python
"""
Proverava G2710-0 acceptance kapije protiv hp3900 reference izvora.

GATE A  Svaka funkcija u G2710 code path-u je pokrivena ekstrakcijom.
GATE B  Svaki control transfer u G2710 code path-u koristi vendor request
        type 0x40 ili 0xC0 (inace pada pretpostavka o usbscan.sys transportu).

Izlaz 0 = obe kapije prolaze. Namenjeno CI-ju; nije jednokratna provera.

Pokretanje:  python tools/verify-reference-gates.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from hp3900_parse import REF, strip_comments  # noqa: E402

# hp3800_* familija koju extract-hp3900-profile.py transkribuje.
EXTRACTED = {
    "hp3800_refvoltages", "hp3800_offset", "hp3800_effectivepixel",
    "hp3800_gainoffset", "hp3800_checkstable", "hp3800_fixedpwm",
    "hp3800_vrefs", "hp3800_scanmodes", "hp3800_timing_get",
    "hp3800_motormove", "hp3800_motor", "hp3800_shading_cut",
    "hp3800_wrefs", "hp3800_calibreflective", "hp3800_calibtransparent",
    "hp3800_calibnegative", "srt_hp3800_scanparam_get",
    "srt_hp3800_platform_get",
}

ALLOWED_REQUEST_TYPES = {"0x40", "0xc0"}
EXPECTED_REQUEST = "0x04"


def gate_a() -> list:
    """Svaki `case HPG2710:` dispatch mora voditi na ekstraktovanu funkciju."""
    lines = (REF / "hp3900_config.c").read_text(errors="replace").splitlines()
    failures, sites = [], 0

    for i, line in enumerate(lines):
        if not re.match(r"\s*case\s+HPG2710\s*:", line):
            continue
        sites += 1
        callee = None
        for k in range(i + 1, min(i + 8, len(lines))):
            m = re.search(r"\b(\w+)\s*\(", lines[k])
            if m and m.group(1) not in ("if", "switch", "for", "while",
                                        "sizeof", "memcpy"):
                callee = m.group(1)
                break
            if re.match(r"\s*(break|case)\b", lines[k]):
                break
        if callee is None:
            failures.append("linija %d: HPG2710 grana bez poziva" % (i + 1))
        elif callee.startswith(("hp3800", "srt_hp3800")) and callee not in EXTRACTED:
            failures.append("linija %d: %s nije ekstraktovan" % (i + 1, callee))

    if sites == 0:
        failures.append("nijedan HPG2710 dispatch nije nadjen - referenca se promenila?")
    print("GATE A: %d HPG2710 dispatch sajtova, %d nepokrivenih" % (sites, len(failures)))
    return failures


def gate_b() -> list:
    """Svi control transferi moraju biti vendor 0x40 / 0xC0 sa bRequest 0x04,
    i nijedan poziv ne sme zaobici USB sloj."""
    failures = []
    # Argumenti su razdvojeni komentarima preko vise linija, pa ih prvo skidamo.
    usb = strip_comments((REF / "hp3900_usb.c").read_text(errors="replace"))

    calls = re.findall(
        r"(?:sanei_)?usb_control_msg\s*\(\s*[^,]+,\s*(0x[0-9a-fA-F]+)\s*,"
        r"\s*(0x[0-9a-fA-F]+)\s*,", usb)
    if not calls:
        failures.append("nijedan control_msg poziv nije nadjen u hp3900_usb.c")

    for rtype, request in calls:
        if rtype.lower() not in ALLOWED_REQUEST_TYPES:
            failures.append("nedozvoljen bmRequestType %s" % rtype)
        if request.lower() != EXPECTED_REQUEST:
            failures.append("neocekivan bRequest %s" % request)

    # Nijedan drugi fajl ne sme zvati control_msg direktno.
    for path in sorted(REF.glob("hp3900*.c")):
        if path.name == "hp3900_usb.c":
            continue
        text = strip_comments(path.read_text(errors="replace"))
        if re.search(r"(?:sanei_)?usb_control_msg\s*\(", text):
            failures.append("%s zaobilazi USB sloj" % path.name)
        for fn in ("usb_ctl_read", "usb_ctl_write"):
            if re.search(r"\b" + fn + r"\s*\(", text):
                failures.append("%s poziva %s direktno" % (path.name, fn))

    print("GATE B: %d control transfera, svi vendor 0x40/0xC0 req 0x04, "
          "%d prekrsaja" % (len(calls), len(failures)))
    return failures


def main() -> int:
    failures = gate_a() + gate_b()
    if failures:
        print("\nFAIL:")
        for f in failures:
            print("  " + f)
        return 1
    print("\nOBE KAPIJE PROLAZE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

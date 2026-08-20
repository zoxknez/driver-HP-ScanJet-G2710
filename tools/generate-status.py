#!/usr/bin/env python3
"""Generise docs/STATUS.md iz stvarnih rezultata, ne iz secanja.

Tri izvora, i nijedan nije rucno kucan:

  1. ctest      sta je zaista zeleno u ovom trenutku
  2. g2710ctl capabilities --json
                sta binarni fajl zaista tvrdi da ume
  3. qualification/test-results.json
                sta je hardver zaista potvrdio; ako fajla nema, treca kolona
                ostaje prazna i to je tacan opis stanja

Poenta je da STATUS.md ne moze da tvrdi vise nego sto testovi pokazuju. Zato se
generise, umesto da se odrzava.

Upotreba:
    python tools/generate-status.py [--build-dir build] [--config Release]
    python tools/generate-status.py --check      # samo proveri da je azuran
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, "docs", "STATUS.md")
HARDWARE_RESULTS = os.path.join(ROOT, "qualification", "test-results.json")

# Faze iz MASTER plana, i test fajlovi koji ih dokazuju.
#
# Imena gtest suite-ova se ne prepisuju ovde - citaju se iz samih izvora. Tako
# jedno preimenovanje ne moze da ostavi fazu tiho praznu; umesto toga alat pada
# uz spisak nepokrivenih suite-ova.
PHASES = [
    ("G2710-0", "Reference truth extraction", [
        "unit/profile_generated_test.cpp"]),
    ("G2710-1", "WDK, skeleton, transport", [
        "unit/transport_provider_test.cpp", "unit/wia_clsid_test.cpp",
        "unit/result_test.cpp"]),
    ("G2710-2", "RTS8822 core", [
        "golden/register_sequence_test.cpp", "golden/dma_sequence_test.cpp",
        "golden/gpio_sequence_test.cpp", "golden/sensor_timing_test.cpp",
        "golden/motor_sequence_test.cpp", "golden/motor_steps_test.cpp"]),
    ("G2710-3", "Simulator uredjaja", [
        "unit/sim_transport_test.cpp", "unit/simulator_test.cpp"]),
    ("G2710-4", "Stanja, MotionGuard, arbitraza", [
        "unit/safety_level_test.cpp", "unit/safety_ceiling1_test.cpp",
        "unit/device_arbiter_test.cpp", "unit/motion_guard_test.cpp",
        "unit/device_lifecycle_test.cpp"]),
    ("G2710-5", "Kalibracija", [
        "golden/lamp_test.cpp", "integration/calibration_test.cpp"]),
    ("G2710-6", "Obrada slike", [
        "golden/line_offset_test.cpp", "golden/pixel_format_test.cpp"]),
    ("G2710-7", "Planer i tabela mogucnosti", [
        "golden/scan_planner_test.cpp"]),
]

# Testovi koji nemaju gtest suite - registruju se u CMake-u pod svojim imenom.
# `profile_generated` je compile-time provera: kompajliranje JESTE test.
STANDALONE = {
    "unit/profile_generated_test.cpp": "profile_generated",
    "unit/safety_ceiling1_test.cpp": "safety_ceiling_1",
}

GATE_REASONS = {
    "IMPLEMENTED": "ceka hardversku kvalifikaciju",
    "REFERENCE_VALIDATED": "poklapa se sa hp3900; ceka hardversku kvalifikaciju",
    "HARDWARE_VALIDATED": "potvrdjeno na uredjaju",
    "NOT_IMPLEMENTED": "nije implementirano",
}

SUITE_PATTERN = re.compile(r"^TEST(?:_F|_P)?\(\s*([A-Za-z0-9_]+)", re.MULTILINE)


def suites_in(source):
    """Imena gtest suite-ova u jednom test fajlu."""
    path = os.path.join(ROOT, "tests", source)
    if not os.path.exists(path):
        raise SystemExit("nema test fajla %s - azuriraj PHASES" % source)
    with open(path, encoding="utf-8") as handle:
        return set(SUITE_PATTERN.findall(handle.read()))


def phase_index():
    """suite ili samostalno ime -> oznaka faze."""
    owner = {}
    for identifier, _title, sources in PHASES:
        for source in sources:
            for suite in suites_in(source):
                if suite in owner and owner[suite] != identifier:
                    raise SystemExit("suite %s pripada i %s i %s"
                                     % (suite, owner[suite], identifier))
                owner[suite] = identifier
            standalone = STANDALONE.get(source)
            if standalone:
                owner[standalone] = identifier
    return owner


def run_ctest(build_dir, config):
    """Vraca listu (ime, prosao) za svaki test."""
    handle, junit = tempfile.mkstemp(suffix=".xml")
    os.close(handle)
    try:
        subprocess.run(
            ["ctest", "--test-dir", build_dir, "-C", config, "--output-junit", junit],
            cwd=ROOT, capture_output=True, text=True,
        )
        tree = ElementTree.parse(junit)
    finally:
        os.unlink(junit)

    results = []
    for case in tree.iter("testcase"):
        name = case.get("name", "")
        skipped = case.find("skipped") is not None
        failed = case.find("failure") is not None or case.find("error") is not None
        results.append((name, not failed and not skipped))
    return results


def run_capabilities(build_dir, config):
    exe = os.path.join(build_dir, "native", "cli", config, "g2710ctl.exe")
    if not os.path.exists(exe):
        raise SystemExit("nema %s - izgradi metu g2710ctl" % exe)
    output = subprocess.run([exe, "capabilities", "--json"],
                            capture_output=True, text=True, check=True).stdout
    return json.loads(output)


def load_hardware_results():
    if not os.path.exists(HARDWARE_RESULTS):
        return None
    with open(HARDWARE_RESULTS, encoding="utf-8") as handle:
        return json.load(handle)


def split_by_phase(tests, owner):
    """Grupise ctest imena po fazama; nepokriveno je greska, ne tisina."""
    grouped = {identifier: [] for identifier, _title, _sources in PHASES}
    orphans = []
    for name, ok in tests:
        key = name.split(".", 1)[0]
        identifier = owner.get(key) or owner.get(name)
        if identifier is None:
            orphans.append(name)
        else:
            grouped[identifier].append((name, ok))
    if orphans:
        raise SystemExit("testovi koje nijedna faza ne polaze:\n  "
                         + "\n  ".join(sorted(set(orphans))))
    return grouped


def render(tests, grouped, capabilities, hardware):
    total = len(tests)
    passed = sum(1 for _, ok in tests if ok)

    lines = []
    lines.append("<!-- GENERISANO: tools/generate-status.py. Ne menjati rucno. -->")
    lines.append("")
    lines.append("# STATUS")
    lines.append("")
    lines.append("Jedini izvor istine o tome sta je dokazano. Sve u ovom fajlu potice "
                 "iz `ctest`, iz `g2710ctl capabilities --json` i iz izvestaja "
                 "hardverske kvalifikacije - nista nije upisano rucno.")
    lines.append("")
    lines.append("Uredjaj: `%s`" % capabilities["device"])
    lines.append("")
    lines.append("## Testovi")
    lines.append("")
    lines.append("**%d/%d prolazi.**" % (passed, total))
    lines.append("")
    lines.append("| Faza | Oblast | Stanje | Testovi |")
    lines.append("|---|---|---|---|")
    for identifier, title, _sources in PHASES:
        matched = grouped[identifier]
        ok = sum(1 for _, passed in matched if passed)
        badge = "**PASS**" if matched and ok == len(matched) else "**PAO**"
        lines.append("| %s | %s | %s | %d/%d |" % (identifier, title, badge, ok, len(matched)))
    lines.append("")

    if total != passed:
        lines.append("Padaju:")
        lines.append("")
        for name, ok in tests:
            if not ok:
                lines.append("- `%s`" % name)
        lines.append("")

    lines.append("## Rezolucije")
    lines.append("")
    lines.append("Tri statusa, ne jedan. `IMPLEMENTED` znaci da kod postoji; "
                 "`REFERENCE_VALIDATED` da se ponasa kao hp3900; "
                 "`HARDWARE_VALIDATED` da je potvrdjeno na uredjaju.")
    lines.append("")
    lines.append("| dpi | Izvor | Skenira na | Poravnanje | Status | Oglasava se |")
    lines.append("|---|---|---|---|---|---|")
    for row in capabilities["resolutions"]:
        source = row["origin"]
        if row["origin"] == "resize":
            source = "resize iz %d" % row["sourceDpi"]
        lines.append("| %d | %s | %d dpi | %s | `%s` | %s |" % (
            row["dpi"], source, row["nativeDpi"], row["alignment"],
            row["level"], "da" if row["advertisable"] else "**ne**"))
    lines.append("")

    for row in capabilities["resolutions"]:
        if row["note"]:
            lines.append("- **%d dpi** - %s" % (row["dpi"], row["note"]))
    lines.append("")

    lines.append("## Dubine")
    lines.append("")
    lines.append("| Bita po kanalu | Status | Napomena |")
    lines.append("|---|---|---|")
    for depth in capabilities["depths"]:
        lines.append("| %d | `%s` | %s |" % (
            depth["bits"], depth["level"], depth["note"] or GATE_REASONS.get(depth["level"], "")))
    lines.append("")

    lines.append("## Sta WIA i TWAIN oglasavaju")
    lines.append("")
    advertisable = capabilities["advertisable"]
    if advertisable:
        lines.append("Rezolucije: %s" % ", ".join(str(dpi) for dpi in advertisable))
    else:
        lines.append("**Nijedna rezolucija.**")
        lines.append("")
        lines.append("To nije propust nego pravilo iz MASTER plana: oglasava se "
                     "iskljucivo ono sto je proslo hardversku kvalifikaciju, a skener "
                     "jos nije bio prikljucen. Kod zna da skenira svih devet "
                     "rezolucija i to se moze pozvati kroz dijagnostiku "
                     "(`allowUnqualified`), ali se korisniku ne nudi.")
    lines.append("")

    lines.append("## Hardverska kvalifikacija")
    lines.append("")
    if hardware is None:
        lines.append("Nema izvestaja. Ocekuje se `qualification/test-results.json` "
                     "iz paketa koji se salje na testiranje; do tada je treca kolona "
                     "svake mogucnosti prazna.")
        lines.append("")
        lines.append("| Test | Nivo | Stanje |")
        lines.append("|---|---|---|")
        for identifier, title, level in [
            ("H1", "Instalacija drajvera i enumeracija", 1),
            ("H2", "Chipset ID i read-only registri", 1),
            ("H3", "Lampa i warmup", 2),
            ("H4", "HOME i osnovno kretanje", 3),
            ("H5", "RAW CCD akvizicija", 4),
            ("H6", "300 dpi RGB flatbed", 5),
            ("H7", "Puna kalibracija", 5),
            ("H8", "75/150/600, zatim 1200/2400 i 48-bit", 5),
            ("H9", "Sivo, lineart, preview, crop", 5),
            ("H10", "Fizicka dugmad", 5),
            ("H11", "WIA integracija", 5),
            ("H12", "TWAIN x64 i x86", 5),
            ("H13", "Stres i otkazi", 5),
        ]:
            lines.append("| %s %s | %d | ceka |" % (identifier, title, level))
    else:
        lines.append("| Test | Stanje | Datum |")
        lines.append("|---|---|---|")
        for entry in hardware.get("tests", []):
            lines.append("| %s | %s | %s |" % (
                entry.get("id", "?"), entry.get("result", "?"), entry.get("date", "")))
    lines.append("")

    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--check", action="store_true",
                        help="ne pise nista; izlazi sa 1 ako STATUS.md nije azuran")
    arguments = parser.parse_args()

    build_dir = os.path.join(ROOT, arguments.build_dir)
    tests = run_ctest(build_dir, arguments.config)
    capabilities = run_capabilities(build_dir, arguments.config)
    hardware = load_hardware_results()
    grouped = split_by_phase(tests, phase_index())
    content = render(tests, grouped, capabilities, hardware)

    if arguments.check:
        existing = ""
        if os.path.exists(OUTPUT):
            with open(OUTPUT, encoding="utf-8") as handle:
                existing = handle.read()
        if existing != content:
            print("docs/STATUS.md nije azuran - pokreni tools/generate-status.py")
            return 1
        print("docs/STATUS.md je azuran")
        return 0

    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
    passed = sum(1 for _, ok in tests if ok)
    print("docs/STATUS.md: %d/%d testova, %d rezolucija, %d oglaseno"
          % (passed, len(tests), len(capabilities["resolutions"]),
             len(capabilities["advertisable"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

# HP ScanJet G2710 — Native Windows 10/11 Driver

Moderan x64 scanning stack za HP ScanJet G2710 (`USB\VID_03F0&PID_2805`,
Realtek RTS8822BL-03A) na Windows 10 i 11, bez HP legacy softvera.

WIA 2.0 drajver · TWAIN (x86 + x64) · fizička dugmad · sopstvena aplikacija.

Licenca: **GPL-2.0-or-later** — vidi [NOTICE-hp3900.md](NOTICE-hp3900.md).

## Status

| Faza | Stanje |
|---|---|
| G2710-0 Reference truth | **završen** — Gate A i Gate B PASS |
| G2710-1 WDK / skeleton / transport | **završen** — gate PASS |
| G2710-2 RTS8822 core | **završen** — gate PASS |
| G2710-3 Device simulator | motor, lampe, CCD, otkazi — nedostaje scan putanja |
| G2710-4 State machine / MotionGuard | **završen** |
| G2710-5 Kalibracija | lampa, warmup, shading — gate PASS |
| G2710-6 Image pipeline | line offset, gamma, sivo, lineart, dubina |
| G2710-7 Planer i mogucnosti | **zavrsen** — 306/306, [STATUS.md](docs/STATUS.md) generisan |

```bash
python tools/verify-reference-gates.py && python tools/verify-source-hygiene.py
```

`docs/STATUS.md` se **generise**, ne odrzava — jedini je izvor istine o tome sta je
dokazano:

```bash
python tools/generate-status.py
```

## Build

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release && ctest --test-dir build -C Release
```

```bash
build/native/cli/Release/g2710ctl.exe info
```

```bash
build/native/cli/Release/g2710ctl.exe capabilities
```

Driver paket i potpisivanje:

```bash
powershell -File driver/sign/make-dev-cert.ps1 -Install
```

```bash
powershell -File driver/sign/sign-package.ps1 -PackageDir build/package
```

Dokumentacija: [PROTOCOL-RTS8822.md](docs/PROTOCOL-RTS8822.md) ·
[G2710-PROFILE.md](docs/G2710-PROFILE.md) · [SAFETY.md](docs/SAFETY.md) ·
[SIGNING.md](docs/SIGNING.md) ·
[REFERENCE-DEFECTS.md](docs/REFERENCE-DEFECTS.md) ·
[STATUS.md](docs/STATUS.md)

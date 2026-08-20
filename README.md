# HP ScanJet G2710 — Native Windows 10/11 Driver

Moderan x64 scanning stack za HP ScanJet G2710 (`USB\VID_03F0&PID_2805`,
Realtek RTS8822BL-03A) na Windows 10 i 11, bez HP legacy softvera.

WIA 2.0 drajver · TWAIN (x86 + x64) · fizička dugmad · sopstvena aplikacija.

Licenca: **GPL-2.0-or-later** — vidi [NOTICE-hp3900.md](NOTICE-hp3900.md).

## Status

| Faza | Stanje |
|---|---|
| G2710-0 Reference truth | **završen** — Gate A i Gate B PASS |
| G2710-1 WDK / skeleton / transport | u toku — build, transport, arbiter, safety |

```bash
python tools/verify-reference-gates.py
```

## Build

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release && ctest --test-dir build -C Release
```

Dokumentacija: [PROTOCOL-RTS8822.md](docs/PROTOCOL-RTS8822.md) ·
[G2710-PROFILE.md](docs/G2710-PROFILE.md) · [SAFETY.md](docs/SAFETY.md)

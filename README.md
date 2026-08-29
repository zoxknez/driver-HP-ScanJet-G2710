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
| G2710-3 Simulator i akvizicija | **završen** — simulator zaista skenira kroz bulk |
| G2710-4 State machine / MotionGuard | **završen** |
| G2710-5 Kalibracija | **završen** — lampa, shading, ADC gain/offset, keš |
| G2710-6 Image pipeline | line offset, gamma, sivo, lineart, dubina |
| G2710-7 Planer i sesija skeniranja | **završen** — `g2710ctl scan` daje sliku |
| G2710-9 WIA minidriver | `IStiUSD` + `IWiaMiniDrv` — čeka H11 na hardveru |
| G2710-11 Kvalifikacioni paket | **završen** — wizard, instalacija, dijagnostika, ZIP |

Sve offline kapije, jednim pozivom — isti ulaz koji koristi i CI:

```bash
powershell -File tools/verify-all.ps1
```

Samo jeftine kapije (poreklo koda i higijena izvora, manje od sekunde) — ovo
pokreću i skripte za pakovanje pre nego što bilo šta pošalju:

```bash
powershell -File tools/verify-all.ps1 -GatesOnly
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

Skeniranje nad simulatorom, bez skenera:

```bash
build/native/cli/Release/g2710ctl.exe scan --transport sim --safety-level 5 --dpi 300 --mode color --out slika.ppm
```

Hardverska kvalifikacija (H1–H13) nad simulatorom, kao proba isporuke:

```bash
build/native/cli/Release/g2710ctl.exe qualify --transport sim --safety-level 5 --out test-results.json
```

Wizard koji ide prijatelju (WPF, .NET 10):

```bash
dotnet test managed/G2710.sln -c Release
```

Driver paket i potpisivanje:

```bash
powershell -File driver/sign/make-dev-cert.ps1 -Install
```

```bash
powershell -File driver/sign/sign-package.ps1 -PackageDir build/package
```

## Paket za testiranje na hardveru

Jedna komanda pravi ZIP koji se šalje: wizard, `g2710ctl`, potpisan drajver,
`install.ps1`, `collect-diagnostics.ps1` i uputstvo na srpskom.

```bash
powershell -File tools/build-qualification-package.ps1 -SafetyCeiling 2
```

**Plafon bezbednosti se ugrađuje u binarni fajl, ne čita se pri pokretanju.**
Paket sa plafonom ispod 3 uopšte nema preveden motorni kod, pa se na tuđem
računaru ne može „otključati“. Skript to i proverava — čita `g2710ctl info` iz
sveže izgrađenog binarnog fajla i odbija da spakuje nešto drugo nego što je
traženo.

Tvrdnja da motorni kod zaista nedostaje meri se posebno, nad simbolima u
biblioteci:

```bash
powershell -File tools/verify-safety-ceiling.ps1
```

Dokumentacija: [PROTOCOL-RTS8822.md](docs/PROTOCOL-RTS8822.md) ·
[G2710-PROFILE.md](docs/G2710-PROFILE.md) · [SAFETY.md](docs/SAFETY.md) ·
[SIGNING.md](docs/SIGNING.md) ·
[ROADMAP.md](docs/ROADMAP.md) ·
[REFERENCE-DEFECTS.md](docs/REFERENCE-DEFECTS.md) ·
[STATUS.md](docs/STATUS.md)

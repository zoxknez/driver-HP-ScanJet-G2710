<!-- GENERISANO: tools/generate-status.py. Ne menjati rucno. -->

# STATUS

Jedini izvor istine o tome sta je dokazano. Sve u ovom fajlu potice iz `ctest`, iz `dotnet test`, iz `g2710ctl capabilities --json` i iz izvestaja hardverske kvalifikacije - nista nije upisano rucno.

Uredjaj: `03F0:2805`

## Testovi

**688/701 prolazi, 13 preskoceno.**

Preskoceni testovi nisu pali. U izdanju se provera WIA vrednosti preskace jer ponuda nema nijednu hardverski potvrdjenu rezoluciju; ista provera se izvrsava u kvalifikacionom build-u (`wia_qualification`).

| Faza | Oblast | Stanje | Prolazi | Preskoceno |
|---|---|---|---|---|
| G2710-0 | Reference truth extraction | **PASS** | 1/1 | - |
| G2710-1 | WDK, skeleton, transport | **PASS** | 18/18 | - |
| G2710-2 | RTS8822 core | **PASS** | 111/111 | - |
| G2710-3 | Simulator i akvizicija | **PASS** | 57/57 | - |
| G2710-4 | Stanja, MotionGuard, arbitraza | **PASS** | 60/60 | - |
| G2710-5 | Kalibracija | **PASS** | 72/72 | - |
| G2710-6 | Obrada slike i izlaz | **PASS** | 84/84 | - |
| G2710-7 | Planer i sesija skeniranja | **PASS** | 53/53 | - |
| G2710-8 | C ABI, Interop i aplikacija | **PASS** | 109/109 | - |
| G2710-9 | WIA minidriver | **PASS** | 56/56 | 13 |
| G2710-10 | TWAIN Data Source | **PASS** | 8/8 | - |
| G2710-11 | Kvalifikacioni paket | **PASS** | 59/59 | - |

## Rezolucije

Tri statusa, ne jedan. `IMPLEMENTED` znaci da kod postoji; `REFERENCE_VALIDATED` da se ponasa kao hp3900; `HARDWARE_VALIDATED` da je potvrdjeno na uredjaju.

| dpi | Izvor | Skenira na | Poravnanje | Status | Oglasava se |
|---|---|---|---|---|---|
| 50 | resize iz 150 | 150 dpi | hardware | `REFERENCE_VALIDATED` | **ne** |
| 75 | resize iz 150 | 150 dpi | hardware | `REFERENCE_VALIDATED` | **ne** |
| 100 | resize iz 150 | 150 dpi | hardware | `REFERENCE_VALIDATED` | **ne** |
| 150 | native | 150 dpi | hardware | `REFERENCE_VALIDATED` | **ne** |
| 200 | resize iz 300 | 300 dpi | hardware | `REFERENCE_VALIDATED` | **ne** |
| 300 | native | 300 dpi | hardware | `REFERENCE_VALIDATED` | **ne** |
| 600 | native | 600 dpi | hardware | `REFERENCE_VALIDATED` | **ne** |
| 1200 | native | 1200 dpi | software | `IMPLEMENTED` | **ne** |
| 2400 | native | 2400 dpi | software | `IMPLEMENTED` | **ne** |

- **50 dpi** - smanjivanje iz 150 dpi; nema native red
- **75 dpi** - smanjivanje iz 150 dpi; nema native red
- **100 dpi** - smanjivanje iz 150 dpi; nema native red
- **150 dpi** - najniza native rezolucija
- **200 dpi** - smanjivanje iz 300 dpi; nema native red
- **600 dpi** - najvisa rezolucija na kojoj hardversko poravnanje redova jos staje u 6 bita
- **1200 dpi** - defekt D3: pomak plavog kanala se preliva iz 6-bitnog polja; ceka H8
- **2400 dpi** - defekt D3: prelivaju se i zeleni i plavi; ceka H8

## Dubine

| Bita po kanalu | Status | Napomena |
|---|---|---|
| 8 | `REFERENCE_VALIDATED` | poklapa se sa hp3900; ceka hardversku kvalifikaciju |
| 16 | `IMPLEMENTED` | 48-bit izlaz je qualification-gated; ceka H8 |

## Sta WIA i TWAIN oglasavaju

**Nijedna rezolucija.**

To nije propust nego pravilo iz MASTER plana: oglasava se iskljucivo ono sto je proslo hardversku kvalifikaciju, a skener jos nije bio prikljucen. Kod zna da skenira svih devet rezolucija i to se moze pozvati kroz dijagnostiku (`allowUnqualified`), ali se korisniku ne nudi.

## Hardverska kvalifikacija

Nema izvestaja. Ocekuje se `qualification/test-results.json` iz paketa koji se salje na testiranje; do tada je treca kolona svake mogucnosti prazna.

| Test | Nivo | Stanje |
|---|---|---|
| H1 Instalacija drajvera i enumeracija | 1 | ceka |
| H2 Chipset ID i read-only registri | 1 | ceka |
| H3 Lampa i warmup | 2 | ceka |
| H4 HOME i osnovno kretanje | 3 | ceka |
| H5 RAW CCD akvizicija | 4 | ceka |
| H6 300 dpi RGB flatbed | 5 | ceka |
| H7 Puna kalibracija | 5 | ceka |
| H8 75/150/600, zatim 1200/2400 i 48-bit | 5 | ceka |
| H9 Sivo, lineart, preview, crop | 5 | ceka |
| H10 Fizicka dugmad | 5 | ceka |
| H11 WIA integracija | 5 | ceka |
| H12 TWAIN x64 i x86 | 5 | ceka |
| H13 Stres i otkazi | 5 | ceka |


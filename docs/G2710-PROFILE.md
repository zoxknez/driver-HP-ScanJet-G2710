# HP ScanJet G2710 — device profile

Izvor: `third_party/hp3900-reference/` @ `951e8a5a`. Model id `HPG2710 = 0x07`.

---

## 0. Centralni nalaz — G2710 ≡ HP3800

U **svakoj** konfiguracionoj tabeli `hp3900_config.c` G2710 ima vrednosti
identične HP ScanJet 3800, i **svaka** `switch(dev_model)` grana dispečuje
`case HP3800: case HPG2710:` na istu `hp3800_*` funkciju.

Praktične posledice:

1. Kompletan G2710 code path je **18 funkcija** — `hp3800_*` familija.
2. Sve što je poznato o HP3800 (uključujući probleme) prenosi se direktno.
3. Ekstrakcija profila je dobro ograničen posao, ne pretraga po 425 KB koda.

| # | Funkcija | Uloga |
|---|---|---|
| 1 | `hp3800_refvoltages` | CCD referentni naponi (top/middle/bottom) |
| 2 | `hp3800_offset` | left/width za offset kalibraciju |
| 3 | `hp3800_effectivepixel` | efektivni broj piksela po rezoluciji |
| 4 | `hp3800_gainoffset` | ADC gain/offset po USB tipu |
| 5 | `hp3800_checkstable` | kriterijum stabilnosti lampe (warmup) |
| 6 | `hp3800_fixedpwm` | PWM lampe po scantype/USB |
| 7 | `hp3800_vrefs` | SER/LER po rezoluciji |
| 8 | `hp3800_scanmodes` | **tabela režima skeniranja** |
| 9 | `hp3800_timing_get` | CCD timing profili |
| 10 | `hp3800_motormove` | profili kretanja motora |
| 11 | `hp3800_motor` | motorne krive |
| 12 | `hp3800_shading_cut` | shading cut po kanalu |
| 13 | `hp3800_wrefs` | white refs po kanalu |
| 14 | `hp3800_calibreflective` | kalibracija flatbed |
| 15 | `hp3800_calibtransparent` | kalibracija TMA pozitiv |
| 16 | `hp3800_calibnegative` | kalibracija TMA negativ |
| 17 | `srt_hp3800_scanparam_get` | scan parametri |
| 18 | `srt_hp3800_platform_get` | platform parametri |

---

## 1. Identitet i hardver

| Polje | Vrednost | Izvor |
|---|---|---|
| USB VID / PID | `0x03F0` / `0x2805` | `config.c:373` |
| Chipset | `RTS8822BL-03A`, `CAP_EEPROM` | `config.c:410,448` |
| Senzor | `CCD_SENSOR`, Toshiba `TCD2905` | `config.c:644` |
| Optička rezolucija senzora | **2400** dpi | `config.c:644` |
| Rezolucija motora | **1200** dpi | `config.c:594` |
| Tip motora | `MT_OUTPUTSTATE` | `config.c:594` |
| Motor freq / speed | `30` / `800` | `config.c:594` |
| basemove / highmove / parkmove | `1` / `0` / `0` | `config.c:594` |
| Spectrum clock generator | enable `1`, mode `1`, clock `0` | `config.c:542` |

### CCD geometrija — ulaz za `LineOffsetCorrector`

| Polje | Vrednost |
|---|---|
| `line_dist` | **64** |
| `evenodd_dist` | **8** |
| Kanali (color) | `CL_RED, CL_GREEN, CL_BLUE` |
| Kanal (gray) | `CL_RED` |
| RGB redosled | `CL_RED, CL_GREEN, CL_BLUE` |

`line_dist` i `evenodd_dist` su izraženi u jedinicama **bazne rezolucije
senzora (2400 dpi)** i moraju se skalirati na radnu rezoluciju.

> Senzor je 2400 dpi, ali motor 1200 dpi. HP3970, koji ima motor 2400, je jedini
> u familiji sa punom vertikalnom podrškom na 2400. Ova asimetrija je verovatno
> direktno povezana sa problemom opisanim u §3.

---

## 2. Geometrija i referentna pozicija

### Ograničenja površine (mm)

| Izvor | left | width | top | height |
|---|---|---|---|---|
| Reflective (flatbed) | 0 | 220 | 0 | 300 |
| Negative | 89 | 45 | 0 | 85 |
| Transparent (TMA) | 89 | 45 | 0 | 100 |

### Auto reference position

| Polje | Vrednost |
|---|---|
| Tip | `REF_TAKEFROMSCANNER` |
| x / y offset (bazirano na 2400 dpi) | `88` / `624` |
| Rezolucija detekcije | `600` |
| Extern boundary | `40` |

---

## 3. Rezolucije — korigovana slika u odnosu na MASTER plan

MASTER plan je nosio rizik „**visoka verovatnoća da 2400 DPI ne postoji u
hp3900 code path-u**“. To je **netačno**, ali stvarnost nije ni bolja — samo je
drugačija.

### 3a. Šta hardverske tabele sadrže

`hp3800_scanmodes` **ima** kompletne unose za 1200 i 2400 dpi:

| Color mode | Native rezolucije u tabeli |
|---|---|
| `CM_COLOR` | 100, 150, 200, 300, 600, 1200, **2400** |
| `CM_GRAY` | 100, 150, 200, 300, 600, 1200, **2400** |
| `CM_LINEART` | 100, 150, 300, 600, 1200 — **bez 2400** |

Pokriveno za sve kombinacije `ST_NORMAL` / `ST_TA` / `ST_NEG` × `USB11` / `USB20`.

### 3b. Šta backend zapravo izlaže

`hp3900_sane.c:279-286`:

```c
case HPG2710:
case HP3800:
  {
    /* 1200 and 2400 dpi are disabled until problems are solved */
    SANE_Int myres[] = { 7, 50, 75, 100, 150, 200, 300, 600 };
```

**Autor je namerno onemogućio 1200 i 2400 dpi za G2710/HP3800 zbog nerešenih
problema.** Ovo je poreklo „≤600 dpi“ napomene u SANE dokumentaciji: nije
nedostajuća implementacija, nego poznato pokvarena funkcija.

### 3c. Korigovan rizik

| | MASTER v1.0 pretpostavka | Stvarnost |
|---|---|---|
| Postoji li config za 2400? | verovatno ne | **da, kompletan** |
| Zašto onda ≤600? | nije implementirano | **namerno isključeno, „until problems are solved“** |
| Šta je rizik? | pisanje koda ispočetka | **ulazimo u poznato pokvarenu oblast, na daljinu** |

Mitigacija ostaje ista i sada je još opravdanija: 1200/2400 su
`HARDWARE-VALIDATED = DEFERRED` do H8. Dodatno, H8 mora da prikupi dovoljno
dijagnostike da se utvrdi **priroda** problema (geometrija? motor na 1200 dpi
protiv senzora na 2400? DMA? tajming?), a ne samo PASS/FAIL.

### 3d. 75 DPI nije native mod — ispravka obima 1.0

MASTER plan navodi 75 DPI kao obaveznu hardversku rezoluciju. `hp3800_scanmodes`
**nema nijedan unos za 75 dpi** (ni za 50). Najniži native mod je **100 dpi**.

75 i 50 dpi u SANE frontend listi postižu se **softverskim smanjivanjem**
(`RSZ_DECREASE`) iz višeg native moda.

**Posledica za plan:** 75 DPI ostaje u 1.0 obimu, ali se reklasifikuje iz
hardverske rezolucije u **resize path**. Njegov acceptance gate pripada fazi
G2710-6 (image pipeline), ne H8 (rezolucijska kvalifikacija). Isto važi za 50 dpi
ako ga budemo izlagali.

### 3e. Predlog capability tabele za 1.0

| DPI | Poreklo | Status u 1.0 |
|---|---|---|
| 50, 75 | resize iz 100/150 | izložiti, gate u G2710-6 |
| 100, 150, 200, 300, 600 | native | izložiti, gate H6/H8 |
| 1200, 2400 | native, ali „disabled until problems are solved“ | **skriveno** dok H8 ne dokaže |

`CM_LINEART` maksimum je 1200 čak i ako 2400 prođe kvalifikaciju.

---

## 4. Dugmad

| Polje | Vrednost |
|---|---|
| Broj dugmadi | **3** |
| Maske | `0x01`, `0x02`, `0x04` |

Odgovara fizičkim Scan / Copy / PDF dugmadima. Mapiranje maska → funkcija se
potvrđuje u H10.

---

## 5. Preostalo za ekstrakciju (`tools/extract-hp3900-profile.py`)

Tabele koje treba mehanički prevesti u `G2710Profile.generated.h`:

- [ ] `hp3800_scanmodes` — pun `st_scanmode` red po (usb, scantype, colormode, res)
- [ ] `hp3800_timing_get` — CCD timing profili (`timing` indeks iz scanmodes)
- [ ] `hp3800_motor` + `hp3800_motormove` — motorne krive i profili kretanja
- [ ] `hp3800_gainoffset` — ADC gain/offset
- [ ] `hp3800_refvoltages` — vrts / vrms / vrbs
- [ ] `hp3800_vrefs` — SER / LER po rezoluciji
- [ ] `hp3800_offset` — left/width za offset kalibraciju
- [ ] `hp3800_effectivepixel` — efektivni pikseli po rezoluciji
- [ ] `hp3800_wrefs` + `hp3800_shading_cut` — white refs i shading cut
- [ ] `hp3800_checkstable` + `hp3800_fixedpwm` — warmup kriterijum i PWM lampe
- [ ] `hp3800_calibreflective` — flatbed kalibracioni parametri
- [ ] `srt_hp3800_scanparam_get` + `srt_hp3800_platform_get`

TMA grane (`hp3800_calibtransparent`, `hp3800_calibnegative`, `ST_TA`, `ST_NEG`)
ekstraktuju se u tabele ali se **ne aktiviraju** u 1.0 — odluka iz MASTER plana.

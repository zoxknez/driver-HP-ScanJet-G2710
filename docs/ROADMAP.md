# ROADMAP

Šta je ostalo, kojim redom, i po čemu se zna da je gotovo.

Ovaj dokument se **održava ručno** — za razliku od [STATUS.md](STATUS.md), koji
se generiše. STATUS govori šta je dokazano *sada*; ROADMAP govori šta tek treba
dokazati i kojim redom. Kad se sesija završi, ovde se štrikira kutija i
regeneriše STATUS.

---

## 1. Gde smo

| | |
|---|---|
| Faze završene | 0, 1, 2, 3, 5, 6, 7, 11 |
| Faze delimično | 4 i 9 — kod postoji, jedan gate po fazi nije izmeren (§3) |
| Faze nedirnute | 8, 10 |
| Blokirano hardverom | 12, 13 |
| Kod | ~21.500 linija (`native/core` 10.414, `native/wia` 2.305, wizard 4.747, CLI 1.708, sim 1.643) |
| Testovi | 552 (506 C++ ×2 arhitekture, 46 wizard) |

Po broju linija urađeno je oko **60%**. Po riziku više — najteže je iza nas:
RTS8822 protokol, kalibracija, line-offset i bezbednosni model su portovani i
izmereni. Ono što ostaje je uglavnom *površina*: ABI, dve aplikacije, TWAIN.

---

## 2. Pravila koja važe u svakoj sesiji

Ovo nije uvod nego lista koja se proverava pre commit-a. Svako od njih je
nastalo iz greške koja se već desila.

1. **Nijedna sesija se ne zatvara crvena.** `ctest` x64 i x86, `dotnet test`,
   `verify-source-hygiene.py`, `verify-reference-gates.py`.
2. **Novi kod se mutira.** Namerno se pokvari, i mora pasti *imenovani* test.
   Test koji ne padne ne testira ono što tvrdi da testira.
3. **`docs/STATUS.md` se regeneriše, nikad ne kuca rukom.** Nova test meta ide
   u `PHASES` ili `MANAGED_SUITES` u `tools/generate-status.py`, inače
   generator pada uz spisak siročeta — i to je namerno.
4. **WIA i TWAIN oglašavaju isključivo `HARDWARE_VALIDATED`.** Trenutno:
   ništa. Ne popravlja se dodavanjem vrednosti nego prolaskom H8.
5. **`third_party/hp3900-reference/` se ne kompajlira i ne `#include`-uje.**
   Sprovodi `verify-source-hygiene.py`.
6. **GUI se ne isporučuje kao skelet.** Pun raspored, stanja za prazno /
   učitavanje / grešku / nema uređaja, nijedno dugme bez ponašanja, ništa
   sabijeno uz levu ivicu.
7. **Referentni defekt se reprodukuje, ne krije.** Vidi
   [REFERENCE-DEFECTS.md](REFERENCE-DEFECTS.md).

---

## 3. Dve otvorene rupe u već „završenim" fazama

Obe su iste vrste: **kod postoji, tvrdnja nije izmerena.**

### 3.1 `tests/wiaharness/` je prazan

Testiran je sloj odluka — `WiaCapabilities`, `WiaEvents`, `WiaItemContext`,
CLSID (30 testova). Nije testiran ceo `IWiaMiniDrv` životni ciklus kroz pravi
COM objekat.

**Tehnička prepreka koju treba znati unapred:** `g2710_wia` linkuje
`G2710::Core` statički. Ako test uradi `LoadLibrary` + `DllGetClassObject`,
DLL nosi **svoju** kopiju `TransportProvider` singletona, pa
`SetForTesting` iz test procesa ne bi imao nikakvog efekta — a test bi i dalje
prolazio, samo bi merio nešto drugo.

**Rešenje:** harness kompajlira `native/wia/*.cpp` direktno u test binarni fajl,
kao što se `WiaCapabilities.cpp` već gradi kao deo testova. Tako je
`TransportProvider` jedan objekat.

### 3.2 `tests/arbiter/` je prazan

`unit/device_arbiter_test.cpp` postoji, ali radi u **jednom procesu** — a
`Global\` namespace i postoji zato što proces nije jedini nivo izolacije.

**Šta se offline može dokazati:** dva odvojena procesa se otimaju o isti
`Global\` objekat i tačno jedan dobija `DataSession`; drugi dobija poruku sa
imenom vlasnika, ne sirov Win32 kod.

**Šta se offline NE može dokazati:** pravi Session 0 ↔ interaktivna sesija.
Za to treba Windows servis. To ostaje H12 na hardveru, i ROADMAP to tako i
zove — ne pretvara se da je pokriveno.

---

## 4. Sesije

Svaka sesija je zaokružena: počinje zeleno, završava zeleno, i ostavlja
repozitorijum u stanju iz koga se sme stati.

### S1 · Zatvaranje dva gate-a  *(mali, visok prinos)*

Prvi je namerno najmanji: zatvara dva gate-a i ne ostavlja ništa nedovršeno ako
nas limit prekine.

**Nastaje:**
- `tests/wiaharness/wia_lifecycle_test.cpp` + `CMakeLists.txt`
- `tests/arbiter/arbiter_cross_process_test.cpp` + pomoćni exe
  `tests/arbiter/lock_holder.cpp`

**Pokriva:**
- `IStiUSD::Initialize` → `GetCapabilities` → `GetStatus` → `LockDevice` →
  `drvInitializeWia` → `drvInitItemProperties` → `drvValidateItemProperties` →
  `drvAcquireItemData` → `drvFreeDrvItemContext` → `drvUnInitializeWia`
- cancel usred transfera (`S_FALSE` semantika)
- greška usred transfera
- `TransportLost` usred transfera → nijedna dalja motion komanda
- dva procesa, isti `Global\` objekat, tačno jedan `DataSession`
- klijent koji čeka dobija ime vlasnika, ne `ERROR_SHARING_VIOLATION`

**Gotovo kada:** obe mete zelene na x64 i x86, `generate-status.py` ih svrstava
u G2710-9 i G2710-4, i mutacija (npr. uklonjena provera vlasništva) obara
imenovani test.

---

### S2 · C ABI  ·  `native/abi/`

Granica preko koje .NET priča sa jezgrom. Mora biti **stabilna** — menja se
namerno, nikad slučajno.

**Nastaje:**
- `native/abi/g2710_abi.h` — čist C, bez ijednog C++ tipa u potpisu
- `native/abi/G2710Abi.cpp` — implementacija nad `G2710Device` i `ScanSession`
- `native/abi/G2710.Native.def`
- `tests/unit/abi_test.cpp`

**Funkcije** (iz MASTER plana): `g2710_open`, `g2710_close`, `g2710_identify`,
`g2710_warmup`, `g2710_home`, `g2710_preview`, `g2710_scan_begin`,
`g2710_scan_read_line`, `g2710_cancel`, `g2710_get_status`,
`g2710_get_effective_safety_level`, `g2710_enable_trace`.

**Odluke koje treba doneti u ovoj sesiji, ne kasnije:**

| Pitanje | Predlog |
|---|---|
| Model greške | `int` kod + `g2710_last_error_message(handle)`; nijedan izuzetak ne sme preći granicu |
| Vlasništvo memorije | pozivalac daje bafer, ABI ga puni; ABI nikad ne alocira ono što .NET oslobađa |
| Callback-ovi | `progress` i `log`, sa `void* user`; dokumentovano da se zovu **sa radne niti** |
| Threading | jedan handle = jedan pozivalac; paralelna upotreba je greška pozivaoca i vraća kod, ne ruši se |
| Arbitraža | svaki `g2710_open` prolazi kroz `DeviceArbiter` |

**Dodatni test koji vredi više nego što izgleda:** *ABI stability test* —
spisak izvezenih simbola i veličine struktura se porede sa zapamćenim golden
fajlom. Slučajna promena ABI-ja tada pada kao test, a ne kao pad aplikacije
kod prijatelja.

**Gotovo kada:** `abi_test` vozi ceo tok (otvori → identify → warmup → home →
scan_begin → read_line ×N → close) nad `SimTransport`, cancel u bilo kom
trenutku ostavlja uređaj u `Idle`, i ABI stability test je zelen.

---

### S3 · `managed/G2710.Interop`

**Nastaje:**
- `managed/G2710.Interop/NativeMethods.cs` — `LibraryImport`, ne `DllImport`
- `managed/G2710.Interop/DeviceHandle.cs` — `SafeHandle`, ne `IntPtr`
- `managed/G2710.Interop/G2710Exception.cs`
- `managed/G2710.Interop.Tests/`

**Zamke koje se ovde plaćaju ako se preskoče:**
- delegat prosleđen kao callback mora biti **držan živ** dok ga native strana
  može pozvati; GC ga inače pokupi i to se ruši nasumično, obično kod korisnika
- callback stiže sa native niti → sve što dira UI mora kroz dispečer
  (isti obrazac kao `OnUiThread` u wizardu)
- `SafeHandle` mora zatvarati uređaj i kad se aplikacija ruši

**Gotovo kada:** testovi voze pravi `G2710.Native.dll` nad sim transportom iz
.NET-a, uključujući cancel i callback pod pritiskom GC-a
(`GC.Collect()` usred skeniranja mora biti bezopasan).

---

### S4–S6 · `managed/G2710.App`  *(tri sesije — najveći deo koji je ostao)*

Wizard je 4.747 linija i **jednostavniji** je od ovoga: nema preview sa
crop-om, ni live progress nad pravim skeniranjem, ni izvoz u šest formata.
Zato tri sesije, a ne jedna — aplikacija se ne sme ostaviti na pola.

#### S4 · Ljuska i podešavanja
- prozor, navigacija, tema (isti `Palette.xaml` / `Controls.xaml` obrazac kao
  wizard, izdvojen u deljeni resurs)
- izbor izvora / moda / rezolucije / dubine, sve vezano za `capabilities`
- **stanja:** nema uređaja · zauzet drugim klijentom · greška · učitavanje ·
  prazan rezultat
- dijagnostika i log viewer

#### S5 · Preview, crop, progres, cancel
- preview scan → slika u prozoru
- interaktivni crop (rubber-band) sa mapiranjem preview ↔ uređaj; ovo je
  mesto gde se najlakše pogreši za faktor rezolucije, pa ide sa testovima nad
  koordinatnom transformacijom
- live progress iz ABI callback-a
- cancel u bilo kom trenutku → `Idle`, bez zaostalog prolaza

#### S6 · Izvoz i završna obrada
- PNG · JPEG · TIFF 8-bit · TIFF 16-bit · PDF · multi-page PDF
- 16-bit TIFF je taj koji obično ispadne pogrešan (byte order); ide sa golden
  testom
- keyboard navigacija, tooltip-ovi, poruke greške koje kažu šta da se uradi

**Gotovo kada:** ceo tok otvori → warmup → home → kalibracija → preview →
crop → scan → izvoz radi nad simulatorom kroz `TestTransportProvider`, u svih
šest formata, i cancel u bilo kom trenutku ostavlja uređaj u `Idle`.

---

### S7–S8 · TWAIN  ·  `native/twain/`

#### S7 · Data Source, x64
- `DSM_Entry`, `DAT_IDENTITY`, `DAT_CAPABILITY`
  (`ICAP_XRESOLUTION`, `ICAP_PIXELTYPE`, `ICAP_BITDEPTH`, `ICAP_UNITS`,
  `ICAP_XFERMECH`), `DAT_IMAGELAYOUT`
- `DAT_IMAGENATIVEXFER` + `DAT_IMAGEMEMXFER`
- state machine 1–7, bez curenja stanja
- sopstveni UI + „hide UI" režim

#### S8 · x86, harness, arbitraža
- `G2710.Core` se već gradi za x86 — TWAIN se dodaje
- `tests/twainharness/` za **obe** arhitekture
- x86 i x64 DS istovremeno pokrenuti ne blokiraju jedan drugog trajno
- raspoređivanje: x64 → `C:\Windows\twain_64\`, x86 → `C:\Windows\twain_32\`

**Gotovo kada:** state machine testovi zeleni na obe arhitekture, smoke test
nad simulatorom prolazi, i ukršteni pristup (x86 DS + x64 DS) daje tačno
jednu `DataSession`.

---

### S9 · WiX installer  ·  zatvara G2710-11

Namerno **poslednji** — instalater koji raspoređuje nepostojeći TWAIN i
nepostojeću aplikaciju bio bi skelet.

- install / uninstall, `pnputil` za INF, instalacija sertifikata
- `SIGNING_MODE=Development|Release` — isti paket, drugi potpis
- TWAIN x86 + x64 na prava mesta
- **čist uninstall:** provera da posle deinstalacije ne ostaje nijedan fajl,
  ključ registra ni sertifikat — mereno, ne pretpostavljeno
- `tools/build-qualification-package.ps1` dobija brata za pun proizvod

---

### S10 · Prvi paket koji zaista ide prijatelju

- izbor plafona i redosled eskalacije (§5)
- probni prolaz cele isporuke na ovoj mašini
- kratko uputstvo šta da javi ako nešto ne prođe

---

## 5. Eskalacija plafona — kojim redom paketi idu

Plafon se **ugrađuje u binarni fajl pri pakovanju** i ne može se podići na
tuđem računaru. Zato redosled paketa *jeste* bezbednosni plan.

| Paket | Plafon | Pokriva | Šalje se tek kad |
|---|---|---|---|
| P1 | 1 | H1 instalacija, H2 chipset i read-only registri | — prvi je |
| P2 | 2 | H3 lampa i warmup | P1 čist |
| P3 | 3 | H4 HOME i kretanje, ponašanje pri unplug-u | P2 čist |
| P4 | 4 | H5 RAW CCD akvizicija | P3 čist |
| P5 | 5 | H6–H13 | P4 čist |

Ispod plafona 3 motorni kod **nije preveden**;
`tools/verify-safety-ceiling.ps1` to meri nad simbolima u biblioteci, a
`build-qualification-package.ps1` odbija da spakuje paket koji prijavi drugi
plafon nego što je traženo.

---

## 6. Hardverska staza — blokirana prijateljevim uređajem

Ne troši naše sesije; ide paralelno čim P1 ode.

| # | Test | Plafon |
|---|---|---|
| H1 | Instalacija (H1-A Secure Boot **uključen** → H1-B tek ako padne) | 1 |
| H2 | Chipset ID, read-only sweep, potvrda `IOCTL_SEND_USB_REQUEST` | 1 |
| H3 | Lampa ON/OFF, warmup profil | 2 |
| H4 | HOME, referentna pozicija, unplug tokom kretanja | 3 |
| H5 | RAW CCD akvizicija | 4 |
| H6 | 300 dpi RGB flatbed | 5 |
| H7 | Puna kalibracija | 5 |
| H8 | 75/150/600, zatim 1200/2400 i 48-bit | 5 |
| H9 | Sivo, lineart, preview, crop | 5 |
| H10 | Fizička dugmad Scan/Copy/PDF | 5 |
| H11 | WIA integracija; kvalifikacija `GetMyDeviceHandle` | 5 |
| H12 | TWAIN x64+x86; arbitraža Session 0 ↔ interaktivna | 5 |
| H13 | Stres: 100× open/close, home, scan; unplug; sleep/resume; hub | 5 |

**Napomena o H1-A:** na razvojnoj mašini je Secure Boot **isključen**, pa
uspešna instalacija ovde *nije* dokaz za H1-A. `install-state.json` beleži sve
tri postavke baš zato.

---

## 7. Šta bi promenilo redosled

| Nalaz | Posledica |
|---|---|
| H2 pokaže control transfer koji nije `0x40`/`0xC0` | transport odluka se otvara ponovo; `WinUsbTransport` iz laboratorije postaje kandidat, uz zaseban INF |
| H1-A padne uz Secure Boot | prelazi se na H1-B; Secure Boot se i dalje ne gasi bez pristanka |
| H8 obori 1200/2400 | ostaju `IMPLEMENTED` zauvek i **ne postoje** u WIA property listi (G2710-13) |
| Hardver se ne slaže sa referencom | USBPcap golden capture originalnog HP drajvera → `tools/pcapng-to-trace.py` → `ReplayTransport` → diferencijalna analiza |

---

## 8. Redosled u jednom redu

```
S1 gate-ovi → S2 ABI → S3 Interop → S4·S5·S6 aplikacija
   → S7·S8 TWAIN → S9 installer → S10 paket
                                      ↓
                        P1…P5  ·  H1…H13  ·  G2710-13 capability lock
```

Procena: **7–10 sesija** do trenutka kada je sve što se može uraditi bez
skenera — urađeno.

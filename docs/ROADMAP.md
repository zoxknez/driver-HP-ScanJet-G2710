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
| Faze završene | 0, 1, 2, 3, 4, 5, 6, 7, **8 (ABI deo)**, 9, 11 |
| Faze nedirnute | 8 (aplikacija), 10 |
| Blokirano hardverom | 12, 13 |
| Testovi | 668 — x64 585, x86 559, wizard 46, interop 37 |

Faze 4 i 9 su do S1 stajale kao „završene" sa jednim neizmerenim gate-om po
fazi. Sada su izmerene; ono što se offline ne može izmeriti imenovano je i
prebačeno na H11/H12 (§3), umesto da se prećuti.

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

## 3. Dve rupe koje su bile otvorene — zatvorene u S1

Obe su bile iste vrste: **kod postoji, tvrdnja nije izmerena.**

### 3.1 `tests/wiaharness/` — zatvoreno

Prvo je izmereno šta od `wiaservc` pomoćnih funkcija uopšte radi van WIA
servisa, jer je od toga zavisio ceo projekat harness-a:

| | |
|---|---|
| `wiasCreateDrvItem` | **radi** — stablo se pravi i bez servisa |
| `wiasReadPropLong` | `E_INVALIDARG` |
| `wiasWritePropLong` | `E_INVALIDARG` |

Skladište osobina pravi servis, ne drajver. Zato je prenos podeljen tačno na
toj granici — `native/wia/WiaTransfer.{h,cpp}`:

```
drvAcquireItemData   čita osobine (wiasReadPropLong)      → čeka H11
                     sklopi sink i pozove runTransfer

runTransfer          lampa, plan, sesija, redovi,
                     otkazivanje, napredak, zatvaranje    → mereno sada
```

`MINIDRV_TRANSFER_CONTEXT` je običan POD a `IWiaMiniDrvCallBack` običan COM
interfejs, pa je i `WiaCallbackSink` — koji barata baferima servisa — ostao
testabilan. Servis treba **samo** osobinama.

Harness kompajlira `native/wia/*.cpp` u sebe umesto da učitava DLL. Razlog je
zapisan u samom fajlu: `g2710_wia` linkuje jezgro statički, pa bi `LoadLibrary`
dao DLL-u *svoju* kopiju `TransportProvider` singletona — `ScopedTestProvider`
iz test procesa ne bi imao efekta, a test bi i dalje **prolazio**, samo bi merio
nešto drugo.

Cilja se `G2710_WIA_ALLOW_UNQUALIFIED=1`; bez toga ponuda je prazna,
`drvInitializeWia` odbija na vratima, i nema životnog ciklusa. Grana
„nema šta da se ponudi → odbij" pokrivena je u izdanju, u `wia_capabilities`.

**26 testova.** Samo x64 — `wiaservc` ne postoji kao 32-bitni i x86 binarni
fajl se ruši sa `STATUS_DLL_NOT_FOUND` pre `main()`.

### 3.2 `tests/arbiter/` — zatvoreno koliko se offline može

`unit/device_arbiter_test.cpp` radi u jednom procesu, a takav test prolazi i
kada je brava običan `std::mutex`. Novi test pokreće **drugi proces**
(`tests/arbiter/lock_holder.cpp`) i njih dva se otimaju o isti `Global\`
objekat.

Dokaz da test zaista meri: kada se muteks napravi bezimenim — brava postaje
proces-lokalna — **svih 9 starih testova i dalje prolazi**, a tri nova padaju.

**Šta i dalje NIJE dokazano:** pravi Session 0 ↔ interaktivna sesija. Za to
treba Windows servis koji radi kao LocalSystem, a to se ne podiže iz test
binarnog fajla. Ostaje **H12**.

**6 testova**, obe arhitekture.

## 4. Sesije

Svaka sesija je zaokružena: počinje zeleno, završava zeleno, i ostavlja
repozitorijum u stanju iz koga se sme stati.

### ~~S1 · Zatvaranje dva gate-a~~ — **URAĐENO**

Zatvoreni gate-ovi faza G2710-4 i G2710-9; kako i dokle — §3.

**Nastalo:** `native/wia/WiaTransfer.{h,cpp}` (seam),
`tests/wiaharness/wia_lifecycle_test.cpp` (26),
`tests/arbiter/arbiter_cross_process_test.cpp` + `lock_holder.cpp` (6).

**Pokriveno:** produkciona putanja otvaranja kroz `IStiUSD::Initialize` sa
lažnim `IStiDeviceControl`; `QueryInterface` između `IStiUSD` i `IWiaMiniDrv`;
`GetCapabilities` / `GetStatus` / `LockDevice`; ceo prenos nad simulatorom —
otkazivanje aplikacije, otkazivanje kroz token, tvrda greška, dva prenosa
zaredom, kadenca napretka; oba režima bafera; preslikavanje grešaka; i
međuprocesna arbitraža.

**Nije pokriveno, i zna se zašto:** `drvInitItemProperties`,
`drvValidateItemProperties` i čitanje osobina u `drvAcquireItemData` — traže
WIA servis (§3.1). Pravi Session 0 traži Windows servis (§3.2). Oboje H11/H12.

**Nađeno usput:** napredak se javljao `kProgressSteps + 1` puta jer je brojač
kretao od `-1` — konstanta je lagala o sopstvenoj kadenci. Popravljeno u kodu,
ne u očekivanju testa.

---

### ~~S2 · C ABI~~ — **URAĐENO**  ·  `native/abi/`

Granica preko koje .NET priča sa jezgrom. **47 testova.**

**Odluke, donete pre koda i zapisane u vrhu `g2710_abi.h`:**

| Pitanje | Odluka |
|---|---|
| Model greške | povratna vrednost + `g2710_last_error(handle)`; svaka ulazna tačka je u `guard`-u koji hvata **sve** izuzetke — .NET runtime C++ izuzetak ne može uhvatiti, proces se ruši bez traga |
| Vlasništvo memorije | bafer daje pozivalac; `capacity 0` vraća potrebnu veličinu; premali bafer je **greška**, ne tiho skraćivanje |
| Callback-ovi | `progress` i `log` stižu **sa radne niti**; callback koji baci izuzetak tumači se kao zahtev za prekid |
| Threading | jedan handle = jedan pozivalac; `g2710_cancel` je **jedina** funkcija koja sme iz druge niti |
| Verzionisanje | `size` kao prvo polje svake strukture, plus `g2710_abi_version()` |

**Test stabilnosti** poredi `.def` sa zaglavljem (oba čita kao **tekst**), pamti
offsete i veličine, i zaključava brojeve u enum-ima.

**Dva nalaza koja je S2 iznedrila — oba ozbiljna:**

**1. Otkazivanje je onemogućavalo zaustavljanje skeniranja.** `cancel()` je bio
lepljiv u **oba** transporta — i u simulatoru i u produkcionom
`UsbScanTransport`-u. Posle otkazivanja nijedan transfer nije prolazio,
uključujući `warmReset()` koji zaustavlja čip. Na pravom skeneru to znači: glava
nastavlja da se kreće posle „Prekini".

Rešeno novim korakom u ugovoru — `ITransport::clearCancel()`, i
`G2710Device::endCancellation()` iznad njega. Zove ga sloj koji **zna** da je
otkazivanje gotovo; transport to ne može znati sam.

**2. Trag nije beležio `identity()`** — jedini uređajni poziv koji je izostajao.
A „iza deljenog imena porta je tuđi uređaj" je otkaz koji se **već desio** na
razvojnoj mašini (HP LaserJet MFP). Dodat `TraceEntry::Kind::Identity`.

**Nađeno usput:** pokvaren string u `native/cli/main.cpp` — poruka o odbijenom
paljenju lampe završavala se doslovnim `' + N + '` umesto novim redom. Baš ta
poruka je ono što prijatelj vidi u paketu sa plafonom 1.

**Rupa u sopstvenom testu, zatvorena:** mutacija koja ubacuje polje u **sredinu**
`g2710_open_options` nije pala — novo polje je selo u postojeći padding, pa se
nijedan offset nije promenio. `offsetof` to načelno ne može uhvatiti. Dodata
provera koja čita **spisak polja iz teksta zaglavlja** i poredi ga sa zapamćenim.

**Ono što ABI namerno ne obećava:** `g2710_home` vraća
`G2710_STATUS_NOT_IMPLEMENTED` sa tačnim razlogom („čeka port `Head_Relocate`"),
a ispod nivoa 3 vraća `SAFETY_VIOLATION` — redosled provera je bitan, jer paketu
sa plafonom 1 tačan odgovor nije „nije implementirano" nego „ovaj paket to ne
sme".

---

### ~~S3 · `managed/G2710.Interop`~~ — **URAĐENO**

Most između .NET-a i jezgra. **37 testova**, svi kroz **pravu** `G2710.Native.dll`
nad simulatorom — bez lažnjaka, jer se ono što se ovde meri (raspored struktura,
životni vek callback-ova, `SafeHandle`) ne može izmeriti ni nad čim drugim.

**Delegata nema nigde.** Native strana dobija `delegate* unmanaged[Cdecl]` na
statičku metodu, a stanje putuje kroz `user` kao `GCHandle`. Time cela klasa
grešaka „GC je pokupio delegat koji native strana još drži" ne postoji, umesto
da se izbegava pažnjom.

`SafeHandle`, ne `IntPtr`: uređaj koji ostane otvoren drži ekskluzivnu bravu u
`Global\` namespace-u. Ako aplikacija padne pre `g2710_close`, sledeći klijent
zatiče skener koji „koristi neko drugi", bez ijednog vidljivog procesa. Critical
finalizer je jedina odbrana koju .NET nudi.

**Tri nalaza, sva tri iz testova:**

**1. `ScanReadLine` je vraćao `bool`, a ishoda ima tri.** Otkazivanje se gutalo
kao „nije greška", `done` je ostajao nula — pa je **otkazan prolaz izgledao
identično kao isporučen red**. Aplikacija bi upisala nepotpunu sliku i nikome ne
bi rekla da je nepotpuna. Sada `ScanLineResult { Delivered, Complete, Cancelled }`.

**2. `GCHandle` je bio ukras.** `Pin` je držao `Context` i kao **polje**, pa je
objekat bio dostupan preko upravljanog grafa bez obzira na handle. Mutacija koja
handle menja u **slab** nije oborila nijedan test. Polje uklonjeno; kontekst se
sada čita **kroz** handle, kao što je i pisalo da radi.

**3. Test koji je to trebalo da uhvati nije merio ono što tvrdi.** Pokretao je
`GC.Collect()` između redova, ali je zatim **ponovo registrovao** dnevnik pre
provere — merio je svež callback umesto onog koji je preživeo sakupljanje.
Prepravljen da grešku izazove **bez** ponovnog prijavljivanja; tek tada slabi
handle pada.

**Nađeno u alatu, ne u kodu:** `generate-status.py` je davao
`--logger trx;LogFileName=<fajl>`. Sa jednim test projektom radi; sa dva **drugi
prebriše prvi**, pa je STATUS tiho izgubio 46 provera i prijavio manji ukupan
broj. Broj je i dalje izgledao verodostojno — to je i bio problem. Sada ide
`--results-directory` i čitaju se svi izveštaji.

**Mutacije:**

| Mutacija | Pada |
|---|---|
| Otkazan red neodvojiv od isporučenog | 2 testa |
| Izuzetak iz callback-a se guta | 1 test |
| `SafeHandle` ne zatvara uređaj | 4 testa |
| Slab `GCHandle` | ~~0~~ → 1 nakon ispravke iz nalaza 2 i 3 |

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
S1 gate-ovi ✓ → S2 ABI ✓ → S3 Interop ✓ → S4·S5·S6 aplikacija
   → S7·S8 TWAIN → S9 installer → S10 paket
                                      ↓
                        P1…P5  ·  H1…H13  ·  G2710-13 capability lock
```

Preostalo: **4–7 sesija** do trenutka kada je sve što se može uraditi bez
skenera — urađeno.

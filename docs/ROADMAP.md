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
| Faze završene | 0, 1, 2, 3, 4, 5, 6, 7, **8 (ABI i Interop)**, 9, 11 |
| Ostalo | 8 (aplikacija), 10 (TWAIN), installer |
| Blokirano hardverom | 12, 13 |
| Testovi | **668** — x64 585, x86 559, wizard 46, interop 37 |

Kod, bez testova: **22.846 linija.**

| | |
|---|---|
| `native/core` | 10.501 |
| `native/wia` | 2.530 |
| `managed/G2710.Qualification` | 2.733 |
| `native/cli` | 1.708 |
| `native/sim` | 1.648 |
| `native/abi` | 1.251 |
| `managed/G2710.Interop` | 1.124 |

Testova je **11.015 linija** — skoro pola koliko i koda. To nije pedanterija
nego posledica toga što skener nije kod nas: ono što se ne izmeri ovde, meri se
tek na tuđem računaru, gde se ne može ni videti ni popraviti.

Faze 4 i 9 su do S1 stajale kao „završene" sa jednim neizmerenim gate-om po
fazi. Sada su izmerene; ono što se offline ne može izmeriti imenovano je i
prebačeno na H11/H12 (§3), umesto da se prećuti.

Najteže je iza nas: RTS8822 protokol, kalibracija, line-offset, bezbednosni
model i obe granice ka .NET-u. Ono što ostaje je uglavnom *površina* —
aplikacija, TWAIN, instalater.

Sve što kod **namerno ne radi**, sa razlogom i uslovom pod kojim bi proradilo,
stoji na jednom mestu — §4.

### Stanje offline isporuke (S4–S10)

Ovo je namerno odvojeno od hardverske kvalifikacije: sledeće je izgrađeno i
mereno bez priključenog skenera.

- `G2710.App` prolazi ceo simulator tok: otvori, warmup, preview, crop, finalni
  scan i PNG izvoz; layout se pakuje sa `G2710.Native.dll`. Build zatim podiže
  objavljeni EXE iz sopstvenog foldera, pa XAML/startup greška ne može ostati
  sakrivena iza uspešnog `dotnet publish`.
- TWAIN DS ima state machine, DSM alokatore, `DAT_IMAGEINFO`, layout, native
  24-bit DIB i memory transfer. Harness radi nad istim Core tokom, na x64 i
  x86. Zvanični, potpisani TWAIN DSM 2.5.1 je zatim stvarno učitao `.ds`,
  pronašao identitet i otvorio/zatvorio DS na obe arhitekture. Poseban release
  test dokazuje da test-simulator ne može otključati H8 rezolucije u
  proizvodnom DS-u.
- WiX MSI raspoređuje oba TWAIN DS fajla (`.ds` radi DSM discovery-ja), aplikaciju i WIA paket; deferred akcije
  pozivaju provereni `pnputil`/sertifikat skript i pri instalaciji i pri
  deinstalaciji. Struktura, katalog i potpis su provereni bez instaliranja.

**Dokazano na ovoj mašini:** elevated install → provera aplikacije, WIA/TWAIN
fajlova i razvojnog sertifikata → uninstall ostavlja nula tih fajlova,
sertifikata i G2710 DriverStore unosa.

**Još nije dokazano na ovoj mašini:** fizički uređaj i time H1–H13. To se ne
označava kao završeno samo zato što je paket izgrađen ili DSM uspešno učita DS.

Kada je administratorski token dostupan, `tools/verify-msi-install.ps1` radi
pun install → proveru fajlova/sertifikata → uninstall → proveru čišćenja. Ne
pokreće se iz redovnog build-a, jer namerno menja sistemsko stanje.

---

## 2. Pravila koja važe u svakoj sesiji

Ovo nije uvod nego lista koja se proverava pre commit-a. Svako od njih je
nastalo iz greške koja se **već desila**.

1. **Nijedna sesija se ne zatvara crvena.** `ctest` x64 i x86, `dotnet test`,
   `verify-source-hygiene.py`, `verify-reference-gates.py`.

2. **Novi kod se mutira.** Namerno se pokvari, i mora pasti *imenovani* test.

3. **Mutacija koja ne obori nijedan test je rupa u TESTU, ne dozvola da se
   nastavi.** Ovo se desilo tri puta i sva tri puta je test bio taj koji je
   ćutao:

   | Mutacija | Zašto nije pala | Ishod |
   |---|---|---|
   | polje u sredinu `g2710_open_options` | selo u postojeći padding, nijedan offset se ne menja | dodata provera spiska polja iz teksta zaglavlja |
   | slab `GCHandle` | `Pin` je držao objekat i kao polje | polje uklonjeno — handle je sad jedino što drži |
   | isto, drugi put | test je **ponovo registrovao** dnevnik pre provere | provera bez ponovnog prijavljivanja |

4. **Logika koja se može pogrešiti ide iza seam-a.** Ne zato što je lepše, nego
   zato što infrastruktura često **ne radi** offline: WIA skladište osobina
   traži servis, `wiasCreateDrvItem` ne. Granica se povlači tamo gde prestaje
   ono što se može izmeriti — `WiaCapabilities`, `WiaTransfer`, `NativeMethods`
   naspram `Scanner`.

5. **Ishod koji ima tri vrednosti ne dobija `bool`.** `ScanReadLine` je vraćao
   „ima još" — pa je otkazan prolaz izgledao identično kao isporučen red.

6. **`docs/STATUS.md` se regeneriše, nikad ne kuca rukom.** Nova test meta ide
   u `PHASES` ili `MANAGED_SUITES` u `tools/generate-status.py`, inače
   generator pada uz spisak siročeta — i to je namerno.

7. **WIA i TWAIN oglašavaju isključivo `HARDWARE_VALIDATED`.** Trenutno:
   ništa. Ne popravlja se dodavanjem vrednosti nego prolaskom H8.

8. **`third_party/hp3900-reference/` se ne kompajlira i ne `#include`-uje.**
   Sprovodi `verify-source-hygiene.py`.

9. **GUI se ne isporučuje kao skelet.** Pun raspored, stanja za prazno /
   učitavanje / grešku / nema uređaja, nijedno dugme bez ponašanja, ništa
   sabijeno uz levu ivicu.

10. **Referentni defekt se reprodukuje, ne krije.** Vidi
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

---

## 4. Šta kod namerno NE obećava

Jedno mesto sa svime što ne radi, i zašto. Postoji da se ne bi zaboravilo i da
se ne bi slučajno „popravilo" tako što se doda vrednost koju niko nije izmerio.

| Ne radi | Šta se umesto toga dešava | Otključava |
|---|---|---|
| **Povratak glave na home** | `NOT_IMPLEMENTED` sa razlogom „čeka port `Head_Relocate`" | port `Head_Relocate` / `Head_ParkHome`, pa **H4** |
| **Orkestracija kalibracije** | prolaz radi, ali `shading_applied = 0`; slika nosi neujednačenost senzora, i to se prijavljuje | **H7** |
| **Bilo koja rezolucija u WIA/TWAIN** | ponuda je prazna; `drvInitializeWia` odbija na vratima | **H8** |
| **1200 / 2400 dpi kao proizvod** | kod postoji i skenira; status ostaje `IMPLEMENTED` zbog defekta D3 | **H8** |
| **TMA / slajdovi** | `ScanSource::Tma*` postoji u planeru, vraća `NotImplementedIn10` | verzija 1.1 |
| **Session 0 ↔ interaktivna arbitraža** | dokazano samo međuprocesno; pravi Session 0 traži Windows servis | **H12** |
| **`GetMyDeviceHandle`** | neiskorišćen kandidat; produkciona putanja je `GetMyDevicePortName` | **H11** |
| **WIA osobine offline** | `wiasReadPropLong` van servisa vraća `E_INVALIDARG`; testira se sve **ispod** te granice | **H11** |

Svaka od ovih stavki ima tačan razlog i tačan uslov. Nijedna nije „nismo
stigli" — svaka je „nemamo čime to da izmerimo, a pretpostavljati na tuđem
uređaju je upravo ono što ovaj projekat ne radi".

---

## 5. Sesije

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

Wizard je 2.733 linije i **jednostavniji** je od ovoga: nema preview sa crop-om,
ni live progress nad pravim skeniranjem, ni izvoz u šest formata. Zato tri
sesije, a ne jedna — aplikacija se ne sme ostaviti na pola.

Priča sa jezgrom isključivo kroz `G2710.Interop.Scanner`. Ništa u aplikaciji ne
sme dodirnuti `NativeMethods` — sve što se može pogrešiti već je jednom
izmereno, i ne meri se dvaput.

**Tri stvari koje S3 nameće aplikaciji, a lako se prećute:**

- **`ScanLineResult.Cancelled` nije `Complete`.** Slika je nepotpuna i UI to
  mora reći. Tiho snimanje polovične slike je gore od greške.
- **Callback-ovi stižu sa radne niti.** Isti obrazac kao `OnUiThread` u wizardu
  — bez njega se prozor ruši čim stigne prvi red dnevnika.
- **`ShadingApplied = false`.** Aplikacija mora reći da slika nosi
  neujednačenost senzora, umesto da je pokaže kao gotov proizvod.

#### S4 · Ljuska i podešavanja
- tema se **izdvaja** iz wizarda u deljeni resurs (`Palette.xaml`,
  `Controls.xaml`) — dva prozora sa dve kopije iste palete raziđu se u prvom
  mesecu
- `Scanner.CheckAbiVersion()` pri pokretanju: nepoklapanje je jasna poruka,
  a ne rušenje pri prvom pozivu
- izbor izvora / moda / rezolucije / dubine, vezano za `capabilities`
- **stanja:** nema uređaja · zauzet drugim klijentom (`CurrentOwner` kaže
  kojim) · greška · učitavanje · prazan rezultat
- dijagnostika: plafon build-a, da li je motorni put uopšte preveden, i
  dugme koje piše trag (`WriteTrace`)

#### S5 · Preview, crop, progres, cancel
- **preview je obično skeniranje na niskoj rezoluciji** — zasebnog preview
  poziva u ABI-ju nema i neće ga ni biti; ovo je zapisano da se ne bi tražio
- interaktivni crop (rubber-band) sa mapiranjem preview ↔ uređaj; mesto gde se
  najlakše pogreši za faktor rezolucije, pa ide sa testovima nad **čistom**
  koordinatnom transformacijom, bez UI-ja
- live progress iz `Scanner` callback-a, kroz dispečer
- cancel u bilo kom trenutku → `Idle`, bez zaostalog prolaza; `ScanEnd` u
  `finally`, uvek

#### S6 · Izvoz i završna obrada
- PNG · JPEG · TIFF 8-bit · TIFF 16-bit · PDF · multi-page PDF
- 16-bit TIFF je taj koji obično ispadne pogrešan (byte order); ide sa golden
  testom, kao i PNM u CLI-ju
- keyboard navigacija, tooltip-ovi, poruke greške koje kažu **šta da se uradi**
- pakovanje: `G2710.Native.dll` mora ići pored aplikacije, i to se proverava
  probnim pokretanjem iz raspakovanog foldera

**Gotovo kada:** tok otvori → warmup → preview → crop → scan → izvoz radi nad
simulatorom u svih šest formata; cancel u bilo kom trenutku ostavlja uređaj u
`Idle`; a ono što ne radi (home, kalibracija) aplikacija **kaže**, ne krije.

---

### S7–S8 · TWAIN  ·  `native/twain/`

TWAIN ide direktno na `G2710::Core`, ne kroz C ABI — u istom je procesu i u
istom jeziku, pa bi ABI bio suvišan sloj. WIA to već radi tako.

#### S7 · Data Source, x64
- `DSM_Entry`, `DAT_IDENTITY`, `DAT_CAPABILITY`
  (`ICAP_XRESOLUTION`, `ICAP_PIXELTYPE`, `ICAP_BITDEPTH`, `ICAP_UNITS`,
  `ICAP_XFERMECH`), `DAT_IMAGELAYOUT`
- `DAT_IMAGENATIVEXFER` + `DAT_IMAGEMEMXFER`
- state machine 1–7, bez curenja stanja
- sopstveni UI + „hide UI" režim
- prenos se piše po uzoru na `WiaTransfer`: sink iza seam-a, pa je logika
  merljiva bez TWAIN DSM-a

#### S8 · x86, harness, arbitraža
- `G2710.Core` i `native/abi` se već grade za x86 — TWAIN se dodaje
- `tests/twainharness/` za **obe** arhitekture
- x86 i x64 DS istovremeno pokrenuti ne blokiraju jedan drugog trajno; ovo je
  isti `Global\` objekat koji S1 već meri međuprocesno
- raspoređivanje: x64 → `C:\Windows\twain_64\`, x86 → `C:\Windows\twain_32\`

**Gotovo kada:** state machine testovi zeleni na obe arhitekture, smoke test
nad simulatorom prolazi, i ukršteni pristup daje tačno jednu `DataSession`.

---

### S9 · WiX installer  ·  zatvara G2710-11

Namerno **poslednji** — instalater koji raspoređuje nepostojeći TWAIN i
nepostojeću aplikaciju bio bi skelet.

- install / uninstall, `pnputil` za INF, instalacija sertifikata
- `SIGNING_MODE=Development|Release` — isti paket, drugi potpis
- TWAIN x86 + x64 na prava mesta; `G2710.Native.dll` pored aplikacije
- **čist uninstall:** provera da posle deinstalacije ne ostaje nijedan fajl,
  ključ registra ni sertifikat — mereno, ne pretpostavljeno. `install.ps1` to
  već radi za kvalifikacioni paket i ta provera se preuzima.
- `tools/build-qualification-package.ps1` dobija brata za pun proizvod

---

### S10 · Prvi paket koji zaista ide prijatelju

- izbor plafona i redosled eskalacije (§6)
- ~~probni prolaz cele isporuke na ovoj mašini~~ — **prošao**: pun MSI
  (`build-installer.ps1` → InfVerif → Inf2Cat → signtool → `verify-installer.ps1`),
  proizvodni ZIP sa oba uputstva, i kvalifikacioni ZIP na oba jezika
- kratko uputstvo šta da javi ako nešto ne prođe
- **nemereno, i to se zna:** živ install/uninstall ciklus pravog MSI-ja sa
  brisanjem `HKLM\SOFTWARE\G2710`. Pravilo je da se čist uninstall *meri*;
  zasad je provereno samo na MSI tabelama (`Registry` red sa imenom `-`).
  Traži elevaciju, pa čeka odluku — ili se meri ovde, ili prvi put kod
  prijatelja uz `install.ps1 -Uninstall`.

**Zaključano usput:** pakovanje sada odbija da napravi ZIP ako wizard ne nosi
oba prevoda. Otkaz je bio potpuno tih — paket bi se izgradio, radio, i govorio
engleski onome kome je poslat na srpskom. Dokazano mutacijom: uklanjanje
`Strings.sr.resx` obara pakovanje umesto da prođe.

---

### ~~S11 · Dva jezika, engleski primaran~~ — **URAĐENO**  ·  `managed/G2710.Localization/`

Do sada je sve govorilo samo srpski. Sada program govori engleski ili srpski, a
korisnik bira **pri instalaciji**.

**Zašto je engleski neutralni resurs, a ne satelit:** engleski je ugrađen u samu
biblioteku, srpski se učitava pored nje. Kada satelit nedostaje — a nedostaje kad
god paket nije potpun — program i dalje govori. Obrnut raspored bi dao prazan
prozor.

**Redosled odluke** (`Language.Decide`, mereno u testovima u oba smera):
izričit izbor → ono što je instalater upisao u `HKLM\SOFTWARE\G2710` →
`language.txt` pored programa (prenosivi ZIP nema registar) → jezik Windows-a
ako je srpski → engleski.

**Zapis i poruka nisu ista stvar.** `test-results.json`, `install-state.json` i
`system-info.json` su **uvek na engleskom** — čita ih onaj kome se izveštaj
šalje, a ne onaj ko ga pravi. Čarobnjak prevodi ono što stoji na ekranu, i to po
**ID-u provere**; provera koju prevod još ne poznaje prikazuje se onako kako ju
je alat nazvao, jer je engleski natpis bolji od praznog polja.

**Nastalo:** `managed/G2710.Localization/` (`Language.cs`, `LocExtension.cs`, dva
`.resx`), `managed/G2710.Localization.Tests/` (15), `installer/LanguageDlg.wxs`,
`tools/license-to-rtf.ps1`, `tools/make-installer-art.ps1`.

**Nađeno usput — četiri stvarna kvara, ne kozmetika:**

1. **Izbor „English" pri instalaciji bio je isto što i ćutanje.** `IsSupported`
   je odbacivao `"en"` jer engleski nije satelit — a instalater tu vrednost
   upisuje uvek. Korisnik koji je izabrao engleski dobijao bi srpski čim mu je
   Windows srpski. Test koji je to otkrio napisan je pre popravke i pao je.
2. **Dijalog za izbor jezika se nije pojavljivao.** Prevodio se bez ijedne
   greške i stajao je u dekompilovanom WXS-u, ali dugme „Install" na licencnom
   dijalogu već nosi `EndDialog` na redu 2, a dodati `NewDialog` dobija veći
   red. Mereno na sastavljenom MSI-ju; sada dijalog stoji u `InstallUISequence`
   **pre** licence, a `verify-installer.ps1` proverava i redne brojeve.
3. **Red sa dugmadima u aplikaciji se sekao.** Devet kontrola u jednom
   `StackPanel`-u ne staje u 900 px — „Sačuvaj trag" i „Pregled" bili su
   stisnuti na nekoliko piksela. Dužina natpisa zavisi od jezika, pa raspored
   koji staje na jednom ne staje na drugom. Sada: alati levo (prelamaju se),
   radnje desno (prikovane).
4. **Polja isečka nisu imala natpise** — četiri neoznačena okvira sa razlikom
   samo u tooltip-u. Natpisi su u resursima postojali od početka.

**Provereno na ekranu, ne samo u testu:** obe aplikacije na oba jezika i MSI
dijalog uslikani i pregledani; čarobnjak provezen kroz simulator do izveštaja u
oba jezika (23 provere).

---

## 6. Eskalacija plafona — kojim redom paketi idu

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

## 7. Hardverska staza — blokirana prijateljevim uređajem

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

## 8. Šta bi promenilo redosled

| Nalaz | Posledica |
|---|---|
| H2 pokaže control transfer koji nije `0x40`/`0xC0` | transport odluka se otvara ponovo; `WinUsbTransport` iz laboratorije postaje kandidat, uz zaseban INF |
| Port `Head_Relocate` bude ekstraktovan | `g2710_home` prestaje da bude `NOT_IMPLEMENTED`; H4 postaje izvodljiv, a aplikacija dobija dugme koje danas nema |
| H1-A padne uz Secure Boot | prelazi se na H1-B; Secure Boot se i dalje ne gasi bez pristanka |
| H8 obori 1200/2400 | ostaju `IMPLEMENTED` zauvek i **ne postoje** u WIA property listi (G2710-13) |
| Hardver se ne slaže sa referencom | USBPcap golden capture originalnog HP drajvera → `tools/pcapng-to-trace.py` → `ReplayTransport` → diferencijalna analiza |

---

## 9. Redosled u jednom redu

```
S1 gate-ovi ✓ → S2 ABI ✓ → S3 Interop ✓ → S4·S5·S6 aplikacija
   → S7·S8 TWAIN → S9 installer → S10 paket
                                      ↓
                        P1…P5  ·  H1…H13  ·  G2710-13 capability lock
```

Preostalo: **4–7 sesija** do trenutka kada je sve što se može uraditi bez
skenera — urađeno.

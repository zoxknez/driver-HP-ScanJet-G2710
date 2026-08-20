# Nesaglasnosti u referenci

Mesta na kojima `hp3900` kod ne radi ono što izgleda da radi — **specifično za
RTS8822BL-03A**, dakle za naš čip.

Ovaj dokument postoji jer je port koji verno prepiše grešku gori od porta koji
je zapiše. Nijedna od ovih stavki se ne „popravlja" nagađanjem: iz izvora se ne
može znati koja je strana ispravna, to odlučuje hardver u fazi H3.

---

## D1 — `Lamp_Warmup` gasi TMA lampu umesto da je pali

**Zahvaćeno:** TMA (1.1). Flatbed nije.

`rts8822.c:7994` u `Lamp_Warmup`:

```c
if (tma_lamp == 0)
  {
    /* tma lamp is turned off */
    Lamp_Status_Set (dev, Regs, FALSE, TMA_LAMP);   /* <- FALSE */
    waitforpwm = TRUE;
  }
```

Komentar kaže da je lampa ugašena i da je treba upaliti, ali se prosleđuje
`FALSE`.

To radi na **ostalim** čipovima, zbog trika u default grani `Lamp_Status_Set`
(`rts8822.c:10778`):

```c
data_bitset (&Regs[0x146], 0x40, ((lamp - 1) | turn_on));
```

`TMA_LAMP` je `2`, pa je `lamp - 1 == 1`, a `1 | turn_on == 1` bez obzira na
`turn_on` — TMA se **uvek** pali. Debug string na liniji 10746 to i ispisuje:
`(((lamp - 1) | turn_on) & 1) == 1`.

Grana za BL-03A (`rts8822.c:10768`), dodata kasnije, nema taj trik:

```c
data_bitset (&Regs[0x146], 0x20, ((lamp == TMA_LAMP) && (turn_on == TRUE)) ? 1 : 0);
```

Sa `turn_on == FALSE` bit se **briše**. Na G2710 bi `Lamp_Warmup(TMA_LAMP)`
dakle ugasio TMA lampu.

---

## D2 — `Lamp_Status_Get` i `Lamp_Status_Set` čitaju i pišu **različite bajtove**

**Zahvaćeno:** TMA (1.1). Flatbed nije.

`Lamp_Status_Set` upisuje bit izbora lampe u `Regs[0x155]` (`rts8822.c:10771`):

```c
data_bitset (&Regs[0x155], 0x10, (lamp != FLB_LAMP) ? 1 : 0);
```

`Lamp_Status_Get` čita **reč** sa `0xE954`, pa je
`data1 = Regs[0x154] | (Regs[0x155] << 8)`.

| Grana | Test | Koji bajt |
|---|---|---|
| default | `_B1(data1) & 0x10` | `Regs[0x155]` — **poklapa se sa Set** |
| **BL-03A** | `data1 & 0x10` | `Regs[0x154]` — **ne poklapa se** |

Na BL-03A `Get` testira bit 4, a `Set` upisuje bit 12 iste reči. TMA status se
zato nikad ne bi pročitao kao uključen, čak i da D1 ne postoji.

---

## D3 — hardversko poravnanje redova se preliva **baš na 1200 i 2400 dpi**

**Zahvaćeno:** 1200 i 2400 dpi, sve boje. Do 600 dpi nema problema.

Ovo je najozbiljnija stavka i verovatno **konkretan mehanizam** iza komentara
`/* 1200 and 2400 dpi are disabled until problems are solved */`.

RTS8822 ume da poravna razmaknute R/G/B redove u hardveru. `RTS_Setup_Line_Distances`
(`rts8822.c:8701`) upisuje pet vrednosti:

```c
data_bitset (&Regs[0x149], 0x3f, myevenodddist);
data_bitset (&Regs[0x14a], 0x3f, mylinedistance);
data_bitset (&Regs[0x14b], 0x3f, mylinedistance + myevenodddist);
data_bitset (&Regs[0x14c], 0x3f, mylinedistance * 2);
data_bitset (&Regs[0x14d], 0x3f, (mylinedistance * 2) + myevenodddist);
```

Maska je `0x3F` — **šest bita, najviše 63**.

Za G2710 je `line_distance = 64` na senzorskih 2400 dpi, pa je
`mylinedistance = 64 * res / 2400`:

| Rezolucija | `0x14A` | `0x14B` | `0x14C` | `0x14D` | Staje u 6 bita |
|---|---|---|---|---|---|
| 100 | 2 | 2 | 4 | 4 | da |
| 150 | 4 | 4 | 8 | 8 | da |
| 300 | 8 | 8 | 16 | 16 | da |
| 600 | 16 | 16 | 32 | 32 | da |
| **1200** | 32 | 32 | **64** | **64** | **ne** |
| **2400** | **64** | **72** | **128** | **136** | **ne** |

`data_bitset` ne proverava opseg. Za masku `0x3F` radi
`(*address & 0xC0) | (data & 0x3F)`, pa:

- na **1200 dpi** `64 & 0x3F == 0` → pomak **plavog** kanala postaje nula
- na **2400 dpi** i zeleni ispada (`64 → 0`), plavi takođe (`128 → 0`),
  a `72 → 8` i `136 → 8`

Rezultat je da plava (i na 2400 zelena) ravan ostaje **nepomerena**, pa slika
dobija obojene rubove — tačno simptom koji bi se opisao kao „1200 i 2400 ne
rade kako treba".

Granica je oštra i pada **između 600 i 1200 dpi**, što se poklapa sa listom
rezolucija koje backend izlaže (`50, 75, 100, 150, 200, 300, 600`).

### Šta ovo znači

Ovo je **hipoteza sa konkretnom aritmetikom**, ne dokaz. Moguće je da čip ima
šire polje nego što maska sugeriše, ili da postoji drugi registar koji
referenca ne koristi. Ali je proverljiva, i daje H8 nešto određeno da izmeri
umesto „probaj 1200 dpi i vidi".

Ako se potvrdi, rešenje je poznato: `ARRANGELINE` za G2710 je `FIX_BY_HARD`
(`srt_hp3800_scanparam_get` daje 1), a referenca podržava i `FIX_BY_SOFT`, gde
se poravnanje radi u softveru i nema šestobitno ograničenje. Naš
`LineOffsetCorrector` je upravo taj put.

### Kako se rešava kod nas

`hardwareAlignmentSupported()` **odbija** rezoluciju čije vrednosti ne staju,
umesto da ih tiho odseče. `LineOffsetCorrector` radi isto poravnanje u
softveru, bez ograničenja. Testovi u `tests/golden/line_offset_test.cpp` drže
zaključanu i tabelu prelivanja i činjenicu da odsecanje daje nulu.

---

## D4 — `Resize_Increase` u lineart režimu meša indeks bita sa vrednošću bita

**Zahvaćeno:** nijedna rezolucija koju G2710 nudi. Zapisano jer je ista
funkcija na dohvat ruke i lako je posegnuti za njom.

`rts8822.c:6040`, lineart grana `Resize_Increase`:

```c
bit = (((0x80 >> cont) & *from_buffer) != 0) ? 1 : 0;
...
if ((((myres - sres) * lfad8) + (bit * sres)) > to_resolution)
  *to_buffer |= (0x80 >> bit);          /* <- bit je 0 ili 1, ne pozicija */

bit++;
if (bit == 8) { bit = 0; to_buffer++; *to_buffer = 0; }
```

`bit` je istovremeno **vrednost** izvornog piksela (`0` ili `1`, dodeljena
gore) i **pozicija** u izlaznom bajtu (`0x80 >> bit`, pa `bit++` i poređenje sa
osam). Dve različite veličine u jednoj promenljivoj. Posledica: izlaz može
upisati samo bitove 0 i 1 svakog bajta, a brojač koji treba da napreduje do
osam prepisuje se svakim novim izvornim pikselom.

Ista grana ima i `cont` inicijalizovan na `1` a poređen sa `8`, pa se izvorni
bajt pomera za jednu poziciju pomereno.

Nepovezano sa D3 — ovo je greška u čistom softverskom resampleru, bez ikakve
veze sa hardverom.

### Zašto nas ne pogađa

`Resize_Increase` se poziva samo kada je tražena rezolucija **veća** od
najveće koju tabela ima (`RTS_Scanner_SetupScan`, `rts8822.c:1693`). Za G2710
je 2400 dpi i najveća native i najveća ponuđena, pa je grana nedostižna.

### Kako se rešava kod nas

Ne portuje se. `native/core/image/Resize.cpp` sadrži **samo** smanjivanje —
vodoravno iz `Resize_Decrease` i uspravno iz `Read_ResizeBlock`. Planer koji
bi ipak zatražio `ResizeType::Increase` naleteo bi na `InvalidArgument` iz
`resizeLineDown`, a ne na tiho pokvarenu sliku.

---

## Zašto flatbed nije zahvaćen

D1 i D2 tiču se isključivo TMA putanje. Flatbed koristi bit `0x40` u
`0xE946`, koji `Set` i `Get` tretiraju **saglasno** u obe grane.

D3 ne pogađa flatbed do 600 dpi, što je obim koji 1.0 obećava. Pogađa upravo
1200 i 2400, koje su i inače `HARDWARE-VALIDATED = DEFERRED`.

D4 je na putanji koju G2710 nikada ne uzima.

## Šta ovo znači za plan

MASTER plan je TMA odložio u 1.1 uz obrazloženje da je to najslabije testiran
deo `hp3900` koda. D1 i D2 su konkretan dokaz za tu procenu, i to baš na našem
čipsetu.

D3 menja karakter rizika oko 1200/2400 dpi. Plan je govorio da ulazimo u
„poznato pokvarenu oblast" bez znanja *zašto*. Sada postoji merljiva hipoteza
i, ako se potvrdi, poznat put rešenja.

## Kako se rešava

Naša implementacija:

- **prati referencu doslovno** tamo gde je jednoznačna,
- **ne reprodukuje** `FALSE` iz D1 — `setLamp(kind, on)` poštuje `on`, jer je
  suprotno očigledno nenamerno,
- **zadržava** neslaganje iz D2 vidljivim: `lampStatus()` čita ono što
  referenca čita, `setLamp()` piše ono što referenca piše, a test
  `TmaSelectBitIsWrittenAndReadFromDifferentBytes` to razliku drži zaključanom
  umesto da je zataška.

H3 (kvalifikacija lampe) mora da odgovori koja je strana ispravna. Do tada TMA
ostaje van obima.

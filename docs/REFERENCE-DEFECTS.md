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

## Zašto flatbed nije zahvaćen

Obe stavke tiču se isključivo TMA putanje. Flatbed koristi bit `0x40` u
`0xE946`, koji `Set` i `Get` tretiraju **saglasno** u obe grane. Obim 1.0 je
netaknut.

## Šta ovo znači za plan

MASTER plan je TMA odložio u 1.1 uz obrazloženje da je to najslabije testiran
deo `hp3900` koda. Ove dve stavke su konkretan dokaz za tu procenu, i to baš
na našem čipsetu.

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

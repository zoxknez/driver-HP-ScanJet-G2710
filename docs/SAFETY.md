# Bezbednost udaljenog hardvera

Skener nije kod nas tokom razvoja. Sve u ovom dokumentu postoji zato što grešku
ne možemo videti, čuti ni zaustaviti rukom.

---

## 1. Dva nivoa bezbednosti, ne jedan

```
effective = min(BuildSafetyCeiling, RequestedSafetyLevel)
```

| | |
|---|---|
| `BuildSafetyCeiling` | **nepromenljiv**, ugrađen u binarni fajl u build-u |
| `RequestedSafetyLevel` | šta pozivalac traži (CLI, config, API) |

Plafon se **nikada** ne može podići u runtime-u. Qualification build sa
plafonom 1 odbija sve pozive nivoa 2–5 bez obzira na `--safety-level 5`.

| Nivo | Dozvoljeno |
|---|---|
| 1 | enumeracija, USB descriptor, read-only registri |
| 2 | + safe konfiguracioni write, lampe |
| 3 | + motor / HOME — **od ovde se uređaj fizički kreće** |
| 4 | + CCD akvizicija |
| 5 | + pun scan |

Zahtev iznad plafona se **tiho spušta**, ali `SafetyGate::wasClamped()` to
prijavljuje — dijagnostika mora moći da kaže „tražili ste 5, ovaj paket
dozvoljava 1" umesto da tiho radi nešto drugo. Ono što se **odbija** je pokušaj
izvršenja operacije iznad efektivnog nivoa, i to kao **hard error**
(`ErrorCode::SafetyViolation`), ne upozorenje.

### Odsustvo koda, ne runtime provera

```c
#if G2710_BUILD_SAFETY_CEILING >= 3
#define G2710_MOTOR_PATH_COMPILED 1
#else
#define G2710_MOTOR_PATH_COMPILED 0
#endif
```

U build-u sa plafonom < 3 motorni kod se **ne prevodi**. Paket koji prvi ide
prijatelju fizički ne sadrži put do motora.

Plafon mora biti **jedna vrednost po binarnom fajlu**. Zato se
`tests/unit/safety_ceiling1_test.cpp` gradi kao zaseban izvršni fajl sa
sopstvenom kopijom `SafetyLevel.cpp` — mešanje različitih plafona u istom
binarnom fajlu bilo bi ODR kršenje i dalo bi lažno prolazan test.

---

## 2. Gubitak veze — šta softver sme da tvrdi

Ako se USB fizički iščupa dok se motor kreće, softver **nema više transporta**
kojim bi poslao STOP. Zato ne tvrdimo da možemo zaustaviti motor.

```
AKO transport postoji:
    EmergencyStop MORA biti pokušan, i ishod se loguje.

AKO je veza nestala:
    -> odmah TransportLost / Faulted
    -> NIJEDNA dalja motion komanda se ne izdaje
    -> pozicija glave se proglašava NEPOZNATOM
    -> posle reconnect-a HOME je OBAVEZAN pre bilo čega drugog
```

Razlika koju držimo eksplicitnom:

- ✗ „softver garantuje da je motor stao"
- ✓ „softver garantuje da nikada neće nastaviti da izdaje kretanje;
  ponašanje hardvera pri iščupanom USB-u je zasebno kvalifikovano u H4/H13"

Zato `ErrorCode` razlikuje `Timeout` od `TransportLost`. Prvi ostavlja uređaj
upotrebljivim; drugi invalidira poziciju. `UsbScanTransport::reopen()` vraća
vezu, ali **ne** rešava poziciju — to je posao sloja iznad.

`WAIT_ABANDONED` iz `DeviceArbiter` znači isto: prethodni držalac je pao bez
oslobađanja, bravu dobijamo, ali uređaj je u nepoznatom stanju i HOME je
obavezan.

---

## 3. Arbitraža — `Global\` i jedna zamka

WIA servis radi u **Session 0**, a TWAIN, aplikacija i CLI u interaktivnoj
sesiji. Named objekat bez eksplicitnog namespace-a je **session-local**, pa bi
svaka strana zaključala svoj objekat i obe bi mislile da poseduju skener.

Zato brava ide u `Global\` sa eksplicitnim SD/ACL koji dozvoljava `LocalSystem`
(SY), lokalne administratore (BA) i interaktivnog korisnika (IU).

### Zamka: mutex i section se ne ponašaju isto

Izmereno na Windows 11, neelevirani korisnik:

| Poziv | Rezultat |
|---|---|
| `CreateMutexW(Global\...)` | **uspeva** |
| `CreateFileMappingW(Global\...)` | **pada, `ERROR_ACCESS_DENIED` (5)** |

`SeCreateGlobalPrivilege` se traži za **section** objekte, ne za mutekse, a
običan korisnik ga nema.

Posledica za dizajn: `DeviceArbiter` ima **dva odvojena** dometa.

| | Nosi | Sme li da degradira |
|---|---|---|
| `scope()` | brava — **korektnost arbitraže** | **ne** |
| `ownerChannelScope()` | ime vlasnika — dijagnostika | da |

Kanal sa imenom vlasnika ide u tri koraka: kreiraj `Global\` (uspeva servisu),
otvori postojeći `Global\` (uspeva klijentu kada ga je servis napravio), pa tek
onda `Local\`. Ako padne na `Local\`, arbitraža je i dalje ispravna — gubi se
samo ime u poruci.

> Da su ovo dva imena istog polja, neko bi „popravio" praznu dijagnostiku
> spuštanjem cele arbitraže na `Local\` i tiho razbio međusesijsko
> isključivanje — grešku koja se vidi tek kod prijatelja, kao nasumičan
> `ERROR_SHARING_VIOLATION`.

`tests/unit/device_arbiter_test.cpp::OwnerChannelDegradesWithoutBreakingArbitration`
zaključava upravo tu razliku.

### Sesije

| | |
|---|---|
| `StatusSession` | jeftina, deljiva, read-only — **ne uzima bravu** |
| `DataSession` | ekskluzivna, jedina sme da pomera motor |

Status upit ne sme blokirati scan koji je u toku, ni obrnuto. WIA servis
legitimno drži status-mode instancu dugo.

---

## 4. Cancel mora stvarno raditi

`CreateFileW` ide sa `FILE_FLAG_OVERLAPPED`, a `cancel()` koristi `CancelIoEx`
nad istim handle-om. Bez toga cancel usred scana ne radi, a to je zahtev i WIA
i TWAIN sloja.

`cancel()` je bezbedno pozvati iz drugog thread-a dok transfer traje — to je
jedini način da uopšte ima smisla.

---

## 5. Šta tek dolazi (G2710-4)

`MotionGuard` — nijedna motorna operacija bez `Direction`, `ExpectedSteps`,
`MaximumSteps`, `StartPosition`, `ExpectedEndPosition`, `Deadline`,
`CancellationToken`, `OnUsbLost`, `EmergencyStop`.

Nijedan `while (!home) step();` bez zaštite ne prolazi review.

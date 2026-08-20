# NOTICE — hp3900 provenance

Ovaj projekat je **izvedeni rad** iz `hp3900` SANE backend-a.

```
hp3900 SANE backend
Copyright (C) 2005-2008 Jonathan Bravo Lopez <jkdsoft@gmail.com>
Licensed under the GNU General Public License, version 2 or later.
```

Ceo projekat je zato licenciran pod **GPL-2.0-or-later** (`LICENSE`).
Svaka binarna distribucija mora biti praćena odgovarajućim izvornim kodom.

Referentni izvor stoji nemodifikovan u `third_party/hp3900-reference/`
sa originalnim copyright headerima i `COPYING`. Taj direktorijum se **ne
kompajlira** u naš build — služi kao reference truth i kao dokaz porekla.

## Provenance mapa

Popunjava se kako moduli nastaju. Format: naš fajl ← hp3900 izvor.

| Naš fajl | Izveden iz | Napomena |
|---|---|---|
| `docs/PROTOCOL-RTS8822.md` | `hp3900_usb.c`, `hp3900_rts8822.c` | analiza, ne kod |
| `docs/G2710-PROFILE.md` | `hp3900_config.c`, `hp3900_types.c`, `hp3900_sane.c` | analiza, ne kod |
| `native/core/device/G2710Profile.generated.h` | `hp3800_*` familija u `hp3900_config.c` | generisano |

## SANE link exception

Originalni headeri sadrže SANE link exception. Ona se odnosi na linkovanje SANE
biblioteka i **ne** oslobađa izvedeni rad GPL obaveza. Ne oslanjamo se na nju.

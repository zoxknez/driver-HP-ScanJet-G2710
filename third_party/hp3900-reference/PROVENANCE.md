# hp3900 reference — provenance

Nemodifikovana kopija `hp3900` SANE backend izvornog koda, korišćena kao
reference truth za G2710 protokol. **Ne kompajlira se** u naš build.

| Polje | Vrednost |
|---|---|
| Upstream | https://gitlab.com/sane-project/backends |
| Putanja | `backend/hp3900*` |
| Commit | `951e8a5a08b05b8fe1914219dc087dd9c87f646e` |
| Commit datum | 2026-08-18 |
| Preuzeto | 2026-08-20 |
| Autor | Jonathan Bravo Lopez `<jkdsoft@gmail.com>`, 2005–2008 |
| Licenca | GPL-2.0-or-later, sa SANE link exception |
| Backend verzija | `BACKEND_VRSN "0.12"` |

Kompletnost potvrđena protiv `backend/Makefile.am`:
`EXTRA_DIST += hp3900_config.c hp3900_debug.c hp3900_rts8822.c hp3900_sane.c hp3900_types.c hp3900_usb.c`
plus `hp3900.c` (`libhp3900_la_SOURCES`) i `hp3900.conf.in`.

Integritet: `SHA256SUMS.txt`.

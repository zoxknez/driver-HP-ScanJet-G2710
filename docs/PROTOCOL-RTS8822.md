# RTS8822 protokol — transport truth

Izvor: `third_party/hp3900-reference/` @ `951e8a5a` (vidi `PROVENANCE.md`).
Status: **G2710-0 Gate B ZATVOREN — PASS.**

---

## 1. Transport invariant (Gate B)

> **ASSERT:** svaki control transfer dostižan u HPG2710 code path-u koristi vendor
> request type `0x40` ili `0xC0`.

**Rezultat: PASS — strukturno, ne statistički.**

Ceo backend ima tačno **4** `control_msg` call site-a, svi u `hp3900_usb.c`,
svi sa literalima:

| Fajl:linija | Funkcija | `bmRequestType` | `bRequest` |
|---|---|---|---|
| `hp3900_usb.c:370` | `usb_ctl_write` (standalone/libusb) | `0x40` | `0x04` |
| `hp3900_usb.c:380` | `usb_ctl_write` (SANE) | `0x40` | `0x04` |
| `hp3900_usb.c:413` | `usb_ctl_read` (standalone/libusb) | `0xC0` | `0x04` |
| `hp3900_usb.c:423` | `usb_ctl_read` (SANE) | `0xC0` | `0x04` |

Broj poziva `usb_ctl_read`/`usb_ctl_write` **van** `hp3900_usb.c`:

```
hp3900_rts8822.c  0
hp3900_config.c   0
hp3900_sane.c     0
hp3900_types.c    0
hp3900_debug.c    0
```

Nema nijedne grane koja bi mogla proizvesti drugačiji `bmRequestType` — vrednost
nije parametar nego literal u dve funkcije. Invariant ne zavisi od analize
dostižnosti.

**Posledica:** `usbscan.sys` kao primarni transport je potvrđen. usbscan.sys iz
`IO_BLOCK_EX` gradi `URB_CONTROL_VENDOR_OR_CLASS_REQUEST` i smer izvodi iz
`fTransferDirectionIn` — što tačno proizvodi `0xC0` (read) i `0x40` (write).
Protokol nema nijedan oblik koji usbscan.sys ne može da izrazi.

---

## 2. Control transfer — mapiranje na `IO_BLOCK_EX`

```
hp3900:   usb_ctl_read (handle, address, buffer, size, index)
          usb_ctl_write(handle, address, buffer, size, index)

USB:      bmRequestType = 0xC0 (read) / 0x40 (write)
          bRequest      = 0x04
          wValue        = address
          wIndex        = index          ← KOMANDA, ne adresa
          wLength       = size
```

```c
IO_BLOCK_EX b = {};
b.bRequest             = 0x04;
b.uOffset              = address;   // wValue
b.uIndex               = index;     // wIndex
b.uLength              = size;      // wLength
b.pbyData              = buffer;
b.fTransferDirectionIn = read ? TRUE : FALSE;
// b.bmRequestType se NE popunjava kao izvor istine — usbscan.sys ga ne koristi
DeviceIoControl(h, IOCTL_SEND_USB_REQUEST, &b, sizeof b, &b, sizeof b, &ret, &ov);
```

---

## 3. `wIndex` je selektor komande

Najvažniji nalaz faze 0. `wIndex` nije proširenje adrese — bira **adresni prostor
odnosno operaciju**. Kompletan skup korišćen u backendu:

| `wIndex` | Značenje | Izvor |
|---|---|---|
| `0x0000` | Register **write** | `Write_Word`, `Write_Buffer`, `IWrite_Byte` faza 2 |
| `0x0100` | Register **read** | `Read_Byte/Word/Integer/Buffer`, `IWrite_Byte` faza 1 |
| `0x0200` | **EEPROM** read/write | `RTS_EEPROM_*` (`rts8822.c:14304-14401`) |
| `0x0400` | DMA **enable read** | `RTS_DMA_Enable_Read` |
| `0x0401` | DMA **enable write** | `RTS_DMA_Enable_Write` |
| `0x0600` | DMA **cancel** | `RTS_DMA_Cancel` |
| `0x0800` | DMA **operation-type** selektor | `RTS_DMA_Reset` (op `0x0000`), shading upload (op `0x0014`) |
| `0x0801` | **Chipset reset** | `Chipset_Reset` |

Register read koristi `0x0100`, a register write `0x0000` — **asimetrija je
namerna i mora se reprodukovati doslovno.**

G2710 chipset je `RTS8822BL-03A` sa `CAP_EEPROM`, pa je `0x0200` grana **aktivna**
za naš uređaj.

---

## 4. Accessor sloj — semantika koju moramo reprodukovati

| Accessor | Smer | `wIndex` | `wLength` | Napomena |
|---|---|---|---|---|
| `Read_Byte(addr)` | IN | `0x100` | 2 | čita 2 bajta, koristi `buffer[0]` |
| `Read_Word(addr)` | IN | `0x100` | 2 | little-endian |
| `Read_Integer(addr)` | IN | `0x100` | 4 | little-endian |
| `Read_Buffer(addr,n)` | IN | `0x100` | n | |
| `Write_Word(addr)` | OUT | `0x000` | 2 | little-endian |
| `Write_Buffer(addr,n)` | OUT | `0x000` | n | |
| `Write_Byte(addr,d)` | **IN+OUT** | `0x100` pa `0x000` | 2 pa 2 | **read-modify-write** |

### `Write_Byte` je read-modify-write, ne prost upis

```c
usb_ctl_read (h, address + 1, buf, 2, 0x100);   // procitaj susedni par
buf[1] = buf[0] & 0xff;                          // stari bajt se pomera
buf[0] = data   & 0xff;                          // novi bajt ide na poziciju 0
usb_ctl_write(h, address, buf, 2, 0x0000);       // upisi par
```

Upis **jednog** registarskog bajta je **dva** USB transfera i dira **dva** registra.
Naivna implementacija `Write_Byte` kao jednog OUT transfera bi tiho korumpirala
susedni registar. Ovo je obavezan golden-sequence test u fazi G2710-2.

---

## 5. Bulk transfer

| Parametar | Vrednost |
|---|---|
| Bulk IN endpoint | `0x81` |
| Bulk OUT endpoint | `0x02` |
| Timeout | `1000` ms |

Bulk se koristi **isključivo** za DMA payload (`Read_Bulk` / `Write_Bulk`), po
jedan call site svaki. Nema komandi preko bulk-a.

### Kanonska DMA write sekvenca

```
IWrite_Word(0x0000, opType, 0x0800)         // izbor tipa operacije
RTS_DMA_Enable_Write(dmacs, size, options)  // wIndex 0x0401, 6 bajtova
Bulk_Operation(BLK_WRITE, size, buffer)     // bulk EP 0x02
```

`RTS_DMA_Enable_*` payload je 6 bajtova: `options` MSB-first u `[0..2]`,
`size / 2` (broj **reči**, ne bajtova) LSB-first u `[3..5]`.

---

## 6. Register bank

| Konstanta | Vrednost | Izvor |
|---|---|---|
| `RT_BUFFER_LEN` | `0x71a` = **1818** bajtova | `hp3900_types.c:144` |
| Bazna adresa | `0xe800` | `RTS_WriteRegs` / `RTS_ReadRegs` |

`RTS_WriteRegs` / `RTS_ReadRegs` prenose **ceo bank od 1818 bajtova u jednom
control transferu**.

> ### ⚠ RIZIK — nije bio predviđen u MASTER planu u ovom obliku
> Plan je predviđao samo limit na **bulk** transfere. Ovde imamo **control**
> transfer sa `wLength = 1818`. Nepoznato je da li `usbscan.sys` nameće limit na
> veličinu `IOCTL_SEND_USB_REQUEST` payload-a.
>
> **Mitigacija:** `UsbScanTransport` mora podržati chunked register-bank prenos
> iza istog API-ja, a H2 dobija eksplicitan test: pun `0x71a` transfer u jednom
> komadu, pa isti prenos u komadima, sa poređenjem rezultata.

---

## 7. Ostali fiksni parametri

| Parametar | Vrednost |
|---|---|
| USB VID / PID | `0x03F0` / `0x2805` |
| Chipset | `RTS8822BL-03A` (`RTS8822BL_03A = 0x02`) |
| Chipset capabilities | `CAP_EEPROM` |
| Model id u backendu | `HPG2710 = 0x07` |

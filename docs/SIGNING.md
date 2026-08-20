# Driver package i potpisivanje

Status: **lanac dokazan od kraja do kraja**, sa razvojnim sertifikatom, na ovoj
mašini. `signtool verify /pa` prolazi.

---

## 1. Lanac

```
InfVerif  g2710.inf
      ↓
Inf2Cat  /driver:<paket>  /os:10_VB_X64,10_NI_X64,10_GE_X64,10_25H2_X64
      ↓
g2710.cat
      ↓
signtool sign   /fd SHA256 /tr <timestamp> /td SHA256
      ↓
signtool verify /pa /v
```

Pokreće se sa `driver/sign/sign-package.ps1`.

> **`MakeCat` se ne koristi.** On je za ručno pravljene kataloge. INF-installed
> PnP paket ide kroz `Inf2Cat`. Ovo je bila greška u ranoj verziji plana.

### Gde alati zapravo žive

Lako se promaši — nisu na istom mestu:

| Alat | Putanja | Napomena |
|---|---|---|
| `Inf2Cat.exe` | `Windows Kits\10\bin\<ver>\**x86**\` | postoji **samo** kao x86 |
| `infverif.exe` | `Windows Kits\10\**Tools**\<ver>\x64\` | u `Tools\`, **ne** u `bin\` |
| `signtool.exe` | `Windows Kits\10\bin\<ver>\x64\` | dolazi sa SDK |
| `stampinf.exe` | `Windows Kits\10\bin\<ver>\x64\` | |

`Inf2Cat` i `InfVerif` dolaze sa **WDK**-om, ne sa SDK-om. KMDF i custom
kernel-driver projekat nam i dalje nisu potrebni — ne isporučujemo nijedan
`.sys`.

Instalacija: `winget install --id Microsoft.WindowsWDK.10.0.26100` (verzija se
mora poklapati sa instaliranim SDK-om).

---

## 2. Ciljne OS verzije

```
10_VB_X64     Windows 10, 2004 – 22H2
10_NI_X64     Windows 11, 22H2
10_GE_X64     Windows 11, 24H2
10_25H2_X64   Windows 11, 25H2
```

Windows Server nije cilj projekta, pa `Server*` tokeni ne ulaze.
`10_CO_X64` (Windows 11 21H2) se dodaje ako se pojavi tester na toj verziji.

---

## 3. Otvoreno pitanje: universal driver package

`InfVerif /w` (universal / declarative provere) **odbija** naš INF:

```
ERROR(1320): Registry root 'HKCR\CLSID\{...}' is not isolated to HKR
```

Universal paket sme pisati **samo** u `HKR`, a COM registracija WIA
minidriver-a zahteva `HKCR\CLSID\...\InprocServer32`. Bez te registracije WIA
servis ne može da instancira drajver.

U podrazumevanom (legacy) režimu INF je **VALID**, i tako se trenutno gradi.

### Odluka se donosi u G2710-12

| Opcija | Posledica |
|---|---|
| **A. Legacy INF** (trenutno) | radi na Win10/11, `pnputil` instalira samostalno; ne prolazi universal provere |
| **B. Universal INF + COM registracija iz MSI-ja** | paket prolazi `/w`, ali drajver više nije samostalno instalabilan — traži installer |

Izbor zavisi od toga da li idemo na Partner Center submission. Do tada opcija A,
jer je samostalna instalacija preko `pnputil` upravo ono što treba za H1 kod
prijatelja.

---

## 4. `SIGNING_MODE` — isti paket, drugi potpis

| | Ključ | Kada |
|---|---|---|
| `Development` | self-signed `.pfx` sa razvojne mašine | H1–H13, alpha, beta |
| `Release` | EV / HSM sertifikat na potpisničkoj mašini (`signtool /a`) | javni release |

Drajver se **ne menja** između njih. Menja se samo potpis, pa prelazak na
produkciju ne zahteva ponovnu kvalifikaciju hardvera.

Produkcioni ključ nikada ne stoji u repozitorijumu. Razvojni `.pfx` ima fiksnu
i beskorisnu lozinku namerno — nikada ne napušta lokalnu mašinu, a
`out/` je u `.gitignore`.

---

## 5. Poverenje u sertifikat na ciljnoj mašini

Potpisan paket nije dovoljan: sertifikat mora biti u **Trusted Root** *i*
**Trusted Publishers**, inače PnP odbija instalaciju. Isti pristup koriste
libwdi i Zadig.

`driver/sign/make-dev-cert.ps1 -Install` to radi.

> **Zamka:** `Import-Certificate` za root store traži GUI potvrdu i pada sa
> `UI is not allowed in this operation` u neinteraktivnom radu — CI, installer,
> remote sesija. Skript zato koristi `X509Store` API direktno.

### H1-A je hipoteza, ne činjenica

```
H1-A   Secure Boot ON · Memory Integrity (HVCI) ON · TESTSIGNING OFF
       self-signed katalog u LocalMachine\Root + TrustedPublisher
       → pokušaj PnP instalacije

       PASS → hipoteza potvrđena, Secure Boot se NIKAD ne dira
       FAIL → H1-B, development test-signing path
```

Obrazloženje hipoteze: paket ne isporučuje **nijedan novi kernel binarni fajl**
(`usbscan.sys` je već Microsoft-potpisan i in-box), pa bi provera integriteta
trebalo da se svede na katalog. Prijatelja **ne teramo** unapred da gasi Secure
Boot ni HVCI; `system-info.json` beleži stanje sve tri opcije.

---

## 6. CLSID mora biti na dva mesta isti

`driver/g2710.inf` (`G2710.CLSID` u `[Strings]`) i `native/wia/G2710Wia.h`
(`G2710_WIA_CLSID_STRING`).

Ako se raziđu, skener se pojavi u sistemu ali ne radi — a to se vidi tek kada
je uređaj priključen, kod prijatelja, na daljinu.
`tests/unit/wia_clsid_test.cpp` zaključava saglasnost i proveren je negativnim
testom (izmena CLSID-a u INF-u obara test).

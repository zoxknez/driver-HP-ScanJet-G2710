# MSI proizvodnog paketa

`tools/build-installer.ps1` pravi jedan x64 MSI koji sadrži aplikaciju, WIA
paket i obe TWAIN arhitekture. TWAIN x64 se instalira u `C:\Windows\twain_64`,
a x86 u `C:\Windows\twain_32`, pa ih DSM pronalazi bez ručnog kopiranja. MSI
čisto uklanja svoje fajlove pri deinstalaciji.
Podrazumevani `-SigningMode Development` pravi i proverava INF katalog sa
razvojnim sertifikatom; `-SigningMode Release` bira sertifikat sa potpisničke
mašine (EV/HSM), bez ključa u repozitorijumu.

## Jezik

Instalater pita na kom jeziku program govori pre nego što bilo šta prekopira, i
odgovor upisuje u `HKLM\SOFTWARE\G2710\Language`. Engleski je podrazumevan.
Tiha instalacija ne vidi nijedan dijalog, pa vrednost stiže sa komandne linije:

```powershell
msiexec /i G2710-x64.msi /qn G2710LANGUAGE=sr
```

Izbor se kasnije menja izmenom te vrednosti u registru, bez ponovne
instalacije. Utiče samo na tekst na ekranu — izveštaji koje program piše ostaju
na engleskom, jer ih čita onaj kome se šalju.

## Instalacija drajvera

Instalacija drajvera je namerno odvojena, jer mora da bude pokrenuta povišeno i
jer korisnik mora da vidi rezultat `pnputil`/Secure Boot provere. Posle MSI
instalacije, pokrenuti kao administrator:

```powershell
powershell -ExecutionPolicy Bypass -File "$env:ProgramFiles\HP ScanJet G2710\driver\install.ps1"
```

Za uklanjanje driver paketa i razvojnog sertifikata dodaje se `-Uninstall`, a
`-Language sr` ako se poruke radije čitaju na srpskom.

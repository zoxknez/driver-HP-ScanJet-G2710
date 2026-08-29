# Production MSI package

`tools/build-installer.ps1` produces one x64 MSI containing the application,
the WIA package and both TWAIN architectures. TWAIN x64 is installed into
`C:\Windows\twain_64` and x86 into `C:\Windows\twain_32`, so the DSM finds them
without any manual copying. The MSI removes its own files cleanly on uninstall.

The default `-SigningMode Development` builds and verifies the INF catalogue
with the development certificate; `-SigningMode Release` picks the certificate
from the signing machine (EV/HSM), with no key in the repository.

## Language

The installer asks which language the program should speak before it copies
anything, and writes the answer to `HKLM\SOFTWARE\G2710\Language`. English is
the default. A silent installation never sees a dialog, so the value comes from
the command line instead:

```powershell
msiexec /i G2710-x64.msi /qn G2710LANGUAGE=sr
```

The choice can be changed later by editing that registry value; there is no need
to reinstall. It affects only the text on screen - the reports the program
writes stay in English, because they are read by whoever receives them.

## Installing the driver

Driver installation is deliberately separate: it has to run elevated, and the
user has to see the result of the `pnputil` / Secure Boot check. After the MSI
has been installed, run as administrator:

```powershell
powershell -ExecutionPolicy Bypass -File "$env:ProgramFiles\HP ScanJet G2710\driver\install.ps1"
```

Add `-Uninstall` to remove the driver package and the development certificate,
and `-Language sr` if you would rather read the messages in Serbian.

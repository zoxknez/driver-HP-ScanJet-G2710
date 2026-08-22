// Stvarni DSM smoke, namerno bez GTest-a: poziva binarni TWAINDSM.DLL koji je
// prosledjen u komandnoj liniji i meri da DSM nadje NAS .ds fajl u Windows
// discovery direktorijumu. Ne otvara uredjaj i ne trazi simulator.

#include <Windows.h>
#include "twain.h"

#include <cstring>
#include <iostream>
#include <string>

namespace {

void appIdentity(TW_IDENTITY& app) {
    std::memset(&app, 0, sizeof(app));
    app.Version.MajorNum = 0;
    app.Version.MinorNum = 1;
    app.ProtocolMajor = TWON_PROTOCOLMAJOR;
    app.ProtocolMinor = TWON_PROTOCOLMINOR;
    app.SupportedGroups = DG_CONTROL | DG_IMAGE;
    strcpy_s(app.Manufacturer, "G2710 Project");
    strcpy_s(app.ProductFamily, "G2710 DSM smoke");
    strcpy_s(app.ProductName, "G2710 DSM smoke");
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: g2710_twaindsm_smoke <TWAINDSM.dll>\n";
        return 2;
    }
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) {
        std::cerr << "DSM nije ucitan: " << GetLastError() << "\n";
        return 3;
    }
    const auto entry = reinterpret_cast<DSMENTRYPROC>(GetProcAddress(module, "DSM_Entry"));
    if (!entry) {
        std::cerr << "DSM nema DSM_Entry\n";
        FreeLibrary(module);
        return 4;
    }

    TW_IDENTITY app{};
    appIdentity(app);
    TW_HANDLE parent = GetConsoleWindow();
    if (entry(&app, nullptr, DG_CONTROL, DAT_PARENT, MSG_OPENDSM, &parent) != TWRC_SUCCESS) {
        std::cerr << "DSM OPENDSM nije uspeo\n";
        FreeLibrary(module);
        return 5;
    }

    TW_IDENTITY source{};
    const TW_UINT16 found = entry(&app, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETFIRST, &source);
    bool g2710Found = false;
    while (found == TWRC_SUCCESS) {
        if (std::strcmp(source.ProductName, "HP ScanJet G2710") == 0) {
            g2710Found = true;
            break;
        }
        if (entry(&app, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_GETNEXT, &source) != TWRC_SUCCESS) break;
    }
    if (!g2710Found) {
        (void)entry(&app, nullptr, DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, &parent);
        FreeLibrary(module);
        std::cerr << "DSM nije otkrio HP ScanJet G2710 .ds\n";
        return 7;
    }
    std::cout << "DSM DS id: " << source.Id << "\n";
    // Ovaj poziv ide kroz DSM do istog OPENDS prelaza koji koristi stvarna
    // aplikacija. Ne otvara USB/Core sesiju, pa je bezbedan bez uredjaja.
    if (entry(&app, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_OPENDS, &source) != TWRC_SUCCESS) {
        (void)entry(&app, nullptr, DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, &parent);
        FreeLibrary(module);
        std::cerr << "DSM nije otvorio G2710 DS\n";
        return 8;
    }
    if (entry(&app, nullptr, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &source) != TWRC_SUCCESS) {
        (void)entry(&app, nullptr, DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, &parent);
        FreeLibrary(module);
        std::cerr << "DSM nije zatvorio G2710 DS\n";
        return 9;
    }
    const auto close = entry(&app, nullptr, DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, &parent);
    FreeLibrary(module);
    if (close != TWRC_SUCCESS) {
        std::cerr << "DSM CLOSEDSM nije uspeo\n";
        return 6;
    }
    std::cout << "DSM je otkrio, otvorio i zatvorio HP ScanJet G2710 .ds\n";
    return 0;
}

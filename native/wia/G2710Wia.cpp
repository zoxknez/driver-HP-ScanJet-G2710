// G2710 WIA 2.0 minidriver - KOSTUR.
//
// Puna implementacija (IStiUSD + IWiaMiniDrv) dolazi u G2710-9. Ovaj modul
// postoji sada da bi se lanac pakovanja mogao dokazati od kraja do kraja:
//
//   InfVerif -> Inf2Cat -> signtool sign -> signtool verify
//
// Inf2Cat zahteva da svi fajlovi navedeni u INF-u postoje, pa bez ovoga
// signing pipeline ne bi mogao da se testira do faze 9. Dokazivanje pakovanja
// rano smanjuje rizik za H1, gde prijatelj prvi put instalira drajver.

#include <windows.h>
#include <objbase.h>

#include "G2710Wia.h"

namespace {

std::atomic<LONG> g_lockCount{0};
HMODULE g_module = nullptr;

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = module;
            ::DisableThreadLibraryCalls(module);
            break;
        default:
            break;
    }
    return TRUE;
}

// WIA servis instancira minidriver preko CoCreateInstance nad USDClass CLSID-om
// iz INF-a. Do faze G2710-9 nemamo sta da vratimo.
STDAPI DllGetClassObject(REFCLSID /*clsid*/, REFIID /*iid*/, LPVOID* out) {
    if (out != nullptr) {
        *out = nullptr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return g_lockCount.load(std::memory_order_acquire) == 0 ? S_OK : S_FALSE;
}

// Registraciju radi INF preko AddReg, ne self-registration - PnP driver paketi
// ne smeju zavisiti od DllRegisterServer. Eksportovano samo radi kompletnosti.
STDAPI DllRegisterServer() {
    return S_OK;
}

STDAPI DllUnregisterServer() {
    return S_OK;
}

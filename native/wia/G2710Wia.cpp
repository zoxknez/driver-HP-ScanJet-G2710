// G2710 WIA 2.0 minidriver - ulazne tacke DLL-a i fabrika objekata.
//
// WIA servis instancira drajver preko CoCreateInstance nad CLSID-om koji stoji
// u USDClass vrednosti iz driver/g2710.inf. Sve sto ovaj fajl radi je da taj
// poziv pretvori u G2710Usd; sve ostalo je tamo.
//
// Registracija ide kroz INF AddReg, ne kroz DllRegisterServer - PnP driver
// paketi ne smeju zavisiti od self-registration.

#include <windows.h>
#include <objbase.h>

#include "G2710Usd.h"
#include "G2710Wia.h"

#include <new>

namespace {

std::atomic<LONG> g_lockCount{0};
HMODULE g_module = nullptr;

// CLSID iz G2710Wia.h, u binarnom obliku. Mora odgovarati
// G2710_WIA_CLSID_STRING; WiaClsid test to drzi saglasnim sa INF-om.
// {2C4E8A1D-7F63-4B95-9E12-3A6D5C8B0417}
constexpr GUID kG2710UsdClsid = {
    0x2c4e8a1d, 0x7f63, 0x4b95, {0x9e, 0x12, 0x3a, 0x6d, 0x5c, 0x8b, 0x04, 0x17}};

class ClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(references_.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            delete this;
        }
        return static_cast<ULONG>(remaining);
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;

        // Agregacija se ne podrzava: G2710Usd drzi otvoren handle uredjaja i
        // njegov zivotni vek ne sme zavisiti od tudjeg objekta.
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }

        auto* usd = new (std::nothrow) g2710::wia::G2710Usd();
        if (usd == nullptr) {
            return E_OUTOFMEMORY;
        }
        const HRESULT result = usd->QueryInterface(riid, object);
        usd->Release();
        return result;
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) {
            g_lockCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_lockCount.fetch_sub(1, std::memory_order_acq_rel);
        }
        return S_OK;
    }

private:
    std::atomic<LONG> references_{1};
};

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

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    *out = nullptr;

    if (clsid != kG2710UsdClsid) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    auto* factory = new (std::nothrow) ClassFactory();
    if (factory == nullptr) {
        return E_OUTOFMEMORY;
    }
    const HRESULT result = factory->QueryInterface(iid, out);
    factory->Release();
    return result;
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

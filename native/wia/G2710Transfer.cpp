// drvAcquireItemData - prenos slike ka aplikaciji.
//
// Ovo je jedini WIA poziv koji zaista pomera skener, i jedini koji traje.
//
// Ono sto je OSTALO u ovom fajlu je tacno ono sto se bez WIA servisa ne moze
// izvrsiti: citanje osobina stavke (`wiasReadPropLong`) i put od wiasContext-a
// do naseg bloka (`wiasGetDrvItem`). Izmereno, ne pretpostavljeno - vidi
// komentar na vrhu WiaTransfer.h.
//
// Sve ostalo - lampa, plan, sesija, redovi, otkazivanje, napredak, zatvaranje
// prolaza - preselilo se u `runTransfer`, koji se testira nad simulatorom.
// Granica je povucena tamo gde prestaje ono sto se moze pogresiti.

#include "G2710MiniDrv.h"

#include "G2710Usd.h"
#include "WiaCapabilities.h"
#include "WiaTransfer.h"

#include <wiamdef.h>

namespace g2710::wia {
namespace {

// Procitaj podesavanja iz WIA skladista.
HRESULT readSettings(BYTE* wiasContext, ItemSettings* out) {
    LONG value = 0;

    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPS_XRES, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    out->xResolution = value;
    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPS_YRES, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    out->yResolution = value;
    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPA_DATATYPE, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    out->dataType = static_cast<WiaDataType>(value);
    if (FAILED(wiasReadPropLong(wiasContext, WIA_IPA_DEPTH, &value, nullptr, TRUE))) {
        return E_INVALIDARG;
    }
    out->wiaDepth = value;

    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_XPOS, &value, nullptr, FALSE))) {
        out->xPosition = value;
    }
    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_YPOS, &value, nullptr, FALSE))) {
        out->yPosition = value;
    }
    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_XEXTENT, &value, nullptr, FALSE))) {
        out->xExtent = value;
    }
    if (SUCCEEDED(wiasReadPropLong(wiasContext, WIA_IPS_YEXTENT, &value, nullptr, FALSE))) {
        out->yExtent = value;
    }
    return S_OK;
}

}  // namespace

STDMETHODIMP G2710MiniDrv::drvAcquireItemData(BYTE* wiasContext, LONG /*flags*/,
                                              PMINIDRV_TRANSFER_CONTEXT transfer,
                                              LONG* deviceError) {
    if (wiasContext == nullptr || transfer == nullptr || deviceError == nullptr) {
        return E_POINTER;
    }
    *deviceError = 0;

    G2710Device* device = owner_->device();
    if (device == nullptr) {
        return WIA_ERROR_OFFLINE;
    }

    // Kontekst stavke drzi sesiju; bez njega bi prenos bio bez vlasnika.
    //
    // Put je WIA kontekst -> IWiaDrvItem -> nas blok. Nema precice:
    // blok pripada STAVCI, a wiasContext je pogled servisa na nju.
    IWiaDrvItem* drvItem = nullptr;
    if (FAILED(wiasGetDrvItem(wiasContext, &drvItem)) || drvItem == nullptr) {
        return E_UNEXPECTED;
    }

    BYTE* rawContext = nullptr;
    const HRESULT gotContext = drvItem->GetDeviceSpecContext(&rawContext);
    if (FAILED(gotContext) || rawContext == nullptr) {
        return E_UNEXPECTED;
    }

    ItemContext* item = asItemContext(*reinterpret_cast<ItemContext**>(rawContext));
    if (item == nullptr) {
        return E_UNEXPECTED;
    }

    ItemSettings settings;
    if (const HRESULT result = readSettings(wiasContext, &settings); FAILED(result)) {
        return result;
    }

    WiaCallbackSink sink{transfer};
    return runTransfer(*device, *item, settings, sink, deviceError);
}

}  // namespace g2710::wia

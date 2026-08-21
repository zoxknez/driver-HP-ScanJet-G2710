// IWiaMiniDrv - WIA 2.0 DDI.
//
// Stablo je koliko i uredjaj: Root -> Flatbed. Jedna stavka, jer 1.0 nudi samo
// flatbed; TMA bi dodala drugu i tu je jedina promena koju bi zahtevala.
//
// Podela posla u ovom fajlu:
//
//   drvInitializeWia          napravi stablo
//   drvInitItemProperties     upisi sta se nudi (iz WiaCapabilities)
//   drvValidateItemProperties popravi ili odbij (isto)
//   drvAcquireItemData        vodi prenos kroz ScanSession
//
// Sve odluke su u WiaCapabilities; ovde je prevod na WIA pozive. To je i
// razlog zasto ovaj fajl nema nijedan `if` o rezolucijama.
//
// DRUGI OBJEKAT, ISTA INSTANCA: G2710Usd i G2710MiniDrv su odvojeni interfejsi
// nad istim uredjajem. WIA servis dolazi do minidriver-a preko QueryInterface
// na USD objektu, pa oba zive u istom C++ objektu i dele transport.

#pragma once

#include <windows.h>

#include <wia.h>

#include <wiamindr.h>

#include "WiaItemContext.h"

namespace g2710 {
class G2710Device;
}

namespace g2710::wia {

class G2710Usd;

// Implementacija je metod-po-metod na G2710Usd; ova klasa je samo vtable koji
// WIA servis vidi. Vlasnik je USD objekat i njegov zivotni vek je merodavan.
class G2710MiniDrv final : public IWiaMiniDrv {
public:
    explicit G2710MiniDrv(G2710Usd* owner) : owner_(owner) {}

    // --- IUnknown --------------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // --- IWiaMiniDrv -----------------------------------------------------
    STDMETHODIMP drvInitializeWia(BYTE* wiasContext, LONG flags, BSTR deviceId,
                                  BSTR rootFullItemName, IUnknown* stiDevice,
                                  IUnknown* unknownInner, IWiaDrvItem** drvItemRoot,
                                  IUnknown** unknownOuter, LONG* deviceError) override;

    STDMETHODIMP drvAcquireItemData(BYTE* wiasContext, LONG flags,
                                    PMINIDRV_TRANSFER_CONTEXT transfer,
                                    LONG* deviceError) override;

    STDMETHODIMP drvInitItemProperties(BYTE* wiasContext, LONG flags,
                                       LONG* deviceError) override;

    STDMETHODIMP drvValidateItemProperties(BYTE* wiasContext, LONG flags, ULONG count,
                                           const PROPSPEC* propSpec,
                                           LONG* deviceError) override;

    STDMETHODIMP drvWriteItemProperties(BYTE* wiasContext, LONG flags,
                                        PMINIDRV_TRANSFER_CONTEXT transfer,
                                        LONG* deviceError) override;

    STDMETHODIMP drvReadItemProperties(BYTE* wiasContext, LONG flags, ULONG count,
                                       const PROPSPEC* propSpec, LONG* deviceError) override;

    STDMETHODIMP drvLockWiaDevice(BYTE* wiasContext, LONG flags, LONG* deviceError) override;
    STDMETHODIMP drvUnLockWiaDevice(BYTE* wiasContext, LONG flags, LONG* deviceError) override;

    STDMETHODIMP drvAnalyzeItem(BYTE* wiasContext, LONG flags, LONG* deviceError) override;

    STDMETHODIMP drvGetDeviceErrorStr(LONG flags, LONG deviceError, LPOLESTR* text,
                                      LONG* newDeviceError) override;

    STDMETHODIMP drvDeviceCommand(BYTE* wiasContext, LONG flags, const GUID* command,
                                  IWiaDrvItem** newItem, LONG* deviceError) override;

    STDMETHODIMP drvGetCapabilities(BYTE* wiasContext, LONG flags, LONG* count,
                                    WIA_DEV_CAP_DRV** capabilities,
                                    LONG* deviceError) override;

    STDMETHODIMP drvDeleteItem(BYTE* wiasContext, LONG flags, LONG* deviceError) override;

    STDMETHODIMP drvFreeDrvItemContext(LONG flags, BYTE* context, LONG* deviceError) override;

    STDMETHODIMP drvGetWiaFormatInfo(BYTE* wiasContext, LONG flags, LONG* count,
                                     WIA_FORMAT_INFO** formats, LONG* deviceError) override;

    STDMETHODIMP drvNotifyPnpEvent(const GUID* event, BSTR deviceId, ULONG reserved) override;

    STDMETHODIMP drvUnInitializeWia(BYTE* wiasContext) override;

private:
    G2710Usd* owner_;
};

}  // namespace g2710::wia

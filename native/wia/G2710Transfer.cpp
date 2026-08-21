// drvAcquireItemData - prenos slike ka aplikaciji.
//
// Ovo je jedini WIA poziv koji zaista pomera skener, i jedini koji traje. Zato
// je odvojen od ostatka minidriver-a.
//
// Tok:
//
//   1. procitaj podesavanja iz WIA skladista i pretvori ih u ScanRequest
//   2. napravi plan; ako ne prolazi, prekini PRE nego sto se ista pomeri
//   3. upali lampu i sacekaj zagrevanje
//   4. ScanSession::begin - od ovog trenutka cip skenira
//   5. red po red: obradi i posalji aplikaciji
//   6. zatvori prolaz, ma kako se zavrsilo
//
// Korak 6 nije formalnost. Sesija koja se ne zatvori ostavlja cip da skenira,
// glava nastavlja da se krece, a sledeci prenos zatice zauzet uredjaj. Zato
// ovde postoji `TransferGuard`, a ne niz `if`-ova sa `goto cleanup`.

#include "G2710MiniDrv.h"

#include "G2710Usd.h"
#include "WiaCapabilities.h"

#include <wiamdef.h>

#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "scan/ScanPlanner.h"
#include "scan/ScanSession.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace g2710::wia {
namespace {

// Koliko se ceka posle paljenja lampe pre nego sto prvi red ima smisla.
//
// Referenca meri stabilnost (Lamp_PWM_CheckStable) umesto da ceka slepo, ali
// to trazi GetOneLineInfo koji dolazi sa orkestracijom kalibracije. Do tada
// slepo cekanje - i to je zapisano u izvestaju, ne precutano.
constexpr std::chrono::milliseconds kLampWarmup{3000};

// Koliko cesto se javlja napredak. Prijavljivati svaki red znaci hiljade
// COM poziva po slici.
constexpr int kProgressSteps = 50;

// Zatvara prolaz ma kako se izaslo iz funkcije.
class TransferGuard {
public:
    explicit TransferGuard(ItemContext* context) : context_(context) {}

    ~TransferGuard() {
        if (context_ != nullptr) {
            (void)context_->endTransfer();
        }
    }

    TransferGuard(const TransferGuard&) = delete;
    TransferGuard& operator=(const TransferGuard&) = delete;

private:
    ItemContext* context_;
};

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

HRESULT toHresult(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Cancelled:     return S_FALSE;
        case ErrorCode::Busy:          return WIA_ERROR_BUSY;
        case ErrorCode::TransportLost: return WIA_ERROR_OFFLINE;
        case ErrorCode::Timeout:       return WIA_ERROR_DEVICE_COMMUNICATION;
        case ErrorCode::NotImplementedIn10:
        case ErrorCode::SafetyViolation:
            return WIA_ERROR_INVALID_COMMAND;
        default:                       return E_FAIL;
    }
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
    if (item == nullptr || item->kind != ItemKind::Flatbed) {
        return E_UNEXPECTED;
    }

    // --- 1. i 2. sta se trazi, i da li se moze -----------------------------

    ItemSettings settings;
    if (const HRESULT result = readSettings(wiasContext, &settings); FAILED(result)) {
        return result;
    }

    const Validation checked = validate(settings);
    if (!checked.accepted()) {
        return WIA_ERROR_INVALID_COMMAND;
    }
    item->settings = checked.corrected;

    auto request = toScanRequest(item->settings);
    if (!request) {
        return WIA_ERROR_INVALID_COMMAND;
    }
    auto planned = scan::planScan(request.value());
    if (!planned) {
        *deviceError = static_cast<LONG>(planned.error().code);
        return toHresult(planned.error().code);
    }

    // --- 3. lampa ---------------------------------------------------------

    item->registers = std::make_unique<rts8822::RegisterFile>(device->transport());
    item->cancellation.reset();
    item->deliveredLines = 0;

    TransferGuard guard(item);

    rts8822::Lamp lamp{*item->registers, device->safety()};
    if (const Status lit = lamp.setLamp(rts8822::LampKind::Flatbed, true); !lit) {
        *deviceError = static_cast<LONG>(lit.error().code);
        return toHresult(lit.error().code);
    }
    if (const Status pwm = lamp.setupPwm(rts8822::LampKind::Flatbed); !pwm) {
        *deviceError = static_cast<LONG>(pwm.error().code);
        return toHresult(pwm.error().code);
    }
    std::this_thread::sleep_for(kLampWarmup);

    // --- 4. pokretanje ----------------------------------------------------

    item->session = std::make_unique<scan::ScanSession>(*item->registers, device->safety(),
                                                        planned.value());
    if (const Status begun = item->session->begin(); !begun) {
        *deviceError = static_cast<LONG>(begun.error().code);
        return toHresult(begun.error().code);
    }

    const std::size_t bytesPerLine = item->session->outputBytesPerLine();
    const int totalLines = item->session->expectedOutputLines();
    if (bytesPerLine == 0 || totalLines <= 0) {
        return E_UNEXPECTED;
    }

    // WIA ocekuje da drajver popuni ova polja pre prvog bajta.
    transfer->lWidthInPixels = item->settings.xExtent;
    transfer->lLines = totalLines;
    transfer->lDepth = item->settings.wiaDepth;
    transfer->lXRes = item->settings.xResolution;
    transfer->lYRes = item->settings.yResolution;
    transfer->cbWidthInBytes = static_cast<LONG>(bytesPerLine);
    transfer->lImageSize = static_cast<LONG>(bytesPerLine * static_cast<std::size_t>(totalLines));
    transfer->lItemSize = transfer->lImageSize;

    // --- 5. redovi --------------------------------------------------------

    IWiaMiniDrvCallBack* callback = transfer->pIWiaMiniDrvCallBack;
    const bool banded = transfer->bTransferDataCB != FALSE && callback != nullptr;

    std::vector<std::uint8_t> line(bytesPerLine, 0);
    LONG offset = 0;
    int lastReported = -1;

    for (;;) {
        auto more = item->session->nextLine(line, item->cancellation);
        if (!more) {
            *deviceError = static_cast<LONG>(more.error().code);
            return toHresult(more.error().code);
        }
        if (!more.value()) {
            break;
        }

        if (banded) {
            // Traka po traka: red se prepisuje u bafer koji je servis dao, pa
            // se javlja da je spreman.
            if (transfer->pTransferBuffer == nullptr ||
                offset + static_cast<LONG>(bytesPerLine) > transfer->lBufferSize) {
                // Bafer je pun; predaj ga pa nastavi od pocetka.
                const HRESULT sent = callback->MiniDrvCallback(
                    IT_MSG_DATA, IT_STATUS_TRANSFER_TO_CLIENT,
                    (item->deliveredLines * 100) / totalLines, 0, offset, transfer, 0);
                if (FAILED(sent)) {
                    return sent;
                }
                if (sent == S_FALSE) {
                    // Aplikacija je odustala. Nije greska.
                    return S_FALSE;
                }
                offset = 0;
            }
            std::memcpy(transfer->pTransferBuffer + offset, line.data(), bytesPerLine);
        } else {
            // Ceo memorijski bafer: red ide na svoje mesto.
            if (transfer->pTransferBuffer == nullptr ||
                offset + static_cast<LONG>(bytesPerLine) > transfer->lImageSize) {
                return E_UNEXPECTED;
            }
            std::memcpy(transfer->pTransferBuffer + offset, line.data(), bytesPerLine);
        }

        offset += static_cast<LONG>(bytesPerLine);
        ++item->deliveredLines;

        // Napredak, ali ne po svakom redu.
        const int step = (item->deliveredLines * kProgressSteps) / totalLines;
        if (callback != nullptr && step != lastReported) {
            lastReported = step;
            const HRESULT status = callback->MiniDrvCallback(
                IT_MSG_STATUS, IT_STATUS_TRANSFER_TO_CLIENT,
                (item->deliveredLines * 100) / totalLines, 0, 0, transfer, 0);
            if (status == S_FALSE) {
                return S_FALSE;
            }
        }
    }

    // Ostatak u baferu.
    if (banded && offset > 0) {
        const HRESULT sent = callback->MiniDrvCallback(IT_MSG_DATA,
                                                       IT_STATUS_TRANSFER_TO_CLIENT, 100, 0,
                                                       offset, transfer, 0);
        if (FAILED(sent)) {
            return sent;
        }
    }

    // --- 6. kraj ----------------------------------------------------------
    //
    // TransferGuard zatvara prolaz. Ovde se samo javlja da je stranica gotova.
    if (callback != nullptr) {
        (void)callback->MiniDrvCallback(IT_MSG_TERMINATION, IT_STATUS_TRANSFER_TO_CLIENT, 100,
                                        0, 0, transfer, 0);
    }
    return S_OK;
}

}  // namespace g2710::wia

// Prenos jedne stranice, bez ijedne WIA osobine.
//
// Tok:
//
//   1. proveri sta je trazeno; ako ne prolazi, prekini PRE nego sto se ista
//      pomeri
//   2. napravi plan
//   3. upali lampu i sacekaj zagrevanje
//   4. ScanSession::begin - od ovog trenutka cip skenira
//   5. red po red: obradi i posalji kroz sink
//   6. zatvori prolaz, ma kako se zavrsilo
//
// Korak 6 nije formalnost. Sesija koja se ne zatvori ostavlja cip da skenira,
// glava nastavlja da se krece, a sledeci prenos zatice zauzet uredjaj. Zato
// ovde postoji `TransferGuard`, a ne niz `if`-ova sa `goto cleanup`.

#include "WiaTransfer.h"

#include <wiamdef.h>

#include "device/G2710Device.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "scan/ScanPlanner.h"
#include "scan/ScanSession.h"

#include <cstring>
#include <thread>
#include <vector>

namespace g2710::wia {
namespace {

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

}  // namespace

// Namerno NIJE `toHresult`: G2710Usd.cpp ima istoimenu funkciju sa DRUGACIJIM
// mapiranjem (STIERR_*, ne WIA_ERROR_*). Ona je u anonimnom imenskom prostoru
// pa se ne sudaraju danas - ali dve funkcije istog imena i istog potpisa koje
// vracaju razlicite vrednosti su zamka koja ceka prvi `#include`.
HRESULT toTransferHresult(ErrorCode code) noexcept {
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

HRESULT runTransfer(G2710Device& device, ItemContext& item,
                    const ItemSettings& requested, ITransferSink& sink,
                    LONG* deviceError, std::chrono::milliseconds warmup) {
    if (deviceError == nullptr) {
        return E_POINTER;
    }
    *deviceError = 0;

    if (item.kind != ItemKind::Flatbed) {
        return E_UNEXPECTED;
    }

    // --- 1. i 2. sta se trazi, i da li se moze -----------------------------

    const Validation checked = validate(requested);
    if (!checked.accepted()) {
        return WIA_ERROR_INVALID_COMMAND;
    }
    item.settings = checked.corrected;

    auto request = toScanRequest(item.settings);
    if (!request) {
        return WIA_ERROR_INVALID_COMMAND;
    }
    auto planned = scan::planScan(request.value());
    if (!planned) {
        *deviceError = static_cast<LONG>(planned.error().code);
        return toTransferHresult(planned.error().code);
    }

    // --- 3. lampa ---------------------------------------------------------

    item.registers = std::make_unique<rts8822::RegisterFile>(device.transport());
    item.cancellation.reset();
    item.deliveredLines = 0;

    TransferGuard guard(&item);

    rts8822::Lamp lamp{*item.registers, device.safety()};
    if (const Status lit = lamp.setLamp(rts8822::LampKind::Flatbed, true); !lit) {
        *deviceError = static_cast<LONG>(lit.error().code);
        return toTransferHresult(lit.error().code);
    }
    if (const Status pwm = lamp.setupPwm(rts8822::LampKind::Flatbed); !pwm) {
        *deviceError = static_cast<LONG>(pwm.error().code);
        return toTransferHresult(pwm.error().code);
    }
    std::this_thread::sleep_for(warmup);

    // --- 4. pokretanje ----------------------------------------------------

    item.session = std::make_unique<scan::ScanSession>(*item.registers, device.safety(),
                                                       planned.value());
    if (const Status begun = item.session->begin(); !begun) {
        *deviceError = static_cast<LONG>(begun.error().code);
        return toTransferHresult(begun.error().code);
    }

    const std::size_t bytesPerLine = item.session->outputBytesPerLine();
    const int totalLines = item.session->expectedOutputLines();
    if (bytesPerLine == 0 || totalLines <= 0) {
        return E_UNEXPECTED;
    }

    TransferGeometry geometry;
    geometry.widthInPixels = item.settings.xExtent;
    geometry.lines = totalLines;
    geometry.wiaDepth = item.settings.wiaDepth;
    geometry.xResolution = item.settings.xResolution;
    geometry.yResolution = item.settings.yResolution;
    geometry.bytesPerLine = bytesPerLine;

    if (const HRESULT ready = sink.begin(geometry); ready != S_OK) {
        return ready;
    }

    // --- 5. redovi --------------------------------------------------------

    std::vector<std::uint8_t> line(bytesPerLine, 0);

    // Pocinje na 0, a NE na -1.
    //
    // Sa -1 bi se korak 0 javio kao promena, pa bi poziva bilo kProgressSteps
    // PLUS JEDAN - konstanta bi lagala o sopstvenoj kadenci. Taj prvi poziv i
    // ne nosi nista: napredak od nula procenata je vec receno kroz sink.begin().
    int lastReported = 0;

    for (;;) {
        auto more = item.session->nextLine(line, item.cancellation);
        if (!more) {
            *deviceError = static_cast<LONG>(more.error().code);
            return toTransferHresult(more.error().code);
        }
        if (!more.value()) {
            break;
        }

        if (const HRESULT written = sink.writeLine(line); written != S_OK) {
            return written;
        }
        ++item.deliveredLines;

        // Napredak, ali ne po svakom redu.
        const int step = (item.deliveredLines * kProgressSteps) / totalLines;
        if (step != lastReported) {
            lastReported = step;
            const int percent = (item.deliveredLines * 100) / totalLines;
            if (const HRESULT reported = sink.progress(percent); reported != S_OK) {
                return reported;
            }
        }
    }

    // --- 6. kraj ----------------------------------------------------------
    //
    // TransferGuard zatvara prolaz. Sink predaje ostatak bafera.
    return sink.finish();
}

// --- sink nad baferima WIA servisa ------------------------------------------

HRESULT WiaCallbackSink::begin(const TransferGeometry& geometry) {
    if (transfer_ == nullptr) {
        return E_POINTER;
    }

    // WIA ocekuje da drajver popuni ova polja pre prvog bajta.
    transfer_->lWidthInPixels = geometry.widthInPixels;
    transfer_->lLines = geometry.lines;
    transfer_->lDepth = geometry.wiaDepth;
    transfer_->lXRes = geometry.xResolution;
    transfer_->lYRes = geometry.yResolution;
    transfer_->cbWidthInBytes = static_cast<LONG>(geometry.bytesPerLine);
    transfer_->lImageSize =
        static_cast<LONG>(geometry.bytesPerLine * static_cast<std::size_t>(geometry.lines));
    transfer_->lItemSize = transfer_->lImageSize;

    banded_ = transfer_->bTransferDataCB != FALSE &&
              transfer_->pIWiaMiniDrvCallBack != nullptr;
    offset_ = 0;
    delivered_ = 0;
    percent_ = 0;
    return S_OK;
}

HRESULT WiaCallbackSink::writeLine(std::span<const std::uint8_t> line) {
    if (transfer_ == nullptr || transfer_->pTransferBuffer == nullptr) {
        return E_UNEXPECTED;
    }
    const LONG size = static_cast<LONG>(line.size());

    if (banded_) {
        // Traka po traka: bafer se predaje cim sledeci red ne bi stao.
        if (offset_ + size > transfer_->lBufferSize) {
            if (const HRESULT sent = flush(percent_); sent != S_OK) {
                return sent;
            }
        }
        // Red koji ne staje ni u prazan bafer je nesaglasnost geometrije, ne
        // stanje iz koga se oporavlja - bez ove provere bi se pisalo van
        // bafera koji je dao servis.
        if (offset_ + size > transfer_->lBufferSize) {
            return E_UNEXPECTED;
        }
    } else if (offset_ + size > transfer_->lImageSize) {
        return E_UNEXPECTED;
    }

    std::memcpy(transfer_->pTransferBuffer + offset_, line.data(), line.size());
    offset_ += size;
    if (!banded_) {
        delivered_ += size;
    }
    return S_OK;
}

HRESULT WiaCallbackSink::progress(int percent) {
    percent_ = percent;
    if (transfer_ == nullptr || transfer_->pIWiaMiniDrvCallBack == nullptr) {
        return S_OK;
    }
    const HRESULT status = transfer_->pIWiaMiniDrvCallBack->MiniDrvCallback(
        IT_MSG_STATUS, IT_STATUS_TRANSFER_TO_CLIENT, percent, 0, 0, transfer_, 0);

    // Samo S_FALSE znaci odustajanje. Ostale vrednosti se ne tumace kao greska:
    // napredak je obavestenje, i pad obavestenja ne sme oboriti prenos.
    return status == S_FALSE ? S_FALSE : S_OK;
}

HRESULT WiaCallbackSink::finish() {
    if (transfer_ == nullptr) {
        return E_POINTER;
    }
    if (banded_ && offset_ > 0) {
        if (const HRESULT sent = flush(100); sent != S_OK) {
            return sent;
        }
    }
    if (transfer_->pIWiaMiniDrvCallBack != nullptr) {
        (void)transfer_->pIWiaMiniDrvCallBack->MiniDrvCallback(
            IT_MSG_TERMINATION, IT_STATUS_TRANSFER_TO_CLIENT, 100, 0, 0, transfer_, 0);
    }
    return S_OK;
}

HRESULT WiaCallbackSink::flush(int percent) {
    const HRESULT sent = transfer_->pIWiaMiniDrvCallBack->MiniDrvCallback(
        IT_MSG_DATA, IT_STATUS_TRANSFER_TO_CLIENT, percent, 0, offset_, transfer_, 0);
    if (FAILED(sent)) {
        return sent;
    }
    if (sent == S_FALSE) {
        // Aplikacija je odustala. Nije greska.
        return S_FALSE;
    }
    delivered_ += offset_;
    offset_ = 0;
    return S_OK;
}

}  // namespace g2710::wia

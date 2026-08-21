#include "G2710Usd.h"

#include "G2710Profile.generated.h"
#include "G2710Wia.h"
#include "WiaCapabilities.h"
#include "WiaEvents.h"

#include <stierr.h>

#include <algorithm>
#include <cstring>

namespace g2710::wia {
namespace {

// Kapacitet koji STI servis cita. Vrednosti prate INF (Capabilities 0x33).
constexpr DWORD kGenericCapabilities =
    STI_GENCAP_NOTIFICATIONS | STI_GENCAP_POLLING_NEEDED | STI_GENCAP_WIA;

// Duzina bafera za GetMyDevicePortName. STI koristi imena oblika \\.\UsbscanN,
// pa je MAX_PATH vise nego dovoljno.
constexpr DWORD kPortNameLength = MAX_PATH;

HRESULT toHresult(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:
            return S_OK;
        case ErrorCode::DeviceNotFound:
            return STIERR_OBJECTNOTFOUND;
        case ErrorCode::Busy:
            return STIERR_DEVICE_LOCKED;
        case ErrorCode::Timeout:
            return STIERR_READONLY;
        case ErrorCode::Cancelled:
            return STIERR_BADDRIVER;
        case ErrorCode::TransportLost:
        case ErrorCode::NotOpen:
            return STIERR_NOT_INITIALIZED;
        case ErrorCode::SafetyViolation:
        case ErrorCode::NotImplementedIn10:
            return STIERR_UNSUPPORTED;
        case ErrorCode::InvalidArgument:
            return STIERR_INVALID_PARAM;
        default:
            return E_FAIL;
    }
}

}  // namespace

G2710Usd::G2710Usd() = default;

G2710Usd::~G2710Usd() = default;

HRESULT G2710Usd::recordError(const Error& error) {
    lastError_ = error.code;
    lastWin32_ = error.win32;
    return toHresult(error.code);
}

HRESULT G2710Usd::recordWin32(DWORD code, HRESULT result) {
    lastError_ = ErrorCode::DeviceError;
    lastWin32_ = code;
    return result;
}

// --- IUnknown --------------------------------------------------------------

STDMETHODIMP G2710Usd::QueryInterface(REFIID riid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;

    if (riid == IID_IUnknown || riid == IID_IStiUSD) {
        *object = static_cast<IStiUSD*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IWiaMiniDrv) {
        // Minidriver ne broji sopstvene reference - vlasnik je ovaj objekat.
        // Zato se ovde broji USD, a Release na minidriver-u vraca ovde.
        *object = static_cast<IWiaMiniDrv*>(&miniDriver_);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) G2710Usd::AddRef() {
    return references_.fetch_add(1, std::memory_order_relaxed) + 1;
}

STDMETHODIMP_(ULONG) G2710Usd::Release() {
    const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

// --- IStiUSD ---------------------------------------------------------------

STDMETHODIMP G2710Usd::Initialize(PSTIDEVICECONTROL control, DWORD /*stiVersion*/,
                                  HKEY /*parametersKey*/) {
    if (control == nullptr) {
        return STIERR_INVALID_PARAM;
    }
    std::lock_guard<std::recursive_mutex> guard(lock_);

    // Redosled je iz MASTER plana i nije proizvoljan: rezim otvaranja pre
    // imena porta, jer rezim odlucuje da li smemo da trazimo podatke.
    if (const HRESULT result = control->GetMyDeviceOpenMode(&openMode_); FAILED(result)) {
        return result;
    }

    wchar_t port[kPortNameLength] = {};
    if (const HRESULT result = control->GetMyDevicePortName(port, kPortNameLength);
        FAILED(result)) {
        return result;
    }
    portName_.assign(port);
    if (portName_.empty()) {
        return STIERR_OBJECTNOTFOUND;
    }

    // Od ovog trenutka transport je nas. FILE_FLAG_OVERLAPPED postavlja
    // UsbScanTransport::FromPortName - bez njega cancel ne radi.
    DeviceOptions options;
    options.clientName = "WIA";

    // WIA sme sve sto build dozvoljava; plafon je i dalje BuildSafetyCeiling.
    options.safety = SafetyGate{SafetyLevel::FullScan};

    auto opened = G2710Device::open(DeviceRef::portName(portName_), options);
    if (!opened) {
        return recordError(opened.error());
    }
    device_ = std::move(opened.value());

    // Identitet se proverava ODMAH. Ime porta koje STI daje je deljeno; ako
    // iza njega stoji tudji uredjaj, vendor komande sa G2710 semantikom ne
    // smeju krenuti.
    if (const Status identified = device_->identify(); !identified) {
        device_.reset();
        return recordError(identified.error());
    }
    return S_OK;
}

STDMETHODIMP G2710Usd::GetCapabilities(PSTI_USD_CAPS capabilities) {
    if (capabilities == nullptr) {
        return STIERR_INVALID_PARAM;
    }
    *capabilities = STI_USD_CAPS{};
    capabilities->dwVersion = STI_VERSION_REAL;
    capabilities->dwGenericCaps = kGenericCapabilities;
    return S_OK;
}

STDMETHODIMP G2710Usd::GetStatus(PSTI_DEVICE_STATUS status) {
    if (status == nullptr) {
        return STIERR_INVALID_PARAM;
    }
    std::lock_guard<std::recursive_mutex> guard(lock_);

    if ((status->StatusMask & STI_DEVSTATUS_ONLINE_STATE) != 0) {
        // "Online" znaci da uredjaj odgovara, ne da je kabl u njemu. Jedini
        // pouzdan odgovor je citanje registra.
        status->dwOnlineState = 0;
        if (device_ != nullptr) {
            if (const auto home = device_->isHeadAtHome(); home) {
                status->dwOnlineState = STI_ONLINESTATE_OPERATIONAL;
            } else {
                recordError(home.error());
            }
        }
    }

    if ((status->StatusMask & STI_DEVSTATUS_EVENTS_STATE) != 0) {
        status->dwEventHandlingState = pendingButtonMask_ != 0
                                           ? STI_EVENTHANDLING_PENDING
                                           : STI_EVENTHANDLING_ENABLED;
    }
    return S_OK;
}

STDMETHODIMP G2710Usd::DeviceReset() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (device_ == nullptr) {
        return STIERR_NOT_INITIALIZED;
    }

    // Posle reseta pozicija glave je NEPOZNATA. Ne pretvaramo se da znamo gde
    // je - HOME postaje obavezan pre bilo kakvog kretanja. Vidi docs/SAFETY.md.
    device_->cancel();
    device_->headPosition().invalidate();

    if (const Status recovered = device_->recoverFromTransportLoss(); !recovered) {
        return recordError(recovered.error());
    }
    return S_OK;
}

STDMETHODIMP G2710Usd::Diagnostic(LPSTI_DIAG diagnostic) {
    if (diagnostic == nullptr) {
        return STIERR_INVALID_PARAM;
    }
    std::lock_guard<std::recursive_mutex> guard(lock_);

    diagnostic->dwStatusMask = 0;
    diagnostic->sErrorInfo.dwGenericError = NOERROR;
    diagnostic->sErrorInfo.dwVendorError = 0;
    diagnostic->sErrorInfo.szExtendedErrorText[0] = L'\0';

    if (device_ == nullptr) {
        diagnostic->sErrorInfo.dwGenericError = STI_NOTCONNECTED;
        return STIERR_NOT_INITIALIZED;
    }

    // Dijagnostika je citanje, nikada kretanje: identitet i home senzor.
    const auto home = device_->isHeadAtHome();
    if (!home) {
        diagnostic->sErrorInfo.dwGenericError = STI_NOTCONNECTED;
        diagnostic->sErrorInfo.dwVendorError = static_cast<DWORD>(home.error().code);
        return recordError(home.error());
    }
    return S_OK;
}

STDMETHODIMP G2710Usd::Escape(STI_RAW_CONTROL_CODE /*function*/, LPVOID /*inData*/,
                              DWORD /*inSize*/, LPVOID /*outData*/, DWORD /*outSize*/,
                              LPDWORD actual) {
    // Escape je vendor prolaz za komande koje STI ne poznaje. Nemamo nijednu
    // koju bismo izlozili aplikacijama, a otvarati registarski pristup kroz
    // javni interfejs znacilo bi zaobici SafetyGate.
    if (actual != nullptr) {
        *actual = 0;
    }
    return STIERR_UNSUPPORTED;
}

STDMETHODIMP G2710Usd::GetLastError(LPDWORD lastError) {
    if (lastError == nullptr) {
        return STIERR_INVALID_PARAM;
    }
    *lastError = lastWin32_;
    return S_OK;
}

STDMETHODIMP G2710Usd::LockDevice() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (device_ == nullptr) {
        return STIERR_NOT_INITIALIZED;
    }

    // Prvi LockDevice zauzima ekskluzivnu sesiju kroz DeviceArbiter; ugnjezdeni
    // se samo broje. STI ume da zakljuca vise puta iz iste niti.
    if (lockDepth_ == 0) {
        if (const Status taken = device_->begin(); !taken) {
            return recordError(taken.error());
        }
    }
    ++lockDepth_;
    return S_OK;
}

STDMETHODIMP G2710Usd::UnLockDevice() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (device_ == nullptr) {
        return STIERR_NOT_INITIALIZED;
    }
    if (lockDepth_ == 0) {
        return STIERR_NEEDS_LOCK;
    }

    if (--lockDepth_ == 0) {
        if (const Status released = device_->end(); !released) {
            return recordError(released.error());
        }
    }
    return S_OK;
}

// --- sirovi pristup --------------------------------------------------------
//
// STI ovo nudi aplikacijama kao prolaz do uredjaja. Kod nas je ZATVOREN.
//
// Sirovi upis bi zaobisao SafetyGate, MotionGuard i DeviceArbiter odjednom -
// dakle sve tri zastite koje postoje bas zato sto se uredjaj testira na
// daljinu. Aplikacija koja hoce podatke ide kroz WIA transfer.

STDMETHODIMP G2710Usd::RawReadData(LPVOID, LPDWORD size, LPOVERLAPPED) {
    if (size != nullptr) {
        *size = 0;
    }
    return STIERR_UNSUPPORTED;
}

STDMETHODIMP G2710Usd::RawWriteData(LPVOID, DWORD, LPOVERLAPPED) {
    return STIERR_UNSUPPORTED;
}

STDMETHODIMP G2710Usd::RawReadCommand(LPVOID, LPDWORD size, LPOVERLAPPED) {
    if (size != nullptr) {
        *size = 0;
    }
    return STIERR_UNSUPPORTED;
}

STDMETHODIMP G2710Usd::RawWriteCommand(LPVOID, DWORD, LPOVERLAPPED) {
    return STIERR_UNSUPPORTED;
}

// --- dugmad ----------------------------------------------------------------

STDMETHODIMP G2710Usd::SetNotificationHandle(HANDLE event) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    notificationEvent_ = event;

    // Prazan handle znaci da servis vise ne slusa; sve zapamceno se odbacuje
    // da se sledeci put ne bi prijavilo staro dugme.
    if (event == nullptr) {
        pendingButtonMask_ = 0;
    }
    return S_OK;
}

STDMETHODIMP G2710Usd::GetNotificationData(LPSTINOTIFY notify) {
    if (notify == nullptr) {
        return STIERR_INVALID_PARAM;
    }
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (device_ == nullptr) {
        return STIERR_NOT_INITIALIZED;
    }

    *notify = STINOTIFY{};
    notify->dwSize = sizeof(STINOTIFY);

    // Maska dolazi sa interrupt pipe-a. Mapiranje maska -> dugme je iz profila
    // (kButtons), a POTVRDJUJE ga tek H10 - do tada je pretpostavka, i tako je
    // i zapisana u INF-u.
    auto pressed = device_->transport().waitEvent();
    if (!pressed) {
        return recordError(pressed.error());
    }

    pendingButtonMask_ = pressed.value();
    if (pendingButtonMask_ == 0) {
        return S_FALSE;
    }

    // STINOTIFY nosi GUID, ne broj. Maska se zato prevodi; nepoznata maska se
    // NE prijavljuje kao dogadjaj - bolje tisina nego pogresno dugme.
    const ButtonEvent* event = eventForMask(pendingButtonMask_);
    if (event == nullptr) {
        pendingButtonMask_ = 0;
        return S_FALSE;
    }

    notify->guidNotificationCode = *event->eventGuid;

    // Sirova maska ide u vendor polje, da H10 izvestaj moze reci sta je
    // uredjaj STVARNO poslao, a ne samo kako smo to protumacili.
    static_assert(sizeof(notify->abNotificationData) >= sizeof(std::uint32_t),
                  "vendor polje mora primiti masku");
    const std::uint32_t mask = pendingButtonMask_;
    std::memcpy(notify->abNotificationData, &mask, sizeof(mask));
    return S_OK;
}

STDMETHODIMP G2710Usd::GetLastErrorInfo(STI_ERROR_INFO* info) {
    if (info == nullptr) {
        return STIERR_INVALID_PARAM;
    }
    *info = STI_ERROR_INFO{};
    info->dwGenericError = lastWin32_;
    info->dwVendorError = static_cast<DWORD>(lastError_);

    const wchar_t* text = L"";
    switch (lastError_) {
        case ErrorCode::Busy:          text = L"Uredjaj koristi drugi program."; break;
        case ErrorCode::TransportLost: text = L"Veza sa skenerom je prekinuta."; break;
        case ErrorCode::DeviceNotFound:text = L"Uredjaj na ovom portu nije G2710."; break;
        case ErrorCode::Timeout:       text = L"Skener nije odgovorio na vreme."; break;
        default:                       text = L""; break;
    }

    // Polje je fiksnih 255 znakova (Sti.h:460); duzina se racuna iz njega, ne
    // iz konstante koja u zaglavlju ne postoji.
    constexpr std::size_t kCapacity =
        sizeof(info->szExtendedErrorText) / sizeof(info->szExtendedErrorText[0]);
    const std::size_t length = std::min<std::size_t>(wcslen(text), kCapacity - 1);
    wmemcpy(info->szExtendedErrorText, text, length);
    info->szExtendedErrorText[length] = L'\0';
    return S_OK;
}

}  // namespace g2710::wia

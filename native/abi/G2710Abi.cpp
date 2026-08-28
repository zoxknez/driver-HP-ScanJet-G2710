// Implementacija C granice.
//
// Pravila koja ovaj fajl sprovodi, i koja se ne smeju zaobici ni u jednoj
// funkciji:
//
//   1. Nijedan izuzetak ne izlazi napolje. Svaka ulazna tacka je obavijena
//      guard-om koji hvata sve i pretvara u G2710_STATUS_INTERNAL. .NET runtime
//      ne moze uhvatiti C++ izuzetak - proces bi se srusio bez traga.
//
//   2. Nijedan pokazivac se ne dereferencira bez provere. Sa druge strane
//      granice je marshalling koji moze poslati NULL i kada "ne moze".
//
//   3. Poruka greske ide UZ HANDLE. Dva skenera u istoj aplikaciji ne smeju
//      gaziti jedan drugom poruku.
//
// Licenca: GPL-2.0-or-later.

#include "g2710_abi.h"

#include "../sim/SimTransport.h"
#include "device/G2710Device.h"
#include "device/SafetyLevel.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "scan/Capabilities.h"
#include "scan/CapabilityReport.h"
#include "scan/ScanPlanner.h"
#include "scan/ScanSession.h"
#include "transport/ITransportProvider.h"
#include "transport/TraceRecorder.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using namespace g2710;

namespace {

// --- prevodi ----------------------------------------------------------------

g2710_status toAbi(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:                 return G2710_STATUS_OK;
        case ErrorCode::NotOpen:            return G2710_STATUS_NOT_OPEN;
        case ErrorCode::Timeout:            return G2710_STATUS_TIMEOUT;
        case ErrorCode::ShortTransfer:      return G2710_STATUS_SHORT_TRANSFER;
        case ErrorCode::Stalled:            return G2710_STATUS_STALLED;
        case ErrorCode::Cancelled:          return G2710_STATUS_CANCELLED;
        case ErrorCode::TransportLost:      return G2710_STATUS_TRANSPORT_LOST;
        case ErrorCode::DeviceNotFound:     return G2710_STATUS_DEVICE_NOT_FOUND;
        case ErrorCode::DeviceError:        return G2710_STATUS_DEVICE_ERROR;
        case ErrorCode::Busy:               return G2710_STATUS_BUSY;
        case ErrorCode::SafetyViolation:    return G2710_STATUS_SAFETY_VIOLATION;
        case ErrorCode::NotImplementedIn10: return G2710_STATUS_NOT_IMPLEMENTED;
        case ErrorCode::InvalidArgument:    return G2710_STATUS_INVALID_ARGUMENT;
        case ErrorCode::InvalidState:       return G2710_STATUS_INVALID_STATE;
        case ErrorCode::Internal:           return G2710_STATUS_INTERNAL;
    }
    return G2710_STATUS_INTERNAL;
}

g2710_device_state toAbi(DeviceState state) noexcept {
    switch (state) {
        case DeviceState::Disconnected:     return G2710_STATE_DISCONNECTED;
        case DeviceState::Opened:           return G2710_STATE_OPENED;
        case DeviceState::Identified:       return G2710_STATE_IDENTIFIED;
        case DeviceState::Idle:             return G2710_STATE_IDLE;
        case DeviceState::WarmingUp:        return G2710_STATE_WARMING_UP;
        case DeviceState::Homing:           return G2710_STATE_HOMING;
        case DeviceState::Calibrating:      return G2710_STATE_CALIBRATING;
        case DeviceState::Scanning:         return G2710_STATE_SCANNING;
        case DeviceState::Cancelling:       return G2710_STATE_CANCELLING;
        case DeviceState::TransportLost:    return G2710_STATE_TRANSPORT_LOST;
        case DeviceState::Faulted:          return G2710_STATE_FAULTED;
        case DeviceState::EmergencyStopped: return G2710_STATE_EMERGENCY_STOPPED;
    }
    return G2710_STATE_FAULTED;
}

image::ColorMode toCore(g2710_color_mode mode) noexcept {
    switch (mode) {
        case G2710_GRAY:    return image::ColorMode::Gray;
        case G2710_LINEART: return image::ColorMode::Lineart;
        case G2710_COLOR:
        default:            return image::ColorMode::Color;
    }
}

SafetyLevel toCore(int32_t level) noexcept {
    switch (level) {
        case 1:  return SafetyLevel::ReadOnly;
        case 2:  return SafetyLevel::Lamp;
        case 3:  return SafetyLevel::Motor;
        case 4:  return SafetyLevel::Acquire;
        default: return SafetyLevel::FullScan;
    }
}

int32_t toAbi(SafetyLevel level) noexcept {
    switch (level) {
        case SafetyLevel::ReadOnly: return 1;
        case SafetyLevel::Lamp:     return 2;
        case SafetyLevel::Motor:    return 3;
        case SafetyLevel::Acquire:  return 4;
        case SafetyLevel::FullScan: return 5;
    }
    return 1;
}

// --- kopiranje niski --------------------------------------------------------

// Isti ugovor svuda: upisi najvise `capacity` bajtova UKLJUCUJUCI zavrsnu
// nulu, vrati koliko je bajtova potrebno BEZ zavrsne nule.
//
// Pozivalac tako moze pozvati sa capacity 0 da sazna velicinu, pa alocirati.
int32_t copyOut(const std::string& text, char* buffer, int32_t capacity) noexcept {
    const int32_t needed = static_cast<int32_t>(text.size());
    if (buffer == nullptr || capacity <= 0) {
        return needed;
    }
    const int32_t copied = std::min<int32_t>(needed, capacity - 1);
    if (copied > 0) {
        std::memcpy(buffer, text.data(), static_cast<std::size_t>(copied));
    }
    buffer[copied] = '\0';
    return needed;
}

}  // namespace

// --- handle -----------------------------------------------------------------

struct g2710_device {
    // REDOSLED POLJA JESTE REDOSLED UNISTAVANJA, UNAZAD.
    //
    // Clanovi se ruse obrnuto od deklaracije, a lanac vlasnistva je:
    //
    //     session -> registers -> core -> recorder -> owned
    //
    // Sesija cita registre, registri drze referencu na transport uredjaja,
    // uredjaj poseduje snimac, a snimac drzi referencu na pravi transport koji
    // je ovde. Obrnut raspored bi znacio citanje oslobodjene memorije pri
    // svakom zatvaranju - greska koja se ne vidi dok ne srusi tudju aplikaciju.
    //
    // Prvo deklarisano umire poslednje.
    std::unique_ptr<ITransport> owned;
    std::unique_ptr<G2710Device> core;
    std::unique_ptr<rts8822::RegisterFile> registers;
    std::unique_ptr<scan::ScanSession> session;

    // Samo posmatrac; vlasnik je `core`.
    TraceRecorder* recorder = nullptr;

    std::size_t bytesPerLine = 0;

    g2710_log_fn log = nullptr;
    void* logUser = nullptr;

    std::string lastError;
    std::uint32_t lastWin32 = 0;

    void note(const Error& error) {
        lastWin32 = error.win32;
        lastError = toString(error.code);
        if (error.context != nullptr && error.context[0] != '\0') {
            lastError += " (";
            lastError += error.context;
            lastError += ")";
        }
        if (error.win32 != 0) {
            char tail[32];
            std::snprintf(tail, sizeof(tail), " [win32 %u]", error.win32);
            lastError += tail;
        }
        emit(3, lastError.c_str());
    }

    void noteText(const char* text) {
        lastWin32 = 0;
        lastError = text != nullptr ? text : "";
        emit(3, lastError.c_str());
    }

    void clearError() {
        lastError.clear();
        lastWin32 = 0;
    }

    void emit(int32_t level, const char* message) const {
        if (log != nullptr && message != nullptr) {
            log(level, message, logUser);
        }
    }

    // Zatvori prolaz ako je otvoren. Bezbedno i kada nije.
    //
    // Otkazivanje se PRVO ponistava. Zaustavljanje cipa je i samo transfer, pa
    // ga lepljivi cancel odbija - a prolaz koji se ne zatvori ostavlja glavu
    // da se krece posle "Prekini".
    void closeSession() noexcept {
        if (core != nullptr) {
            core->endCancellation();
        }
        if (session != nullptr) {
            (void)session->finish();
            session.reset();
        }
        registers.reset();
        bytesPerLine = 0;
    }
};

namespace {

// Greska poslednjeg g2710_open-a na ovoj niti. Handle tada jos ne postoji, pa
// poruka nema gde drugde. Namerno thread_local, ne globalno: dva otvaranja u
// paraleli ne smeju gaziti jedno drugom poruku.
thread_local std::string g_openError;

// Svaka ulazna tacka prolazi kroz ovo.
//
// C++ izuzetak koji predje granicu .NET runtime ne moze uhvatiti - proces se
// rusi bez ijedne poruke. Zato ovde nema izuzetka koji prolazi, ukljucujuci i
// one koje bacaju std::string i std::vector pri nedostatku memorije.
template <typename Body>
g2710_status guard(g2710_device* device, Body&& body) noexcept {
    try {
        return body();
    } catch (const std::exception& exception) {
        if (device != nullptr) {
            device->noteText(exception.what());
        }
        return G2710_STATUS_INTERNAL;
    } catch (...) {
        if (device != nullptr) {
            device->noteText("nepoznat izuzetak");
        }
        return G2710_STATUS_INTERNAL;
    }
}

// Prijavi napredak i vrati true ako se nastavlja.
bool report(g2710_progress_fn progress, void* user, int percent) noexcept {
    if (progress == nullptr) {
        return true;
    }
    // Callback koji baci izuzetak je greska POZIVAOCA; ovde se ne moze
    // razlikovati od pada, pa se tumaci kao zahtev za prekid.
    try {
        return progress(percent, user) != 0;
    } catch (...) {
        return false;
    }
}

}  // namespace

// --- osnovno ----------------------------------------------------------------

extern "C" {

const char* G2710_CALL g2710_status_name(g2710_status status) {
    switch (status) {
        case G2710_STATUS_OK:               return "ok";
        case G2710_STATUS_NOT_OPEN:         return "not-open";
        case G2710_STATUS_TIMEOUT:          return "timeout";
        case G2710_STATUS_SHORT_TRANSFER:   return "short-transfer";
        case G2710_STATUS_STALLED:          return "stalled";
        case G2710_STATUS_CANCELLED:        return "cancelled";
        case G2710_STATUS_TRANSPORT_LOST:   return "transport-lost";
        case G2710_STATUS_DEVICE_NOT_FOUND: return "device-not-found";
        case G2710_STATUS_DEVICE_ERROR:     return "device-error";
        case G2710_STATUS_BUSY:             return "busy";
        case G2710_STATUS_SAFETY_VIOLATION: return "safety-violation";
        case G2710_STATUS_NOT_IMPLEMENTED:  return "not-implemented";
        case G2710_STATUS_INVALID_ARGUMENT: return "invalid-argument";
        case G2710_STATUS_INVALID_STATE:    return "invalid-state";
        case G2710_STATUS_INTERNAL:         return "internal";
    }
    return "unknown";
}

uint32_t G2710_CALL g2710_abi_version(void) {
    return (static_cast<uint32_t>(G2710_ABI_VERSION_MAJOR) << 16) |
           static_cast<uint32_t>(G2710_ABI_VERSION_MINOR);
}

int32_t G2710_CALL g2710_build_safety_ceiling(void) {
    return toAbi(kBuildSafetyCeiling);
}

int32_t G2710_CALL g2710_motor_path_compiled(void) {
    return G2710_MOTOR_PATH_COMPILED ? 1 : 0;
}

void G2710_CALL g2710_open_options_init(g2710_open_options* options) {
    if (options == nullptr) {
        return;
    }
    *options = g2710_open_options{};
    options->size = sizeof(g2710_open_options);
    options->transport = G2710_TRANSPORT_USBSCAN;
    options->requested_safety_level = 1;
    options->client_name = nullptr;
    options->acquire_timeout_ms = 0;
    options->record_trace = 0;
}

// --- otvaranje --------------------------------------------------------------

g2710_status G2710_CALL g2710_open(const g2710_open_options* options,
                                   g2710_device** out_device) {
    if (out_device == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    *out_device = nullptr;

    if (options == nullptr) {
        g_openError = "options je NULL";
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    // `size` mora pokrivati bar polja koja citamo. Manja struktura znaci da je
    // pozivalac gradjen uz stariji ABI i da novija polja nisu popunjena; nula
    // znaci da nije ni pozvao g2710_open_options_init.
    if (options->size < offsetof(g2710_open_options, record_trace)) {
        g_openError = "options.size je premali - pozovi g2710_open_options_init";
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    if (options->requested_safety_level < 1 || options->requested_safety_level > 5) {
        g_openError = "requested_safety_level mora biti 1..5";
        return G2710_STATUS_INVALID_ARGUMENT;
    }

    return guard(nullptr, [&]() -> g2710_status {
        auto handle = std::make_unique<g2710_device>();

        DeviceOptions deviceOptions;
        deviceOptions.safety = SafetyGate{toCore(options->requested_safety_level)};
        deviceOptions.clientName =
            options->client_name != nullptr ? options->client_name : "g2710";
        if (options->acquire_timeout_ms != 0) {
            deviceOptions.acquireTimeout =
                std::chrono::milliseconds(options->acquire_timeout_ms);
        }

        // Simulator se bira preko provajdera, ali samo za OVO otvaranje -
        // zamena je globalna, pa se vraca pre nego sto funkcija izadje.
        std::unique_ptr<TransportProvider::ScopedTestProvider> simulated;
        if (options->transport == G2710_TRANSPORT_SIM) {
            simulated = std::make_unique<TransportProvider::ScopedTestProvider>(
                std::make_unique<sim::SimTransportProvider>());
        }

        auto transport = TransportProvider::create(DeviceRef::defaultUsbScan());
        if (!transport) {
            g_openError = toString(transport.error().code);
            return toAbi(transport.error().code);
        }

        std::unique_ptr<ITransport> chain = std::move(transport).value();

        // Snimac se umece IZMEDJU uredjaja i transporta. Posle otvaranja tamo
        // vise nema mesta - zato je ovo jedina prilika.
        bool wantTrace = false;
        if (options->size >= offsetof(g2710_open_options, record_trace) +
                                 sizeof(options->record_trace)) {
            wantTrace = options->record_trace != 0;
        }
        if (wantTrace) {
            auto recorder = std::make_unique<TraceRecorder>(*chain);
            handle->recorder = recorder.get();

            // Snimac ne poseduje transport ispod sebe, pa oba moraju preziveti
            // uredjaj. Pravi transport se seli u handle, a uredjaj dobija
            // snimac.
            handle->owned = std::move(chain);
            chain = std::move(recorder);
        }

        auto device = G2710Device::openWith(std::move(chain), std::move(deviceOptions));
        if (!device) {
            g_openError = toString(device.error().code);
            return toAbi(device.error().code);
        }
        handle->core = std::move(device).value();

        g_openError.clear();
        *out_device = handle.release();
        return G2710_STATUS_OK;
    });
}

void G2710_CALL g2710_close(g2710_device* device) {
    if (device == nullptr) {
        return;
    }
    // Handle koji se zatvori usred skeniranja ne sme ostaviti cip da skenira.
    try {
        device->closeSession();
    } catch (...) {
        // Nema kome da se prijavi - handle upravo nestaje.
    }
    delete device;
}

// --- greske -----------------------------------------------------------------

int32_t G2710_CALL g2710_last_error(const g2710_device* device, char* buffer,
                                    int32_t capacity) {
    // NULL handle je dozvoljen: posle neuspelog g2710_open handle-a jos nema.
    const std::string& text = device != nullptr ? device->lastError : g_openError;
    return copyOut(text, buffer, capacity);
}

uint32_t G2710_CALL g2710_last_win32(const g2710_device* device) {
    return device != nullptr ? device->lastWin32 : 0;
}

// --- identitet i stanje -----------------------------------------------------

g2710_status G2710_CALL g2710_identify(g2710_device* device) {
    if (device == nullptr || device->core == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        device->clearError();
        if (const Status result = device->core->identify(); !result) {
            device->note(result.error());
            return toAbi(result.error().code);
        }
        return G2710_STATUS_OK;
    });
}

g2710_status G2710_CALL g2710_begin(g2710_device* device) {
    if (device == nullptr || device->core == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        device->clearError();
        if (const Status result = device->core->begin(); !result) {
            device->note(result.error());
            return toAbi(result.error().code);
        }
        return G2710_STATUS_OK;
    });
}

g2710_status G2710_CALL g2710_end(g2710_device* device) {
    if (device == nullptr || device->core == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        device->clearError();
        device->closeSession();
        if (const Status result = device->core->end(); !result) {
            device->note(result.error());
            return toAbi(result.error().code);
        }
        return G2710_STATUS_OK;
    });
}

g2710_device_state G2710_CALL g2710_state(const g2710_device* device) {
    if (device == nullptr || device->core == nullptr) {
        return G2710_STATE_DISCONNECTED;
    }
    return toAbi(device->core->state());
}

int32_t G2710_CALL g2710_effective_safety_level(const g2710_device* device) {
    if (device == nullptr || device->core == nullptr) {
        return 0;
    }
    return toAbi(device->core->safety().effective());
}

int32_t G2710_CALL g2710_current_owner(const g2710_device* device, char* buffer,
                                       int32_t capacity) {
    if (device == nullptr || device->core == nullptr) {
        return copyOut(std::string{}, buffer, capacity);
    }
    try {
        return copyOut(device->core->currentOwner(), buffer, capacity);
    } catch (...) {
        return copyOut(std::string{}, buffer, capacity);
    }
}

void G2710_CALL g2710_set_log(g2710_device* device, g2710_log_fn log, void* user) {
    if (device == nullptr) {
        return;
    }
    device->log = log;
    device->logUser = user;
}

// --- operacije koje traju ---------------------------------------------------

g2710_status G2710_CALL g2710_warmup(g2710_device* device, uint32_t warmup_ms,
                                     g2710_progress_fn progress, void* user) {
    if (device == nullptr || device->core == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        device->clearError();

        rts8822::RegisterFile registers{device->core->transport()};
        rts8822::Lamp lamp{registers, device->core->safety()};

        if (const Status lit = lamp.setLamp(rts8822::LampKind::Flatbed, true); !lit) {
            device->note(lit.error());
            return toAbi(lit.error().code);
        }
        if (const Status pwm = lamp.setupPwm(rts8822::LampKind::Flatbed); !pwm) {
            device->note(pwm.error());
            return toAbi(pwm.error().code);
        }

        // Cekanje se deli na korake da bi napredak i otkazivanje bili mogucni.
        // Jedan sleep od tri sekunde je zamrznuta aplikacija bez dugmeta za
        // prekid.
        const uint32_t total = warmup_ms != 0 ? warmup_ms : 3000;
        constexpr int kSteps = 20;
        for (int step = 0; step < kSteps; ++step) {
            if (device->core->cancellation().isCancelled()) {
                device->noteText("otkazano tokom zagrevanja");
                return G2710_STATUS_CANCELLED;
            }
            if (!report(progress, user, (step * 100) / kSteps)) {
                device->core->cancel();
                device->noteText("prekinuto iz callback-a");
                return G2710_STATUS_CANCELLED;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(total / kSteps));
        }
        (void)report(progress, user, 100);
        return G2710_STATUS_OK;
    });
}

g2710_status G2710_CALL g2710_home(g2710_device* device, g2710_progress_fn progress,
                                   void* user) {
    if (device == nullptr || device->core == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        device->clearError();

        // Nivo se proverava PRE nego sto se kaze da nije implementirano.
        // Obrnut redosled bi paketu sa plafonom 1 rekao "nije implementirano",
        // a tacan odgovor je "ovaj paket to ne sme".
        if (const Status allowed =
                device->core->safety().require(SafetyLevel::Motor, "home");
            !allowed) {
            device->note(allowed.error());
            return G2710_STATUS_SAFETY_VIOLATION;
        }

        // Kretanje glave trazi port Head_Relocate i Head_ParkHome, kojih jos
        // nema. Izmisljati ih znacilo bi pomerati tudji motor po pretpostavci -
        // upravo ono sto ceo projekat izbegava. Isti odgovor daje i provera H4.
        device->noteText("ceka port Head_Relocate; do tada se glava ne pomera");
        (void)report(progress, user, 0);
        return G2710_STATUS_NOT_IMPLEMENTED;
    });
}

void G2710_CALL g2710_cancel(g2710_device* device) {
    // JEDINA funkcija koja sme iz druge niti - i zato ne dira nista osim
    // token-a, koji je za to i napravljen.
    if (device != nullptr && device->core != nullptr) {
        device->core->cancel();
    }
}

// --- skeniranje -------------------------------------------------------------

void G2710_CALL g2710_scan_request_init(g2710_scan_request* request) {
    if (request == nullptr) {
        return;
    }
    *request = g2710_scan_request{};
    request->size = sizeof(g2710_scan_request);
    request->resolution = 300;
    request->color_mode = G2710_COLOR;
    request->bits_per_channel = 8;
    request->gamma = 1.0;
}

namespace {

// Zajednicko za plan i za pocetak prolaza: zahtev -> ScanRequest.
g2710_status buildRequest(const g2710_scan_request* request, scan::ScanRequest* out,
                          const char** why) noexcept {
    if (request == nullptr) {
        *why = "request je NULL";
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    if (request->size < sizeof(g2710_scan_request)) {
        *why = "request.size je premali - pozovi g2710_scan_request_init";
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    if (request->bits_per_channel != 8 && request->bits_per_channel != 16) {
        *why = "bits_per_channel mora biti 8 ili 16";
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    if (request->width < 0 || request->height < 0 || request->left < 0 ||
        request->top < 0) {
        *why = "oblast ne sme imati negativne vrednosti";
        return G2710_STATUS_INVALID_ARGUMENT;
    }

    out->resolution = request->resolution;
    out->colorMode = toCore(request->color_mode);
    out->depth = request->bits_per_channel;
    out->source = scan::ScanSource::Flatbed;
    out->region.left = request->left;
    out->region.top = request->top;
    out->region.width = request->width;
    out->region.height = request->height;
    out->allowUnqualified = request->allow_unqualified != 0;
    return G2710_STATUS_OK;
}

void fillInfo(const scan::ScanRequest& request, const scan::ScanPlan& plan,
              const scan::ScanSession* session, g2710_scan_info* info) noexcept {
    *info = g2710_scan_info{};
    info->size = sizeof(g2710_scan_info);
    info->width_pixels = plan.requestedRegion.width;
    info->lines = plan.requestedRegion.height;

    // LineGeometry nosi samo duzinu reda i kod dubine, pa se broj kanala i
    // bita po kanalu izvodi iz rezima - kao i svuda u jezgru.
    info->channels = request.colorMode == image::ColorMode::Color ? 3 : 1;
    info->bits_per_channel =
        request.colorMode == image::ColorMode::Lineart ? 1 : request.depth;
    info->bytes_per_line = static_cast<uint32_t>(plan.outputLine.bytesPerLine);
    info->native_resolution = plan.nativeResolution;

    if (session != nullptr) {
        info->lines = session->expectedOutputLines();
        info->bytes_per_line = static_cast<uint32_t>(session->outputBytesPerLine());
        info->shading_applied = session->shadingApplied() ? 1 : 0;
    }
}

}  // namespace

g2710_status G2710_CALL g2710_plan_scan(const g2710_device* device,
                                        const g2710_scan_request* request,
                                        g2710_scan_info* out_info) {
    if (out_info == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    // `device` sme biti NULL: racun je statican i radi kada skenera nema. Zato
    // se poruka ne moze uvek zapisati uz handle, i guard dobija ono sto ima.
    auto* mutableDevice = const_cast<g2710_device*>(device);

    return guard(mutableDevice, [&]() -> g2710_status {
        scan::ScanRequest core;
        const char* why = "";
        if (const g2710_status bad = buildRequest(request, &core, &why);
            bad != G2710_STATUS_OK) {
            if (mutableDevice != nullptr) {
                mutableDevice->noteText(why);
            }
            return bad;
        }

        auto planned = scan::planScan(core);
        if (!planned) {
            if (mutableDevice != nullptr) {
                mutableDevice->note(planned.error());
            }
            return toAbi(planned.error().code);
        }
        fillInfo(core, planned.value(), nullptr, out_info);
        return G2710_STATUS_OK;
    });
}

g2710_status G2710_CALL g2710_scan_begin(g2710_device* device,
                                         const g2710_scan_request* request,
                                         g2710_scan_info* out_info) {
    if (device == nullptr || device->core == nullptr || out_info == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        device->clearError();

        // Prolaz koji je vec u toku se ne sme tiho zameniti - to bi ostavilo
        // cip da skenira dok se pocinje novi.
        if (device->session != nullptr) {
            device->noteText("prolaz je vec u toku; pozovi g2710_scan_end");
            return G2710_STATUS_INVALID_STATE;
        }

        scan::ScanRequest core;
        const char* why = "";
        if (const g2710_status bad = buildRequest(request, &core, &why);
            bad != G2710_STATUS_OK) {
            device->noteText(why);
            return bad;
        }

        auto planned = scan::planScan(core);
        if (!planned) {
            device->note(planned.error());
            return toAbi(planned.error().code);
        }

        scan::ScanOptions scanOptions;
        if (request->gamma > 0.0 && request->gamma != 1.0) {
            scanOptions.gamma = image::makeGammaTable(request->gamma);
        }

        device->registers =
            std::make_unique<rts8822::RegisterFile>(device->core->transport());
        device->core->cancellation().reset();

        auto session = std::make_unique<scan::ScanSession>(
            *device->registers, device->core->safety(), planned.value(),
            std::move(scanOptions));

        if (const Status begun = session->begin(); !begun) {
            device->note(begun.error());
            device->registers.reset();
            return toAbi(begun.error().code);
        }

        device->bytesPerLine = session->outputBytesPerLine();
        fillInfo(core, planned.value(), session.get(), out_info);
        device->session = std::move(session);
        return G2710_STATUS_OK;
    });
}

g2710_status G2710_CALL g2710_scan_read_line(g2710_device* device, uint8_t* buffer,
                                             uint32_t capacity, int32_t* out_done) {
    if (device == nullptr || buffer == nullptr || out_done == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    *out_done = 0;

    return guard(device, [&]() -> g2710_status {
        if (device->session == nullptr) {
            device->noteText("nema prolaza u toku");
            return G2710_STATUS_INVALID_STATE;
        }
        // Premali bafer je greska, ne tiho skracivanje: skracena slika izgleda
        // kao pokvaren skener.
        if (capacity < device->bytesPerLine) {
            device->noteText("bafer je manji od bytes_per_line");
            return G2710_STATUS_INVALID_ARGUMENT;
        }

        auto more = device->session->nextLine(
            std::span<std::uint8_t>(buffer, device->bytesPerLine),
            device->core->cancellation());

        if (!more) {
            device->note(more.error());
            return toAbi(more.error().code);
        }
        *out_done = more.value() ? 0 : 1;
        return G2710_STATUS_OK;
    });
}

g2710_status G2710_CALL g2710_scan_end(g2710_device* device) {
    if (device == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        if (device->session == nullptr) {
            // Zatvaranje necega sto nije otvoreno nije greska - pozivalac to
            // radi u `finally` bloku i ne zna uvek da li je pocelo.
            return G2710_STATUS_OK;
        }

        // Vidi closeSession: bez ovoga otkazan prolaz ne moze da se zatvori.
        device->core->endCancellation();

        const Status closed = device->session->finish();
        device->session.reset();
        device->registers.reset();
        device->bytesPerLine = 0;

        if (!closed) {
            device->note(closed.error());
            return toAbi(closed.error().code);
        }
        return G2710_STATUS_OK;
    });
}

// --- mogucnosti -------------------------------------------------------------

int32_t G2710_CALL g2710_capabilities(char* buffer, int32_t capacity) {
    // Bez handle-a i bez uredjaja: racun je statican. Izuzetak se ne sme
    // pustiti napolje ni ovde, pa se prazan izlaz vraca kao nula.
    try {
        return copyOut(scan::capabilitiesJson(), buffer, capacity);
    } catch (...) {
        return 0;
    }
}

// --- trag -------------------------------------------------------------------

g2710_status G2710_CALL g2710_write_trace(g2710_device* device, const char* path) {
    if (device == nullptr || path == nullptr) {
        return G2710_STATUS_INVALID_ARGUMENT;
    }
    return guard(device, [&]() -> g2710_status {
        if (device->recorder == nullptr) {
            // Prazan fajl bi izgledao kao da se nista nije desilo.
            device->noteText("snimanje nije bilo ukljuceno u g2710_open_options");
            return G2710_STATUS_INVALID_STATE;
        }
        const std::string text = device->recorder->format(true);

        // ofstream, ne fopen: MSVC pod /W4 /WX odbija fopen kao nesiguran, a
        // fopen_s nije prenosiv. Trag je tekst, pa tok radi isti posao.
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            device->noteText("fajl za trag se ne moze otvoriti");
            return G2710_STATUS_INVALID_ARGUMENT;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) {
            device->noteText("trag nije upisan do kraja");
            return G2710_STATUS_INTERNAL;
        }
        return G2710_STATUS_OK;
    });
}

int32_t G2710_CALL g2710_trace_count(const g2710_device* device) {
    if (device == nullptr || device->recorder == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(device->recorder->entries().size());
}

}  // extern "C"

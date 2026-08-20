// Greske koje G2710.Core propagira. Namerno mala i kopirljiva struktura:
// nosi se kroz Result<T> po vrednosti, bez alokacije.

#pragma once

#include <cstdint>

namespace g2710 {

enum class ErrorCode : int {
    Ok = 0,

    // --- transport -----------------------------------------------------
    NotOpen,          // transport nije otvoren
    Timeout,          // istekao rok transfera
    ShortTransfer,    // preneto manje bajtova nego trazeno
    Stalled,          // endpoint STALL, potreban ResetPipe
    Cancelled,        // otkazano preko CancellationToken ili Cancel()

    // TransportLost je razlicito od Disconnected namerno: znaci da je veza
    // nestala USRED operacije, pa se pozicija glave proglasava nepoznatom i
    // HOME postaje obavezan posle reconnect-a. Vidi docs/SAFETY.md.
    TransportLost,

    DeviceNotFound,
    DeviceError,      // uredjaj je odgovorio, ali neispravno

    // --- arbitraza -----------------------------------------------------
    Busy,             // uredjaj drzi drugi klijent (vidi DeviceArbiter)

    // --- politika ------------------------------------------------------
    SafetyViolation,  // operacija iznad efektivnog SafetyLevel-a
    NotImplementedIn10,

    // --- opste ---------------------------------------------------------
    InvalidArgument,
    InvalidState,
    Internal,
};

const char* toString(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Internal;

    // Win32 GetLastError() ako greska potice iz DeviceIoControl / ReadFile /
    // CreateFile; 0 inace.
    std::uint32_t win32 = 0;

    // Staticki literal koji opisuje mesto nastanka. Nikada vlasnistvo nad
    // memorijom - Error mora ostati trivijalno kopirljiv.
    const char* context = "";
};

}  // namespace g2710

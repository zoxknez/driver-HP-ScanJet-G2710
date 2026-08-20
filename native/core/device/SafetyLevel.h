// Mehanicka bezbednost udaljenog hardvera.
//
// Skener nije kod nas tokom razvoja. Zato postoje DVA nivoa, ne jedan:
//
//   BuildSafetyCeiling   nepromenljiv, ugradjen u binarni fajl u build-u
//   RequestedSafetyLevel sta pozivalac trazi (CLI, config, API)
//
//   effective = min(ceiling, requested)
//
// Plafon se NIKADA ne moze podici u runtime-u. Qualification build sa
// plafonom 1 odbija sve pozive nivoa 2-5 bez obzira na `--safety-level 5`,
// i motor path u njemu nije ni kompajliran (vidi G2710_MOTOR_PATH_COMPILED).

#pragma once

#include "../util/Result.h"

#ifndef G2710_BUILD_SAFETY_CEILING
#error "G2710_BUILD_SAFETY_CEILING mora biti definisan u build sistemu"
#endif

#if G2710_BUILD_SAFETY_CEILING < 1 || G2710_BUILD_SAFETY_CEILING > 5
#error "G2710_BUILD_SAFETY_CEILING mora biti 1-5"
#endif

namespace g2710 {

enum class SafetyLevel : int {
    // Enumeracija, USB descriptor, read-only registri. Nista se ne pomera,
    // nista se ne pali, nijedan konfiguracioni upis.
    ReadOnly = 1,

    // + safe konfiguracioni write i kontrola lampi.
    Lamp = 2,

    // + motor i HOME. Od ovog nivoa uredjaj se fizicki krece.
    Motor = 3,

    // + CCD akvizicija.
    Acquire = 4,

    // + pun scan.
    FullScan = 5,
};

inline constexpr SafetyLevel kBuildSafetyCeiling =
    static_cast<SafetyLevel>(G2710_BUILD_SAFETY_CEILING);

// Kompajlerski prekidac, ne runtime provera. U build-u sa plafonom < 3 motorni
// kod se ne prevodi, pa ga nema ni u binarnom fajlu koji saljemo prijatelju.
#if G2710_BUILD_SAFETY_CEILING >= 3
#define G2710_MOTOR_PATH_COMPILED 1
#else
#define G2710_MOTOR_PATH_COMPILED 0
#endif

constexpr int toInt(SafetyLevel level) noexcept {
    return static_cast<int>(level);
}

constexpr SafetyLevel minLevel(SafetyLevel a, SafetyLevel b) noexcept {
    return toInt(a) < toInt(b) ? a : b;
}

const char* toString(SafetyLevel level) noexcept;

// Cuva efektivni nivo i odbija svaku operaciju iznad njega.
class SafetyGate {
public:
    // Zahtev iznad plafona se TIHO SPUSTA na plafon, ne odbija - pozivalac
    // moze legitimno traziti 5 na build-u koji dozvoljava 2. Ono sto se odbija
    // je pokusaj IZVRSENJA operacije iznad efektivnog nivoa.
    explicit constexpr SafetyGate(SafetyLevel requested) noexcept
        : requested_(requested), effective_(minLevel(kBuildSafetyCeiling, requested)) {}

    constexpr SafetyGate() noexcept : SafetyGate(kBuildSafetyCeiling) {}

    constexpr SafetyLevel requested() const noexcept { return requested_; }
    constexpr SafetyLevel effective() const noexcept { return effective_; }
    constexpr SafetyLevel ceiling() const noexcept { return kBuildSafetyCeiling; }

    // true ako je zahtev spusten zato sto build to ne dozvoljava. Korisno da
    // dijagnostika kaze "trazili ste 5, ovaj paket dozvoljava 1" umesto da
    // tiho radi nesto drugo.
    constexpr bool wasClamped() const noexcept {
        return toInt(requested_) > toInt(effective_);
    }

    constexpr bool allows(SafetyLevel needed) const noexcept {
        return toInt(needed) <= toInt(effective_);
    }

    // Vraca SafetyViolation ako je operacija iznad efektivnog nivoa.
    Status require(SafetyLevel needed, const char* operation) const noexcept;

private:
    SafetyLevel requested_;
    SafetyLevel effective_;
};

}  // namespace g2710

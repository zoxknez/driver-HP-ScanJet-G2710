// Kalibracioni parametri iz profila.
//
// rts8822.c:11448 Calib_LoadConfig - most izmedju 80 vrednosti ekstraktovanih
// u kCalibReflective i algoritama koji ih koriste.
//
// Referenca vise polja skalira sa 0.01 pri ucitavanju (OffsetTargetMax,
// OffsetTargetMin, OffsetBoundaryRatio*, OffsetAvgRatio*), isto kao sto
// Lamp_PWM_CheckStable skalira `diff`. Ta konvencija se lako previdi, pa je
// ovde eksplicitna: `scaled()` govori sta je procenat, `raw()` sta nije.

#pragma once

#include "../util/Result.h"

#include "G2710Profile.generated.h"

#include <array>
#include <cstddef>

namespace g2710::calib {

// Redosled kanala je CL_RED = 0, CL_GREEN = 1, CL_BLUE = 2.
inline constexpr std::size_t kChannels = 3;

// Koji set parametara vazi za dati izvor.
enum class CalibrationSection {
    Reflective,   // flatbed - jedini aktivan u 1.0
    Transparent,  // TMA, 1.1
    Negative,     // TMA negativ, 1.1
};

const char* toString(CalibrationSection section) noexcept;

struct CalibrationConfig {
    // Pozicije kalibracionih traka, u pikselima.
    int whiteStripX = 0;
    int whiteStripY = 0;
    int blackStripX = 0;
    int blackStripY = 0;

    // Ciljne vrednosti po kanalu.
    std::array<int, kChannels> whiteReference{};
    std::array<int, kChannels> blackReference{};

    // Pocetni ADC offseti, po kanalu i po parnosti piksela.
    std::array<int, kChannels> offsetEven1{};
    std::array<int, kChannels> offsetEven2{};
    std::array<int, kChannels> offsetOdd1{};
    std::array<int, kChannels> offsetOdd2{};

    int referenceBitDepth = 8;
    int offsetHeight = 10;
    int offsetNSigma = 2;

    // --- polja skalirana sa 0.01 ---------------------------------------
    double offsetTargetMax = 0.50;
    double offsetTargetMin = 0.02;
    double offsetBoundaryRatio1 = 1.00;
    double offsetBoundaryRatio2 = 1.00;
    double offsetAvgRatio1 = 1.00;
    double offsetAvgRatio2 = 1.00;

    // Koliko redova se usrednjava pri merenju.
    int gainHeight = 10;
    int gainTargetFactor = 80;

    // Da li je pojedini korak kalibracije uopste ukljucen.
    bool calibrateOffset1 = false;
    bool calibrateOffset2 = false;
    bool calibrateGain1 = false;
    bool calibrateGain2 = false;
    bool calibratePaGain = false;
    bool blackShadingEnabled = false;
    bool whiteShadingEnabled = false;

    int blackShadingHeight = 20;
    int whiteShadingHeight = 24;
};

// Ucitaj parametre za dati izvor iz generisanog profila.
//
// Vraca InvalidArgument za sekcije koje 1.0 ne aktivira, umesto da tiho vrati
// flatbed vrednosti - TMA parametri postoje u profilu ali nisu kvalifikovani.
Result<CalibrationConfig> loadCalibrationConfig(CalibrationSection section);

// Procitaj jednu vrednost iz tabele parametara. Vraca `fallback` ako opcija
// nije prisutna - referenca radi isto (get_value sa podrazumevanom vrednoscu).
int calibrationValue(CalibrationSection section, profile::CalibOption option,
                     int fallback) noexcept;

}  // namespace g2710::calib

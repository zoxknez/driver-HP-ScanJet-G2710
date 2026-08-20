// Kontrola lampe.
//
// rts8822.c:10738 Lamp_Status_Set, :2480 Lamp_PWM_Setup, :11138
// Lamp_PWM_CheckStable.
//
// Dve nesaglasnosti u referenci ticu se ISKLJUCIVO TMA putanje i zapisane su u
// docs/REFERENCE-DEFECTS.md. Flatbed je saglasan u oba smera, pa je obim 1.0
// netaknut.

#pragma once

#include "../device/SafetyLevel.h"
#include "../util/Clock.h"
#include "RegisterFile.h"
#include "Registers.h"

#include <chrono>
#include <cstdint>
#include <functional>

namespace g2710::rts8822 {

// FLB_LAMP = 1, TMA_LAMP = 2 iz hp3900_types.c.
enum class LampKind : int {
    Flatbed = 1,
    Tma = 2,
};

const char* toString(LampKind kind) noexcept;

// hp3800_checkstable: diff 100, interval 200 ms, ukupno 10 s.
//
// Warmup NE ceka fiksno vreme - meri. Zaustavlja se kada se dva uzastopna
// ocitavanja razlikuju za manje od `diff`, ili kada istekne `totalTime`.
struct StabilityCriterion {
    double diff = 100.0;
    std::chrono::milliseconds interval{200};
    std::chrono::milliseconds totalTime{10000};
};

// rts8822.c:14021 - overdrive_flb i overdrive_ta su oba 10000 ms.
//
// To je slepo cekanje PRE nego sto merenje uopste pocne.
inline constexpr std::chrono::milliseconds kOverdriveFlatbed{10000};
inline constexpr std::chrono::milliseconds kOverdriveTma{10000};

// rts8822.c:10793 - referenca spava 200 ms izmedju dva upisa u Lamp_Status_Set.
inline constexpr std::chrono::milliseconds kLampSwitchSettle{200};

struct WarmupResult {
    bool stabilised = false;         // merenje je konvergiralo pre isteka
    int samples = 0;                 // koliko je ocitavanja uzeto
    std::chrono::milliseconds elapsed{0};
    double lastLevel = 0.0;
};

class Lamp {
public:
    Lamp(RegisterFile& registers, SafetyGate gate, Clock& clock = systemClock())
        : registers_(registers), gate_(gate), clock_(clock) {}

    // rts8822.c:10738 Lamp_Status_Set, grana za BL-03A.
    //
    // NAPOMENA: referenca u Lamp_Warmup poziva ovo sa turn_on = FALSE da bi
    // UPALILA TMA lampu (vidi D1 u docs/REFERENCE-DEFECTS.md). Mi to ne
    // reprodukujemo - `on` se postuje doslovno, jer je suprotno ocigledno
    // nenamerno.
    Status setLamp(LampKind kind, bool on);

    // rts8822.c:2480 Lamp_PWM_Setup - postavi duty cycle iz profila ako se
    // razlikuje od trenutnog.
    Status setupPwm(LampKind kind);

    // rts8822.c:11138 Lamp_PWM_CheckStable.
    //
    // `readLevel` vraca trenutni maksimalni nivo po kanalima; u radu ga daje
    // GetOneLineInfo, u testu simulator. Guard izmedju ocitavanja je sat, pa
    // je test trenutan.
    Result<WarmupResult> waitUntilStable(const std::function<Result<double>()>& readLevel,
                                         const StabilityCriterion& criterion = {});

    // Ceo postupak: upali lampu, sacekaj overdrive, pa meri do stabilnosti.
    Result<WarmupResult> warmUp(LampKind kind,
                                const std::function<Result<double>()>& readLevel,
                                const StabilityCriterion& criterion = {});

private:
    RegisterFile& registers_;
    SafetyGate gate_;
    Clock& clock_;
};

}  // namespace g2710::rts8822

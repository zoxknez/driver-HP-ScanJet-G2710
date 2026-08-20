// Motorni parametri.
//
// CEO OVAJ MODUL NESTAJE iz build-a sa BuildSafetyCeiling < 3. To nije runtime
// provera nego odsustvo koda: paket koji prvi ide prijatelju fizicki ne sadrzi
// put do motora. Vidi docs/SAFETY.md.
//
// Ovde je SAMO konfiguracija struje motora. Samo kretanje - MotionGuard,
// ExpectedSteps, MaximumSteps, Deadline, EmergencyStop - dolazi u G2710-4 i
// nijedna motorna operacija se ne sme dodati bez njega.

#pragma once

#include "../device/SafetyLevel.h"
#include "RegisterFile.h"

#include <cstdint>

namespace g2710::rts8822 {

// Tip USB veze utice na izbor motornih parametara (rts8822.c:5142).
enum class UsbSpeed {
    Usb11,
    Usb20,
};

// Vrednosti odgovaraju ST_NORMAL / ST_TA / ST_NEG iz reference.
enum class ScanType {
    Normal,
    Tma,
    Negative,
};

// rts8822.c:5137 Motor_GetFromResolution
//
// Vraca vrednost koja ide u Motor_Change, NE indeks u motormove tabelu -
// ta dva se lako pomesaju jer oba izgledaju kao mali indeksi.
//
//   USB 2.0, flatbed        : 0 ako je rezolucija >= 1200, inace 3
//   USB 2.0, TMA / negativ  : 0 ako je rezolucija >= 600,  inace 3
//   USB 1.1, bilo koji izvor: 0 ako je rezolucija >= 600,  inace 3
constexpr int motorCurrentForResolution(int resolution, ScanType scanType,
                                        UsbSpeed usb) noexcept {
    if (usb == UsbSpeed::Usb11) {
        return resolution >= 600 ? 0 : 3;
    }
    if (scanType != ScanType::Normal) {
        return resolution >= 600 ? 0 : 3;
    }
    return resolution >= 1200 ? 0 : 3;
}

// Bitovi struje motora u kLampMode, maska 0x30. rts8822.c:4206
//
// Referenca radi `value--` nad SANE_Byte, pa vrednost 0 postaje 255 i ne
// pogadja nijedan case - bitovi ostaju obrisani. To NIJE previd u prepisu nego
// nacin na koji se bira najveca struja, i zato je ovde eksplicitno.
constexpr std::uint8_t motorCurrentBits(int value) noexcept {
    switch (value - 1) {
        case 2:  return 0x30;  // --11----
        case 1:  return 0x20;  // --10----
        case 0:  return 0x10;  // --01----
        default: return 0x00;  // ukljucuje prelivanje kada je value == 0
    }
}

#if G2710_MOTOR_PATH_COMPILED

class Motor {
public:
    Motor(RegisterFile& registers, SafetyGate gate) : registers_(registers), gate_(gate) {}

    // rts8822.c:4197 Motor_Change
    //
    // Cita REC sa kLampMode, cisti masku 0x30, upisuje nove bitove - i onda
    // upisuje nazad samo NIZI BAJT. Asimetrija citanja i upisa je u referenci
    // i prenosi se doslovno.
    Status applyMotorCurrent(int value);

    // Ista stvar, ali vrednost se izvodi iz rezolucije.
    Status applyMotorCurrentForResolution(int resolution, ScanType scanType, UsbSpeed usb) {
        return applyMotorCurrent(motorCurrentForResolution(resolution, scanType, usb));
    }

private:
    RegisterFile& registers_;
    SafetyGate gate_;
};

#endif  // G2710_MOTOR_PATH_COMPILED

}  // namespace g2710::rts8822

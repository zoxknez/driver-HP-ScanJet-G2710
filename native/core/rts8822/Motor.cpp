#include "Motor.h"

#include "Registers.h"

#if G2710_MOTOR_PATH_COMPILED

namespace g2710::rts8822 {

Status Motor::applyMotorCurrent(int value) {
    // Nivo 3 iako se nista ne pomera: ovo menja pogonske parametre motora, a
    // prema udaljenom hardveru smo namerno konzervativni.
    if (const Status allowed = gate_.require(SafetyLevel::Motor, "motor.applyMotorCurrent");
        !allowed) {
        return allowed;
    }

    auto current = registers_.readWord(reg::kLampMode);
    if (!current) {
        return current.error();
    }

    // data &= 0xcf  -> ocisti masku struje, ostavi ostalo
    const auto cleared = static_cast<std::uint16_t>(current.value() & 0xFFCF);
    const auto updated = static_cast<std::uint16_t>(cleared | motorCurrentBits(value));

    // Referenca cita REC a upisuje BAJT na istu adresu (Read_Word pa
    // Write_Byte sa _B0). Visi bajt se ne vraca na cip.
    return registers_.writeByte(reg::kLampMode, static_cast<std::uint8_t>(updated & 0xFF));
}

}  // namespace g2710::rts8822

#endif  // G2710_MOTOR_PATH_COMPILED

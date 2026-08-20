#include "Gpio.h"

#include "Registers.h"

#include <thread>

namespace g2710::rts8822 {
namespace {

// rts8822.c:1090-1092 - vrednosti se upisuju direktno, bez maskiranja.
constexpr std::uint16_t kSensorProbeGpio0 = 0x13FF;
constexpr std::uint16_t kSensorProbeGpio1 = 0xFCF0;

// rts8822.c:1101 - _B1(c) & 1, dakle bit 8 procitane reci.
constexpr std::uint16_t kSensorProbeCisBit = 0x0100;

}  // namespace

const char* toString(SensorType type) noexcept {
    switch (type) {
        case SensorType::Cis: return "CIS";
        case SensorType::Ccd: return "CCD";
    }
    return "?";
}

Status Gpio::setLock(bool enabled) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "gpio.setLock"); !allowed) {
        return allowed;
    }

    auto current = registers_.readByte(reg::kLock);
    if (!current) {
        return current.error();
    }

    const auto updated = enabled
        ? static_cast<std::uint8_t>(current.value() | reg::kLockBit)
        : static_cast<std::uint8_t>(current.value() & static_cast<std::uint8_t>(~reg::kLockBit));

    return registers_.writeByte(reg::kLock, updated);
}

Status Gpio::setE950Mode(bool enabled) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "gpio.setE950Mode");
        !allowed) {
        return allowed;
    }

    auto current = registers_.readWord(reg::kGpio0);
    if (!current) {
        return current.error();
    }

    // data = (mode == 0) ? data & 0xffbf : data | 0x40
    const auto updated = enabled
        ? static_cast<std::uint16_t>(current.value() | reg::kGpio0ModeBit)
        : static_cast<std::uint16_t>(current.value() & 0xFFBF);

    return registers_.writeWord(reg::kGpio0, updated);
}

Result<SensorType> Gpio::detectSensorType(std::chrono::milliseconds settleTime) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "gpio.detectSensorType");
        !allowed) {
        return allowed.error();
    }

    // Sacuvaj zateceno stanje pre nego sto bilo sta promenimo.
    auto saved0 = registers_.readWord(reg::kGpio0);
    if (!saved0) {
        return saved0.error();
    }
    auto saved1 = registers_.readWord(reg::kGpio1);
    if (!saved1) {
        return saved1.error();
    }

    // Vraca zateceno stanje bez obzira na ishod - detekcija ne sme ostaviti
    // GPIO u probnoj konfiguraciji ako nesto pukne u sredini.
    const auto restore = [&]() {
        (void)registers_.writeWord(reg::kGpio0, saved0.value());
        (void)registers_.writeWord(reg::kGpio1, saved1.value());
    };

    if (const Status s = registers_.writeWord(reg::kGpio0, kSensorProbeGpio0); !s) {
        restore();
        return s.error();
    }
    if (const Status s = registers_.writeWord(reg::kGpio1, kSensorProbeGpio1); !s) {
        restore();
        return s.error();
    }

    if (settleTime.count() > 0) {
        std::this_thread::sleep_for(settleTime);
    }

    auto probe = registers_.readWord(reg::kGpioSense);
    restore();

    if (!probe) {
        return probe.error();
    }

    return (probe.value() & kSensorProbeCisBit) == 0 ? SensorType::Ccd : SensorType::Cis;
}

}  // namespace g2710::rts8822

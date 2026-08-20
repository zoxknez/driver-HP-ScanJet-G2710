#include "Rts8822.h"

#include <array>

namespace g2710::rts8822 {

Result<bool> Rts8822::isExecuting() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "isExecuting"); !allowed) {
        return allowed.error();
    }
    auto value = registers_.readByte(reg::kControl);
    if (!value) {
        return value.error();
    }
    return (value.value() & reg::kControlExecutingBit) != 0;
}

Result<bool> Rts8822::isHeadAtHome() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "isHeadAtHome"); !allowed) {
        return allowed.error();
    }
    auto value = registers_.readByte(reg::kHeadSensor);
    if (!value) {
        return value.error();
    }
    return (value.value() & reg::kHeadAtHomeBit) != 0;
}

Result<LampStatus> Rts8822::lampStatus() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "lampStatus"); !allowed) {
        return allowed.error();
    }

    auto status = registers_.readByte(reg::kLampStatus);
    if (!status) {
        return status.error();
    }
    auto mode = registers_.readWord(reg::kLampMode);
    if (!mode) {
        return mode.error();
    }

    // Grana za RTS8822BL-03A. Ostali chipsetovi u referenci koriste drugaciju
    // logiku; G2710 je uvek BL-03A, pa druga grana ovde ne postoji.
    LampStatus result;
    result.flatbedOn = (status.value() & reg::kLampStatusFlbBit) != 0;
    result.tmaOn = (status.value() & reg::kLampStatusTmaBit) != 0 &&
                   (mode.value() & reg::kLampModeTmaSelectBit) != 0;
    return result;
}

Result<std::uint8_t> Rts8822::lampPwmDutyCycle() {
    if (const Status allowed = gate_.require(SafetyLevel::ReadOnly, "lampPwmDutyCycle");
        !allowed) {
        return allowed.error();
    }
    auto value = registers_.readByte(reg::kLampPwm);
    if (!value) {
        return value.error();
    }
    return static_cast<std::uint8_t>(value.value() & reg::kLampPwmDutyMask);
}

Status Rts8822::warmReset() {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "warmReset"); !allowed) {
        return allowed;
    }

    auto current = registers_.readByte(reg::kControl);
    if (!current) {
        return current.error();
    }

    // Referenca: data = (data & 0x3f) | 0x40  -> upis  -> data &= 0xbf -> upis.
    // Maska 0x3F cisti i bit 7 (executing) i bit 6, pa se ne prepisuje
    // executing stanje nazad na cip.
    const auto asserted =
        static_cast<std::uint8_t>((current.value() & 0x3F) | reg::kControlWarmResetBit);
    if (const Status s = registers_.writeByte(reg::kControl, asserted); !s) {
        return s;
    }

    const auto released =
        static_cast<std::uint8_t>(asserted & static_cast<std::uint8_t>(~reg::kControlWarmResetBit));
    return registers_.writeByte(reg::kControl, released);
}

Status Rts8822::chipsetReset() {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "chipsetReset"); !allowed) {
        return allowed;
    }
    // Prazan payload; komanda je u wIndex-u. rts8822.c:4222
    return registers_.transport().controlOut(0x0000, Command::ChipsetReset, {});
}

Status Rts8822::enableCcdChannels(std::uint8_t channels) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "enableCcdChannels");
        !allowed) {
        return allowed;
    }

    std::array<std::byte, reg::kCcdChannelsLength> block{};
    if (const Status s = registers_.readBuffer(reg::kCcdChannels, block); !s) {
        return s;
    }

    auto low = static_cast<std::uint8_t>(block[0]);
    auto high = static_cast<std::uint8_t>(block[3]);

    low = bitsetValue(low, reg::kCcdChannelsLowMask, channels);
    high = bitsetValue(high, reg::kCcdChannelsHighMask,
                       static_cast<std::uint8_t>(channels >> 3));

    block[0] = static_cast<std::byte>(low);
    block[3] = static_cast<std::byte>(high);

    return registers_.writeBuffer(reg::kCcdChannels, block);
}

}  // namespace g2710::rts8822

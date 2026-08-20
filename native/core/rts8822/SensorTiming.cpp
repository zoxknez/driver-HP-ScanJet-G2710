#include "SensorTiming.h"

#include <cstddef>

namespace g2710::rts8822 {
namespace {

template <typename T, std::size_t N>
constexpr std::size_t countOf(const T (&)[N]) {
    return N;
}

// Rasprsi 36-bitnu masku iz double-a: cetiri puna bajta pa nibble.
// `keepMask` cuva bitove petog bajta koji ne pripadaju vrednosti.
void spreadPhase(RegisterBank& bank, std::size_t index, double phase,
                 std::uint8_t keepMask) {
    bank.set(index + 0, lowByteOfDouble(phase));
    bank.set(index + 1, lowByteOfDouble(shiftRightDouble(phase, 0x08)));
    bank.set(index + 2, lowByteOfDouble(shiftRightDouble(phase, 0x10)));
    bank.set(index + 3, lowByteOfDouble(shiftRightDouble(phase, 0x18)));

    const auto high = static_cast<std::uint8_t>(
        lowByteOfDouble(shiftRightDouble(phase, 0x20)) & 0x0F);
    const auto kept = static_cast<std::uint8_t>(bank.at(index + 4) & keepMask);
    bank.set(index + 4, static_cast<std::uint8_t>(kept | high));
}

}  // namespace

void applySensorClock(RegisterBank& bank, std::size_t index, const profile::Cph& cph) {
    // p1: cetiri bajta pa nibble u petom. Bit 7 petog bajta se cuva
    // (Regs[0x04] &= 0x80), a bitovi 6/5/4 nose ps/ge/go.
    spreadPhase(bank, index, cph.p1, 0x80);

    auto flags = bank.at(index + 4);
    flags = static_cast<std::uint8_t>(flags | ((cph.ps & 1) << 6));
    flags = static_cast<std::uint8_t>(flags | ((cph.ge & 1) << 5));
    flags = static_cast<std::uint8_t>(flags | ((cph.go & 1) << 4));
    bank.set(index + 4, flags);

    // p2: isto, ali peti bajt cuva gornji nibble (Regs[0x09] &= 0xf0).
    spreadPhase(bank, index + 5, cph.p2, 0xF0);
}

void applySensorTiming(RegisterBank& bank, const profile::Timing& timing) {
    // Correlated Double Sample, dva para.
    bank.setBits(timing_offset::kCdss0, 0x3F, static_cast<std::uint8_t>(timing.cdss[0]));
    bank.setBits(timing_offset::kCdsc0, 0x3F, static_cast<std::uint8_t>(timing.cdsc[0]));
    bank.setBits(timing_offset::kCdss1, 0x3F, static_cast<std::uint8_t>(timing.cdss[1]));
    bank.setBits(timing_offset::kCdsc1, 0x3F, static_cast<std::uint8_t>(timing.cdsc[1]));

    bank.setBits(timing_offset::kCnpp, 0x3F, static_cast<std::uint8_t>(timing.cnpp));

    // Tri transfer gate-a dele JEDAN bajt sa cvtrfpw - zato tri odvojene
    // maske nad istim indeksom, a ne jedan upis.
    bank.setBits(timing_offset::kTransferGates, 0x80,
                 static_cast<std::uint8_t>(timing.cvtrp[0]));
    bank.setBits(timing_offset::kTransferGates, 0x40,
                 static_cast<std::uint8_t>(timing.cvtrp[1]));
    bank.setBits(timing_offset::kTransferGates, 0x20,
                 static_cast<std::uint8_t>(timing.cvtrp[2]));
    bank.setBits(timing_offset::kTransferGates, 0x1F,
                 static_cast<std::uint8_t>(timing.cvtrfpw));

    bank.setBits(timing_offset::kTransferBack, 0x1F,
                 static_cast<std::uint8_t>(timing.cvtrbpw));

    bank.setLsb(timing_offset::kTransferWidth, static_cast<std::uint32_t>(timing.cvtrw), 1);

    bank.setLsb(timing_offset::kCphBp2Start, static_cast<std::uint32_t>(timing.cphbp2s), 3);
    bank.setLsb(timing_offset::kCphBp2End, static_cast<std::uint32_t>(timing.cphbp2e), 3);

    bank.setLsb(timing_offset::kClampStart, static_cast<std::uint32_t>(timing.clamps), 3);
    bank.setLsb(timing_offset::kClampEnd, static_cast<std::uint32_t>(timing.clampe), 3);

    // rts8822.c:5103 ima granu za RTS8822L-02A koja `clampe == -1` zamenjuje
    // sa cphbp2e. G2710 je BL-03A, pa se ta grana NIKADA ne izvrsava i ovde je
    // namerno nema - prepisati je znacilo bi tvrditi da vazi i za nas.

    // ADC clock 1: cetiri bajta pa nibble, gornji nibble petog se cuva.
    spreadPhase(bank, timing_offset::kAdcClock0, timing.adcclkp[0], 0xF0);

    // ADC clock 2: peti bajt cuva 0xE0, a bit 4 nosi adcclkp2e.
    spreadPhase(bank, timing_offset::kAdcClock1, timing.adcclkp[1], 0xE0);
    const auto withFlag = static_cast<std::uint8_t>(
        bank.at(timing_offset::kAdcClock1 + 4) | ((timing.adcclkp2e & 1) << 4));
    bank.set(timing_offset::kAdcClock1 + 4, withFlag);

    // Sest clock faza linijskog senzora.
    for (std::size_t i = 0; i < timing_offset::kSensorClockCount; ++i) {
        applySensorClock(bank,
                         timing_offset::kSensorClock0 + i * timing_offset::kSensorClockStride,
                         timing.cph[i]);
    }
}

bool applySensorTiming(RegisterBank& bank, int timingIndex) {
    if (timingIndex < 0 ||
        static_cast<std::size_t>(timingIndex) >= countOf(profile::kTimings)) {
        return false;
    }
    applySensorTiming(bank, profile::kTimings[timingIndex]);
    return true;
}

}  // namespace g2710::rts8822

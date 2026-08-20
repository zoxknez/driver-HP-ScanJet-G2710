#include "RegisterBank.h"

#include "G2710Profile.generated.h"

#include <cmath>

namespace g2710::rts8822 {

static_assert(kBankLength == static_cast<std::size_t>(profile::kRegisterBankLength),
              "duzina bank-a se razisla sa generisanim profilom");

void RegisterBank::setLsb(std::size_t index, std::uint32_t value, std::size_t size) {
    // Referenca odbija size van 1..4 (data_lsb_set); isto radimo, tiho, jer
    // je to jedini ulaz koji se u kodu javlja.
    if (size == 0 || size > 4) {
        return;
    }
    for (std::size_t i = 0; i < size; ++i) {
        data_.at(index + i) = static_cast<std::uint8_t>(value & 0xFF);
        value >>= 8;
    }
}

void RegisterBank::setMsb(std::size_t index, std::uint32_t value, std::size_t size) {
    if (size == 0 || size > 4) {
        return;
    }
    for (std::size_t i = size; i-- > 0;) {
        data_.at(index + i) = static_cast<std::uint8_t>(value & 0xFF);
        value >>= 8;
    }
}

Status RegisterBank::load(RegisterFile& registers) {
    return registers.readBank(bytes());
}

Status RegisterBank::store(RegisterFile& registers) const {
    return registers.writeBank(bytes());
}

double shiftRightDouble(double value, int shift) noexcept {
    // get_shrd: iznad 0x40 vraca nulu, ne pomera dalje.
    if (shift > 0x40) {
        return 0.0;
    }
    return value / std::pow(2.0, static_cast<double>(shift));
}

std::uint8_t lowByteOfDouble(double value) noexcept {
    // get_byte: ako vrednost prelazi 32 bita, prvo skini gornji deo, pa uzmi
    // najnizi bajt. Deljenje umesto pomeranja je namerno - vrednosti su
    // 36-bitne maske faza koje ne staju u unsigned int.
    if (value > 4294967295.0) {
        const double upper = std::floor(shiftRightDouble(value, 0x20));
        value -= upper * std::pow(2.0, 32.0);
    }
    const auto truncated = static_cast<std::uint32_t>(value);
    return static_cast<std::uint8_t>(truncated & 0xFF);
}

}  // namespace g2710::rts8822

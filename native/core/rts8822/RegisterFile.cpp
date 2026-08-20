#include "RegisterFile.h"

#include "G2710Profile.generated.h"

#include <array>

namespace g2710::rts8822 {
namespace {

constexpr Command kRead = Command::RegisterRead;    // wIndex 0x0100
constexpr Command kWrite = Command::RegisterWrite;  // wIndex 0x0000

}  // namespace

Result<std::uint8_t> RegisterFile::readByte(std::uint16_t address) {
    // Referenca cita 2 bajta i uzima buffer[0], cak i za jedan registar.
    std::array<std::byte, 2> pair{};
    if (const Status s = transport_.controlIn(address, kRead, pair); !s) {
        return s.error();
    }
    return static_cast<std::uint8_t>(pair[0]);
}

Result<std::uint16_t> RegisterFile::readWord(std::uint16_t address) {
    std::array<std::byte, 2> pair{};
    if (const Status s = transport_.controlIn(address, kRead, pair); !s) {
        return s.error();
    }
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(pair[0]) |
                                      (static_cast<std::uint16_t>(pair[1]) << 8));
}

Result<std::uint32_t> RegisterFile::readInteger(std::uint16_t address) {
    std::array<std::byte, 4> quad{};
    if (const Status s = transport_.controlIn(address, kRead, quad); !s) {
        return s.error();
    }
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i) {
        value = (value << 8) | static_cast<std::uint32_t>(quad[static_cast<std::size_t>(i)]);
    }
    return value;
}

Status RegisterFile::readBuffer(std::uint16_t address, std::span<std::byte> buffer) {
    return transport_.controlIn(address, kRead, buffer);
}

Status RegisterFile::writeByte(std::uint16_t address, std::uint8_t value) {
    // Korak 1: procitaj par na address + 1. Referenca cita SLEDECI registar,
    // ne trazeni - to nije greska u prepisu.
    std::array<std::byte, 2> pair{};
    const auto next = static_cast<std::uint16_t>(address + 1);
    if (const Status s = transport_.controlIn(next, kRead, pair); !s) {
        return s;
    }

    // Korak 2-3: stari bajt se pomera navise, novi ide na nizu poziciju.
    pair[1] = pair[0];
    pair[0] = static_cast<std::byte>(value);

    // Korak 4: upisi par na trazenu adresu, ovaj put sa wIndex 0x0000.
    return transport_.controlOut(address, kWrite, pair);
}

Status RegisterFile::writeWord(std::uint16_t address, std::uint16_t value) {
    const std::array<std::byte, 2> pair{
        static_cast<std::byte>(value & 0xFF),
        static_cast<std::byte>((value >> 8) & 0xFF),
    };
    return transport_.controlOut(address, kWrite, pair);
}

Status RegisterFile::writeBuffer(std::uint16_t address, std::span<const std::byte> buffer) {
    return transport_.controlOut(address, kWrite, buffer);
}

Status RegisterFile::readBank(std::span<std::byte> bank) {
    if (bank.size() != static_cast<std::size_t>(profile::kRegisterBankLength)) {
        return fail(ErrorCode::InvalidArgument, "readBank: pogresna velicina bank-a");
    }
    return readBuffer(static_cast<std::uint16_t>(profile::kRegisterBankBase), bank);
}

Status RegisterFile::writeBank(std::span<const std::byte> bank) {
    if (bank.size() != static_cast<std::size_t>(profile::kRegisterBankLength)) {
        return fail(ErrorCode::InvalidArgument, "writeBank: pogresna velicina bank-a");
    }
    return writeBuffer(static_cast<std::uint16_t>(profile::kRegisterBankBase), bank);
}

}  // namespace g2710::rts8822

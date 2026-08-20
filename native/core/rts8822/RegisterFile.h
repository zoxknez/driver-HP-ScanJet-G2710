// Pristup registrima RTS8822 preko ITransport-a.
//
// Semantika je DOSLOVNO preuzeta iz hp3900_usb.c. Odstupanja nisu dozvoljena
// ni kada izgledaju kao poboljsanje - vidi writeByte().
//
//   readByte / readWord / readInteger / readBuffer   wIndex 0x0100
//   writeWord / writeBuffer                          wIndex 0x0000
//   writeByte                                        OBOJE, tim redom
//
// Vidi docs/PROTOCOL-RTS8822.md, 4.

#pragma once

#include "../transport/ITransport.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace g2710::rts8822 {

class RegisterFile {
public:
    explicit RegisterFile(ITransport& transport) : transport_(transport) {}

    // --- citanje -------------------------------------------------------
    // Referenca uvek cita PAR bajtova i uzima nizi, cak i za jedan registar.
    Result<std::uint8_t> readByte(std::uint16_t address);
    Result<std::uint16_t> readWord(std::uint16_t address);
    Result<std::uint32_t> readInteger(std::uint16_t address);
    Status readBuffer(std::uint16_t address, std::span<std::byte> buffer);

    // --- upis ----------------------------------------------------------

    // PAZNJA: ovo je read-modify-write, a ne prost upis.
    //
    //   1. procitaj PAR na address + 1   (wIndex 0x0100, 2 bajta)
    //   2. buffer[1] = buffer[0]         stari bajt se pomera navise
    //   3. buffer[0] = value             novi bajt ide na nizu poziciju
    //   4. upisi PAR na address          (wIndex 0x0000, 2 bajta)
    //
    // Znaci: upis JEDNOG registarskog bajta je DVA USB transfera i dira DVA
    // registra. Implementacija kao jedan OUT transfer tiho korumpira susedni
    // registar, a to se vidi tek kao pogresna slika.
    Status writeByte(std::uint16_t address, std::uint8_t value);

    Status writeWord(std::uint16_t address, std::uint16_t value);
    Status writeBuffer(std::uint16_t address, std::span<const std::byte> buffer);

    // --- ceo bank ------------------------------------------------------
    // RTS_ReadRegs / RTS_WriteRegs: 1818 bajtova na 0xE800 odjednom.
    Status readBank(std::span<std::byte> bank);
    Status writeBank(std::span<const std::byte> bank);

    ITransport& transport() noexcept { return transport_; }

private:
    ITransport& transport_;
};

// --- pomocne funkcije za bitfieldove ------------------------------------
// Odgovaraju data_bitset iz reference: upisi `value` u bitove koje pokriva
// `mask`, poravnato na najnizi postavljen bit maske.

constexpr std::uint8_t bitsetValue(std::uint8_t current, std::uint8_t mask,
                                   std::uint8_t value) noexcept {
    if (mask == 0) {
        return current;
    }
    std::uint8_t shift = 0;
    while (((mask >> shift) & 1) == 0) {
        ++shift;
    }
    const auto shifted = static_cast<std::uint8_t>((value << shift) & mask);
    return static_cast<std::uint8_t>((current & static_cast<std::uint8_t>(~mask)) | shifted);
}

constexpr std::uint8_t bitsetGet(std::uint8_t current, std::uint8_t mask) noexcept {
    if (mask == 0) {
        return 0;
    }
    std::uint8_t shift = 0;
    while (((mask >> shift) & 1) == 0) {
        ++shift;
    }
    return static_cast<std::uint8_t>((current & mask) >> shift);
}

}  // namespace g2710::rts8822

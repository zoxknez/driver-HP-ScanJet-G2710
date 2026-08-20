// In-memory slika registarskog bank-a.
//
// Referenca vecinu podesavanja ne salje transfer po transfer nego gradi
// `SANE_Byte Regs[RT_BUFFER_LEN]` u memoriji i onda ga upise odjednom
// (RTS_WriteRegs). Zato su funkcije tipa RTS_Setup_SensorTiming CISTE
// transformacije bank-a - ne diraju uredjaj.
//
// Ta podela je i za nas korisna: sve sto radi nad bank-om testira se bez
// transporta i bez simulatora.

#pragma once

#include "../util/Result.h"
#include "RegisterFile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace g2710::rts8822 {

// RT_BUFFER_LEN. Duplirano iz profila jer je ovo osnovni tip - ali provereno
// static_assert-om u RegisterBank.cpp da se dve vrednosti ne raziidju.
inline constexpr std::size_t kBankLength = 0x71A;

class RegisterBank {
public:
    RegisterBank() { data_.fill(0); }

    // --- pristup po INDEKSU u bank-u (Regs[0x92]) -----------------------
    std::uint8_t at(std::size_t index) const { return data_.at(index); }
    void set(std::size_t index, std::uint8_t value) { data_.at(index) = value; }

    // --- pristup po APSOLUTNOJ adresi (0xE992) --------------------------
    // Referenca naizmenicno koristi oba oblika za ista mesta.
    std::uint8_t atAddress(std::uint16_t address) const { return at(indexOf(address)); }
    void setAddress(std::uint16_t address, std::uint8_t value) { set(indexOf(address), value); }

    static constexpr std::size_t indexOf(std::uint16_t address) noexcept {
        return static_cast<std::size_t>(address) - 0xE800;
    }

    // --- pomocne operacije, imenovane po referenci ----------------------

    // data_bitset: upisi `value` u bitove koje pokriva `mask`, poravnato na
    // najnizi postavljen bit maske.
    void setBits(std::size_t index, std::uint8_t mask, std::uint8_t value) {
        data_.at(index) = bitsetValue(data_.at(index), mask, value);
    }

    std::uint8_t getBits(std::size_t index, std::uint8_t mask) const {
        return bitsetGet(data_.at(index), mask);
    }

    // data_lsb_set: rasprsi `value` po `size` bajtova, najnizi bajt prvi.
    void setLsb(std::size_t index, std::uint32_t value, std::size_t size);

    // data_msb_set: isto, ali najvisi bajt prvi.
    void setMsb(std::size_t index, std::uint32_t value, std::size_t size);

    // data_lsb_get: procitaj `size` bajtova, najnizi bajt prvi.
    std::uint32_t getLsb(std::size_t index, std::size_t size) const;

    // data_wide_bitset: bitfield koji se prostire preko VISE bajtova.
    //
    // Maska je 32-bitna i cita se bajt po bajt. Prvi bajt sa nenultom maskom
    // uzima najnize bitove vrednosti poravnate na svoj najnizi postavljen bit;
    // svaki sledeci uzima narednih 8 bita. Za masku 0x3FFF to znaci 8 bita u
    // prvom bajtu i 6 u drugom - ukupno 14, sto je sirina pokazivaca na
    // tabelu motornih koraka.
    void setWideBits(std::size_t index, std::uint32_t mask, std::uint32_t value);

    std::span<const std::byte> bytes() const noexcept {
        return {reinterpret_cast<const std::byte*>(data_.data()), data_.size()};
    }
    std::span<std::byte> bytes() noexcept {
        return {reinterpret_cast<std::byte*>(data_.data()), data_.size()};
    }

    static constexpr std::size_t size() noexcept { return kBankLength; }

    // Ucitaj sa uredjaja / upisi na uredjaj (RTS_ReadRegs / RTS_WriteRegs).
    Status load(RegisterFile& registers);
    Status store(RegisterFile& registers) const;

private:
    std::array<std::uint8_t, kBankLength> data_{};
};

// --- 36-bitne vrednosti u double-u -----------------------------------------
//
// CCD clock faze prelaze 32 bita (npr. 0xFFFFFFFFF), pa ih referenca drzi kao
// double i vadi bajtove deljenjem sa 2^n umesto pomeranjem. Zadrzavamo isti
// racun da bi rezultat bio bit-identican.

// get_shrd(value, desp) = value / 2^desp, uz 0 iznad 0x40.
double shiftRightDouble(double value, int shift) noexcept;

// get_byte(value): najnizi bajt, uz skidanje dela iznad 32 bita.
std::uint8_t lowByteOfDouble(double value) noexcept;

}  // namespace g2710::rts8822

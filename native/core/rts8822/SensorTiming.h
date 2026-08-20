// CCD timing profili (Toshiba TCD2905).
//
// rts8822.c:5066 RTS_Setup_SensorTiming i :5028 Timing_SetLinearImageSensorClock.
//
// Ovo je CISTA transformacija registarskog bank-a - ne dodiruje uredjaj i ne
// zahteva SafetyGate. Bank se kasnije upisuje odjednom (RTS_WriteRegs).
//
// Profil se bira indeksom `timing` iz scan mode tabele; svih 20 profila je
// ekstraktovano u G2710Profile.generated.h kao kTimings.

#pragma once

#include "RegisterBank.h"

#include "G2710Profile.generated.h"

namespace g2710::rts8822 {

// Indeksi u bank-u na koje RTS_Setup_SensorTiming pise. Imenovani da bi se
// videlo sta se dira - referenca ih koristi kao gole brojeve.
namespace timing_offset {

inline constexpr std::size_t kTransferGates = 0x45;   // cvtrp[0..2], cvtrfpw
inline constexpr std::size_t kTransferBack = 0x46;    // cvtrbpw
inline constexpr std::size_t kTransferWidth = 0x47;   // cvtrw

// Sest clock faza linijskog senzora, svaka po 10 bajtova.
inline constexpr std::size_t kSensorClock0 = 0x48;
inline constexpr std::size_t kSensorClockStride = 0x0A;
inline constexpr std::size_t kSensorClockCount = 6;

inline constexpr std::size_t kCphBp2Start = 0x84;
inline constexpr std::size_t kCphBp2End = 0x87;
inline constexpr std::size_t kClampStart = 0x8A;
inline constexpr std::size_t kClampEnd = 0x8D;

inline constexpr std::size_t kCdss0 = 0x92;
inline constexpr std::size_t kCdsc0 = 0x93;
inline constexpr std::size_t kCdss1 = 0x94;
inline constexpr std::size_t kCdsc1 = 0x95;
inline constexpr std::size_t kCnpp = 0x96;

inline constexpr std::size_t kAdcClock0 = 0x97;  // 0x97..0x9B
inline constexpr std::size_t kAdcClock1 = 0xC1;  // 0xC1..0xC5

}  // namespace timing_offset

// rts8822.c:5028 - jedna clock faza zauzima 10 bajtova.
//
// p1 i p2 su 36-bitne maske drzane kao double; nizih 32 bita idu u cetiri
// bajta, a preostala 4 bita u nibble petog.
void applySensorClock(RegisterBank& bank, std::size_t index, const profile::Cph& cph);

// rts8822.c:5066 - primeni ceo timing profil na bank.
//
// Vraca false ako je indeks van kTimings.
bool applySensorTiming(RegisterBank& bank, int timingIndex);

void applySensorTiming(RegisterBank& bank, const profile::Timing& timing);

}  // namespace g2710::rts8822

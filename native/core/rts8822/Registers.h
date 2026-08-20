// Imenovani RTS8822 registri i bitfieldovi.
//
// Svaka vrednost ovde je izvedena iz konkretnog mesta u referenci, i to mesto
// je navedeno. Nedokumentovana konstanta u ovom fajlu je greska.
//
// Adresiranje: registar N u bank-u je na 0xE800 + N. Referenca ista mesta
// naizmenicno zove i apsolutnom adresom (Read_Byte(0xE96F)) i indeksom u
// bank-u (Regs[0x16F]) - ovde su oba oblika vidljiva.

#pragma once

#include <cstdint>

namespace g2710::rts8822::reg {

// --- kontrola i status -----------------------------------------------------

// rts8822.c:3843 RTS_IsExecuting, :3913 RTS_Warm_Reset, :3868 RTS_WaitScanEnd
inline constexpr std::uint16_t kControl = 0xE800;  // Regs[0x000]
inline constexpr std::uint8_t kControlExecutingBit = 0x80;  // 1 = scan u toku
inline constexpr std::uint8_t kControlWarmResetBit = 0x40;  // set pa clear

// --- CCD kanali ------------------------------------------------------------

// rts8822.c:3890 RTS_Enable_CCD - cita 4 bajta sa 0xE810, menja dva polja
inline constexpr std::uint16_t kCcdChannels = 0xE810;      // Regs[0x010]
inline constexpr std::size_t kCcdChannelsLength = 4;
inline constexpr std::uint8_t kCcdChannelsLowMask = 0xE0;  // u Regs[0x010]
inline constexpr std::uint8_t kCcdChannelsHighMask = 0x80; // u Regs[0x013]
inline constexpr std::uint16_t kCcdChannelsHigh = 0xE813;  // Regs[0x013]

// --- lampa -----------------------------------------------------------------

// rts8822.c:4104 Lamp_Status_Get
inline constexpr std::uint16_t kLampStatus = 0xE946;         // Regs[0x146]
inline constexpr std::uint8_t kLampStatusFlbBit = 0x40;      // flatbed
inline constexpr std::uint8_t kLampStatusTmaBit = 0x20;      // TMA, uz kLampMode

// rts8822.c:4106 Lamp_Status_Get cita RECI sa ove adrese
inline constexpr std::uint16_t kLampMode = 0xE954;           // Regs[0x154]
inline constexpr std::uint16_t kLampModeTmaSelectBit = 0x0010;

// rts8822.c:2519 Lamp_PWM_DutyCycle_Get, :2545 _Set
inline constexpr std::uint16_t kLampPwm = 0xE948;            // Regs[0x148]
inline constexpr std::uint8_t kLampPwmDutyMask = 0x3F;
inline constexpr std::uint8_t kLampPwmLevelBit = 0x40;

// rts8822.c:2550 Lamp_PWM_DutyCycle_Set, drugi upis
inline constexpr std::uint16_t kLampPwmExtra = 0xE9E0;       // Regs[0x1E0]

// --- pozicija glave --------------------------------------------------------

// rts8822.c:3817 Head_IsAtHome
inline constexpr std::uint16_t kHeadSensor = 0xE96F;         // Regs[0x16F]
inline constexpr std::uint8_t kHeadAtHomeBit = 0x40;

// --- zakljucavanje ---------------------------------------------------------

// rts8822.c:774 SetLock
inline constexpr std::uint16_t kLock = 0xEE00;               // Regs[0x600]

}  // namespace g2710::rts8822::reg

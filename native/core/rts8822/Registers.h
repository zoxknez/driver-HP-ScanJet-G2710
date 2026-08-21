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

// --- geometrija skeniranja -------------------------------------------------
//
// rts8822.c:9229 RTS_Setup_Coords. Cela oblast skeniranja stoji u registrima -
// nista se ne drzi sa strane. Zato simulator geometriju CITA odavde, kroz iste
// adrese kroz koje bi je citao i cip, umesto kroz neki testni kanal koji u
// produkciji ne postoji.
inline constexpr std::uint16_t kScanLeft = 0xE8B0;   // Regs[0x0B0], 2 bajta
inline constexpr std::uint16_t kScanRight = 0xE8B2;  // Regs[0x0B2], levo + sirina
inline constexpr std::uint16_t kScanTop = 0xE8D0;    // Regs[0x0D0], 2 bajta
inline constexpr std::uint16_t kScanBottom = 0xE8D2; // Regs[0x0D2], gore + visina

// Treci bajt obe uspravne koordinate. Nizi nibl pripada vrhu, visi dnu, pa je
// najveca uspravna koordinata 20-bitna: 0xE8D0 daje 16 bita, ovaj jos 4.
inline constexpr std::uint16_t kScanVerticalHigh = 0xE8D4;  // Regs[0x0D4]
inline constexpr std::uint8_t kScanTopHighMask = 0x0F;
inline constexpr std::uint8_t kScanBottomHighMask = 0xF0;

// rts8822.c:8701 RTS_Setup_Line_Distances - pomak reda po kanalu.
//
// Pet SESTOBITNIH polja. Sirina je razlog za D3: G2710 na 1200 dpi trazi 64 u
// 0x14C, a 64 ne staje u sest bita. Vidi docs/REFERENCE-DEFECTS.md.
inline constexpr std::uint16_t kLineOffsetEvenOdd = 0xE949;       // Regs[0x149]
inline constexpr std::uint16_t kLineOffsetDistance = 0xE94A;      // Regs[0x14A]
inline constexpr std::uint16_t kLineOffsetDistancePlus = 0xE94B;  // Regs[0x14B]
inline constexpr std::uint16_t kLineOffsetDouble = 0xE94C;        // Regs[0x14C]
inline constexpr std::uint16_t kLineOffsetDoublePlus = 0xE94D;    // Regs[0x14D]
inline constexpr std::uint8_t kLineOffsetMask = 0x3F;

// rts8822.c:9121 - odnos rezolucija: sensorResolution / resolution_x.
// Za G2710 (senzor 2400): 150 dpi -> 16, 300 -> 8, 600 -> 4, 1200 -> 2, 2400 -> 1.
// Odavde se rezolucija skeniranja moze rekonstruisati iz samih registara.
inline constexpr std::uint16_t kResolutionRatio = 0xE8C0;  // Regs[0x0C0]
inline constexpr std::uint8_t kResolutionRatioMask = 0x1F;

// rts8822.c:9144 - broj "dummy" redova; uspravne koordinate su izrazene u
// njima, ne u redovima slike.
inline constexpr std::uint16_t kDummyLine = 0xE8D6;  // Regs[0x0D6]
inline constexpr std::uint8_t kDummyLineMask = 0xF0;

// rts8822.c:8774 RTS_Setup_Depth - kanala po tacki.
inline constexpr std::uint16_t kChannelsPerDot = 0xE812;  // Regs[0x012]
inline constexpr std::uint8_t kChannelsPerDotMask = 0xC0;

// Redosled kanala unutar tacke; ista adresa, nizi bitovi. rts8822.c:9033.
inline constexpr std::uint8_t kChannelOrder0Mask = 0x03;
inline constexpr std::uint8_t kChannelOrder1Mask = 0x0C;
inline constexpr std::uint8_t kChannelOrder2Mask = 0x30;

// rts8822.c:8783 RTS_Setup_Depth - dubina po kanalu, u registru za shading.
// Vrednosti odgovaraju image::DepthCode: 0=8, 1=12, 2=16, 3=lineart.
inline constexpr std::uint16_t kDepthCode = 0xE9CF;  // Regs[0x1CF]
inline constexpr std::uint8_t kDepthCodeMask = 0x30;

// rts8822.c:7731 - dva bita koja zajedno kazu da kanal zauzima dva bajta.
inline constexpr std::uint16_t kChannelSize = 0xEE0B;  // Regs[0x60B]
inline constexpr std::uint8_t kChannelSizeWideBit = 0x40;
inline constexpr std::uint8_t kChannelSizeNarrowBit = 0x08;

// rts8822.c:8390 Scan_Start - prag za lineart, dve reci.
inline constexpr std::uint16_t kThresholdLow = 0xE99E;   // Regs[0x19E]
inline constexpr std::uint16_t kThresholdHigh = 0xE9A0;  // Regs[0x1A0]

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

// rts8822.c:10771 Lamp_Status_Set - bira KOJA lampa je aktivna.
//
// PAZNJA: ovo NIJE isti bajt koji Lamp_Status_Get cita u BL-03A grani.
// Set upisuje Regs[0x155] bit 0x10, a Get (BL-03A) testira Regs[0x154] bit
// 0x10 - dva razlicita bajta. Vidi docs/REFERENCE-DEFECTS.md.
inline constexpr std::uint16_t kLampSelect = 0xE955;         // Regs[0x155]
inline constexpr std::uint8_t kLampSelectTmaBit = 0x10;

// --- pozicija glave --------------------------------------------------------

// rts8822.c:3817 Head_IsAtHome
inline constexpr std::uint16_t kHeadSensor = 0xE96F;         // Regs[0x16F]
inline constexpr std::uint8_t kHeadAtHomeBit = 0x40;

// --- zakljucavanje ---------------------------------------------------------

// rts8822.c:773 SetLock - bit 2
inline constexpr std::uint16_t kLock = 0xEE00;               // Regs[0x600]
inline constexpr std::uint8_t kLockBit = 0x04;

// --- GPIO ------------------------------------------------------------------

// rts8822.c:799 Set_E950_Mode, :1086 RTS_Sensor_Type
inline constexpr std::uint16_t kGpio0 = 0xE950;              // Regs[0x150]
inline constexpr std::uint16_t kGpio0ModeBit = 0x0040;

// rts8822.c:1087 RTS_Sensor_Type
inline constexpr std::uint16_t kGpio1 = 0xE956;              // Regs[0x156]

// rts8822.c:1096 RTS_Sensor_Type - ulaz koji nosi tip senzora
inline constexpr std::uint16_t kGpioSense = 0xE968;          // Regs[0x168]

// --- DMA -------------------------------------------------------------------

// rts8822.c:4151 RTS_DMA_WaitReady - anketira bit 0
inline constexpr std::uint16_t kDmaStatus = 0xEF09;          // Regs[0x709]
inline constexpr std::uint8_t kDmaStatusReadyBit = 0x01;

// --- adrese IZVAN register bank-a ------------------------------------------
//
// Bank pokriva 0xE800 .. 0xEF19 (0x71A bajtova). Cip ima siri adresni prostor
// i referenca ga koristi: RTS_WaitInitEnd (rts8822.c:4181) cita 0xF910, sto je
// van bank-a. RTS_ReadRegs / RTS_WriteRegs takve adrese NE prenose, pa im se
// pristupa samo pojedinacno.

inline constexpr std::uint16_t kInitStatus = 0xF910;         // van bank-a
inline constexpr std::uint8_t kInitStatusDoneBit = 0x08;

constexpr bool isInRegisterBank(std::uint16_t address) noexcept {
    return address >= 0xE800 && address < 0xE800 + 0x71A;
}

}  // namespace g2710::rts8822::reg

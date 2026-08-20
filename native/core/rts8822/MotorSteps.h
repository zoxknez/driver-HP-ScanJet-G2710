// Pretakanje motornih krivih u tabelu koraka.
//
// rts8822.c:3011 Motor_Setup_Steps - najveci pojedinacni komad faze G2710-2.
//
// Cip ne prima krive kao takve. Prima JEDAN bajt-stream koraka i sedam
// pokazivaca u njega, po jedan za svaku kombinaciju smera i namene. Svaki
// korak je tri bajta, MSB-first.
//
// Deo koji se lako previdi: ako su dve krive identicne, referenca NE upisuje
// drugu kopiju nego usmeri drugi pokazivac na istu tabelu. Step bafer je
// ogranicen na 14 bita adrese, pa je to stvarna usteda, a ne stil.
//
// Racunanje je CISTO - menja sliku bank-a i vraca stream. Slanje na uredjaj je
// odvojeno (uploadMotorSteps), jer trazi warm reset, zakljucavanje i DMA.

#pragma once

#include "../device/SafetyLevel.h"
#include "Dma.h"
#include "Gpio.h"
#include "RegisterBank.h"
#include "Rts8822.h"

#include "G2710Profile.generated.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace g2710::rts8822 {

// Vrednosti iz hp3900_types.c: ACC_CURVE = 0, DEC_CURVE = 1.
enum class CurveDirection : int {
    Acceleration = 0,
    Deceleration = 1,
};

// CRV_NORMALSCAN = 0, CRV_PARKHOME = 1, CRV_SMEARING = 2, CRV_BUFFERFULL = 3.
enum class CurveKind : int {
    NormalScan = 0,
    ParkHome = 1,
    Smearing = 2,
    BufferFull = 3,
};

const char* toString(CurveDirection direction) noexcept;
const char* toString(CurveKind kind) noexcept;

// Sedam slotova sa svojim registrima. Tabela je doslovno prepisana iz debug
// ispisa na kraju Motor_Setup_Steps (rts8822.c:3395-3417), koji je i sam
// dokumentacija rasporeda.
struct CurveSlot {
    CurveDirection direction;
    CurveKind kind;
    std::size_t pointerRegister;   // 14-bitni pokazivac, maska 0x3FFF
    std::size_t lastStepRegister;  // tri bajta, LSB-first
};

inline constexpr CurveSlot kCurveSlots[] = {
    {CurveDirection::Acceleration, CurveKind::NormalScan, 0x0F6, 0x0E1},
    {CurveDirection::Acceleration, CurveKind::Smearing,   0x0FA, 0x0E4},
    {CurveDirection::Acceleration, CurveKind::ParkHome,   0x100, 0x0E7},
    {CurveDirection::Deceleration, CurveKind::NormalScan, 0x0FE, 0x0ED},
    {CurveDirection::Deceleration, CurveKind::Smearing,   0x0FC, 0x0F0},
    {CurveDirection::Deceleration, CurveKind::ParkHome,   0x102, 0x0F3},
    {CurveDirection::Deceleration, CurveKind::BufferFull, 0x0F8, 0x0EA},
};

inline constexpr std::uint32_t kCurvePointerMask = 0x3FFF;

// Registri koje racun cita, a ne pise.
namespace motor_offset {
inline constexpr std::size_t kStepSize = 0x0E0;        // Regs[0xe0] + 1
inline constexpr std::size_t kLineExposure = 0x030;    // tri bajta
}  // namespace motor_offset

struct MotorStepProgram {
    // Bajt-stream koji ide u DMA. Duzina je zaokruzena navise na visekratnik
    // od 16 (rts8822.c:3391).
    std::vector<std::byte> steps;

    // Povratna vrednost Motor_Setup_Steps.
    int accelerationStepCount = 0;

    // Broj koraka pre zaokruzivanja - dijagnostika, da se vidi koliko je
    // dopune dodato.
    std::size_t rawLength = 0;
};

struct MotorStepParams {
    // Indeks u profile::kMotorCurves (12 krivih).
    int curveIndex = 0;

    // v15f8: baza step bafera u 16-bajtnim jedinicama. Zavisi od velicine
    // gamma tabela koje stoje ispred njega u istom DMA prostoru.
    std::uint16_t stepBufferBase = 0;

    // motorbackstep iz krive; referenca ga cita iz mtrsetting.
    int motorBackStep = 0;
};

// Cist racun: menja `bank` i vraca stream. Ne dodiruje uredjaj.
//
// Vraca nullopt ako je curveIndex van opsega.
std::optional<MotorStepProgram> buildMotorSteps(RegisterBank& bank,
                                                const MotorStepParams& params);

#if G2710_MOTOR_PATH_COMPILED

// Slanje na uredjaj. rts8822.c:3419-3437:
//
//   warm reset -> lock -> DMA write -> unlock
//
// Zakljucavanje je oko DMA upisa, ne oko celog racuna.
Status uploadMotorSteps(Rts8822& chip, Gpio& gpio, Dma& dma,
                        const MotorStepProgram& program,
                        std::uint16_t stepBufferBase);

#endif  // G2710_MOTOR_PATH_COMPILED

}  // namespace g2710::rts8822

#include "MotorSteps.h"

#include <algorithm>
#include <cstddef>

namespace g2710::rts8822 {
namespace {

template <typename T, std::size_t N>
constexpr std::size_t countOf(const T (&)[N]) {
    return N;
}

// Motor_AddStep: jedan korak = tri bajta, MSB-first.
void addStep(std::vector<std::byte>& steps, std::uint32_t step) {
    steps.push_back(static_cast<std::byte>((step >> 16) & 0xFF));
    steps.push_back(static_cast<std::byte>((step >> 8) & 0xFF));
    steps.push_back(static_cast<std::byte>(step & 0xFF));
}

// Motor_Curve_Get: nadji segment date krive po smeru i nameni.
const profile::MotorCurveSegment* findSegment(const profile::MotorCurve& curve,
                                              CurveDirection direction, CurveKind kind) {
    for (const auto& segment : curve.segments) {
        if (segment.type == static_cast<int>(direction) &&
            segment.name == static_cast<int>(kind)) {
            return &segment;
        }
    }
    return nullptr;
}

// Motor_Curve_Equal: iste duzine i isti koraci.
bool segmentsEqual(const profile::MotorCurveSegment* a,
                   const profile::MotorCurveSegment* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    if (a->count != b->count) {
        return false;
    }
    for (int i = 0; i < a->count; ++i) {
        if (a->values[i] != b->values[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

const char* toString(CurveDirection direction) noexcept {
    switch (direction) {
        case CurveDirection::Acceleration: return "ACC";
        case CurveDirection::Deceleration: return "DEC";
    }
    return "?";
}

const char* toString(CurveKind kind) noexcept {
    switch (kind) {
        case CurveKind::NormalScan:  return "NORMALSCAN";
        case CurveKind::ParkHome:    return "PARKHOME";
        case CurveKind::Smearing:    return "SMEARING";
        case CurveKind::BufferFull:  return "BUFFERFULL";
    }
    return "?";
}

std::optional<MotorStepProgram> buildMotorSteps(RegisterBank& bank,
                                                const MotorStepParams& params) {
    if (params.curveIndex < 0 ||
        static_cast<std::size_t>(params.curveIndex) >= countOf(profile::kMotorCurves)) {
        return std::nullopt;
    }

    const profile::MotorCurve& curve = profile::kMotorCurves[params.curveIndex];

    MotorStepProgram program;
    auto& steps = program.steps;

    // Bazni offset u bajtovima; v15f8 je u jedinicama od 16.
    auto bufferOffset = static_cast<std::uint32_t>(params.stepBufferBase << 4) & 0xFFFF;

    int accCount = 0;
    int decCount = 0;

    // --- ACC / NORMALSCAN ---------------------------------------------
    const auto* accNormal = findSegment(curve, CurveDirection::Acceleration,
                                        CurveKind::NormalScan);

    // Poslednji dozvoljeni korak ubrzanja; cita se iz bank-a, ne racuna.
    const std::uint32_t lastAccStep = bank.getLsb(kCurveSlots[0].lastStepRegister, 3);
    std::uint32_t travelled = 0;

    bank.setWideBits(kCurveSlots[0].pointerRegister, kCurvePointerMask, bufferOffset);

    std::uint32_t firstStep = 0;
    if (accNormal != nullptr && accNormal->count > 0) {
        accCount = accNormal->count;
        firstStep = static_cast<std::uint32_t>(accNormal->values[0]);

        for (int i = 0; i < accNormal->count; ++i) {
            const auto step = static_cast<std::uint32_t>(accNormal->values[i]);
            // Kriva se prekida cim padne na ili ispod poslednjeg koraka -
            // ostatak tabele se ne upisuje.
            if (step <= lastAccStep) {
                accCount = i;
                break;
            }
            travelled += step + 1;
            addStep(steps, step);
        }
    }

    if (accCount == 0) {
        ++accCount;
        travelled += (lastAccStep + 1) + 1;
        addStep(steps, lastAccStep + 1);
    }

    ++accCount;
    travelled += lastAccStep + 1;
    addStep(steps, lastAccStep);

    // Prvi korak se NAKNADNO prepisuje izracunatom vrednoscu koja poravnava
    // ukupno vreme na visekratnik vremena ekspozicije reda (rts8822.c:3075).
    const std::uint32_t lineExposure = bank.getLsb(motor_offset::kLineExposure, 3) + 1;
    if (!steps.empty() && lineExposure != 0) {
        const std::uint32_t rounded = (travelled + lineExposure - 1) / lineExposure;
        const auto adjusted =
            static_cast<std::uint32_t>((rounded * lineExposure) + firstStep - travelled - 0x0D);
        steps[0] = static_cast<std::byte>((adjusted >> 16) & 0xFF);
        steps[1] = static_cast<std::byte>((adjusted >> 8) & 0xFF);
        steps[2] = static_cast<std::byte>(adjusted & 0xFF);
    }

    // --- DEC / BUFFERFULL ---------------------------------------------
    bufferOffset += static_cast<std::uint32_t>(accCount * 3);
    bank.setWideBits(kCurveSlots[6].pointerRegister, kCurvePointerMask, bufferOffset);

    const auto* decBufferFull = findSegment(curve, CurveDirection::Deceleration,
                                            CurveKind::BufferFull);
    std::uint32_t lastDecStep = 0;
    if (decBufferFull != nullptr && decBufferFull->count > 0) {
        decCount = decBufferFull->count;
        lastDecStep = static_cast<std::uint32_t>(decBufferFull->values[decBufferFull->count - 1]);
        bank.setLsb(kCurveSlots[6].lastStepRegister, lastDecStep, 3);
    }

    ++decCount;
    addStep(steps, lastAccStep);

    if (decBufferFull != nullptr && decBufferFull->count > 1) {
        for (int i = 0; i < decBufferFull->count - 1; ++i) {
            const auto step = static_cast<std::uint32_t>(decBufferFull->values[i]);
            if (step > lastAccStep) {
                addStep(steps, step);
            } else {
                --decCount;
            }
        }
    }

    // --- dopuna do motorbackstep --------------------------------------
    std::uint32_t tailStep = lastDecStep;
    std::int32_t padding = 0;

    if (params.motorBackStep > 0) {
        const std::uint32_t stepSize = bank.at(motor_offset::kStepSize) + 1u;
        std::int32_t remaining = (params.motorBackStep - decCount) - accCount + 2;
        const std::int32_t total = remaining;

        if (stepSize != 0) {
            remaining = (remaining / static_cast<std::int32_t>(stepSize)) *
                        static_cast<std::int32_t>(stepSize);
        }

        if (lastAccStep >= tailStep) {
            tailStep = lastAccStep + 1;
        }
        padding = total - remaining;
        decCount += padding;
    }

    for (std::int32_t i = padding; i > 0; --i) {
        addStep(steps, tailStep - 1);
    }
    addStep(steps, tailStep);

    // --- preostali slotovi, sa deljenjem tabela ------------------------
    // Redosled je onaj iz reference: ACC smearing, DEC smearing, DEC
    // normalscan, ACC parkhome, DEC parkhome.
    struct Alias {
        std::size_t slot;
        std::size_t aliasOf[2];  // slotovi sa kojima se poredi, redom
        std::size_t aliasCount;
    };

    static constexpr Alias kOrder[] = {
        {1, {0, 0}, 1},  // ACC smearing   -> ACC normalscan
        {4, {6, 0}, 1},  // DEC smearing   -> DEC bufferfull
        {3, {6, 4}, 2},  // DEC normalscan -> DEC bufferfull, pa DEC smearing
        {2, {0, 1}, 2},  // ACC parkhome   -> ACC normalscan, pa ACC smearing
        {5, {6, 4}, 2},  // DEC parkhome   -> DEC bufferfull, pa DEC smearing
    };

    for (const Alias& entry : kOrder) {
        const CurveSlot& slot = kCurveSlots[entry.slot];
        const auto* segment = findSegment(curve, slot.direction, slot.kind);

        if (segment == nullptr || segment->count == 0) {
            // Kriva iskljucena: pokazivac na nulu.
            bank.setWideBits(slot.pointerRegister, kCurvePointerMask, 0);
            continue;
        }

        // Ako se poklapa sa ranijom krivom, deli njenu tabelu umesto da se
        // upise druga kopija.
        bool aliased = false;
        for (std::size_t i = 0; i < entry.aliasCount && !aliased; ++i) {
            const CurveSlot& other = kCurveSlots[entry.aliasOf[i]];
            const auto* otherSegment = findSegment(curve, other.direction, other.kind);
            if (segmentsEqual(segment, otherSegment)) {
                bank.setWideBits(slot.pointerRegister, kCurvePointerMask,
                                 bank.getLsb(other.pointerRegister, 2) & kCurvePointerMask);
                bank.setLsb(slot.lastStepRegister,
                            bank.getLsb(other.lastStepRegister, 3), 3);
                aliased = true;
            }
        }
        if (aliased) {
            continue;
        }

        bufferOffset += static_cast<std::uint32_t>(segment->count * 3);
        bank.setWideBits(slot.pointerRegister, kCurvePointerMask, bufferOffset);
        bank.setLsb(slot.lastStepRegister,
                    static_cast<std::uint32_t>(segment->values[segment->count - 1]), 3);

        for (int i = 0; i < segment->count; ++i) {
            addStep(steps, static_cast<std::uint32_t>(segment->values[i]));
        }
    }

    program.rawLength = steps.size();

    // rts8822.c:3386-3392 - duzina se zaokruzuje navise na visekratnik od 16.
    if ((steps.size() & 0x0F) != 0) {
        const std::size_t padded = ((steps.size() >> 4) + 1) << 4;
        steps.resize(padded, std::byte{0});
    }

    program.accelerationStepCount = accCount;
    return program;
}

#if G2710_MOTOR_PATH_COMPILED

Status uploadMotorSteps(Rts8822& chip, Gpio& gpio, Dma& dma,
                        const MotorStepProgram& program,
                        std::uint16_t stepBufferBase) {
    if (program.steps.empty()) {
        return fail(ErrorCode::InvalidArgument, "uploadMotorSteps: prazan program");
    }

    if (const Status s = chip.warmReset(); !s) {
        return s;
    }
    if (const Status s = gpio.setLock(true); !s) {
        return s;
    }

    const Status written = dma.write(0x0000, stepBufferBase, program.steps);

    // Otkljucaj i kada je upis pao - ostaviti cip zakljucanim bilo bi gore od
    // same greske.
    const Status unlocked = gpio.setLock(false);

    if (!written) {
        return written;
    }
    return unlocked;
}

#endif  // G2710_MOTOR_PATH_COMPILED

}  // namespace g2710::rts8822

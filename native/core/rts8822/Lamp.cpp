#include "Lamp.h"

#include "G2710Profile.generated.h"

#include <array>
#include <cmath>

namespace g2710::rts8822 {
namespace {

// hp3800_fixedpwm daje 0 za sve scantype-ove, uz podrazumevano 0x16 kada red
// nije nadjen (rts8822.c: hp3800_fixedpwm, `SANE_Int a, rst = 0x16`).
int fixedPwmFor(LampKind kind) {
    const int scanType = kind == LampKind::Flatbed ? 0 : 1;  // ST_NORMAL / ST_TA
    for (const auto& row : profile::kFixedPwm) {
        if (row.usb == 1) {  // USB20
            return row.pwm[scanType];
        }
    }
    return profile::kFixedPwmDefault;
}

}  // namespace

const char* toString(LampKind kind) noexcept {
    switch (kind) {
        case LampKind::Flatbed: return "flatbed";
        case LampKind::Tma:     return "TMA";
    }
    return "?";
}

Status Lamp::setLamp(LampKind kind, bool on) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "lamp.setLamp"); !allowed) {
        return allowed;
    }

    // Referenca ucitava ceo bank pre izmene; mi citamo samo bajtove koje
    // stvarno menjamo, sto daje istu vrednost uz tri transfera umesto 1818
    // bajtova. Bitovi van maski se cuvaju, pa je rezultat identican.
    auto status = registers_.readByte(reg::kLampStatus);
    if (!status) {
        return status.error();
    }

    // BL-03A: svaka lampa ima svoj bit i oba postuju `on`.
    std::uint8_t value = status.value();
    value = bitsetValue(value, reg::kLampStatusTmaBit,
                        (kind == LampKind::Tma && on) ? 1 : 0);
    value = bitsetValue(value, reg::kLampStatusFlbBit,
                        (kind == LampKind::Flatbed && on) ? 1 : 0);

    if (const Status s = registers_.writeByte(reg::kLampStatus, value); !s) {
        return s;
    }

    // rts8822.c:10793 - referenca ovde spava 200 ms pre drugog upisa.
    clock_.sleepFor(kLampSwitchSettle);

    // Bit izbora lampe. Referenca ga upisuje u Regs[0x155]; Lamp_Status_Get
    // ga u BL-03A grani cita iz Regs[0x154]. Vidi D2 u
    // docs/REFERENCE-DEFECTS.md - razlika se NE zataskava.
    auto select = registers_.readByte(reg::kLampSelect);
    if (!select) {
        return select.error();
    }
    const std::uint8_t updated = bitsetValue(select.value(), reg::kLampSelectTmaBit,
                                             kind != LampKind::Flatbed ? 1 : 0);
    return registers_.writeByte(reg::kLampSelect, updated);
}

Status Lamp::setupPwm(LampKind kind) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "lamp.setupPwm"); !allowed) {
        return allowed;
    }

    const auto target = static_cast<std::uint8_t>(fixedPwmFor(kind) & reg::kLampPwmDutyMask);

    auto current = registers_.readByte(reg::kLampPwm);
    if (!current) {
        return current.error();
    }
    const auto currentDuty = static_cast<std::uint8_t>(current.value() & reg::kLampPwmDutyMask);

    // Referenca upisuje samo ako se razlikuje - stedi dva transfera po
    // pozivu, a warmup ga poziva cesto.
    if (currentDuty == target) {
        return ok();
    }

    const std::uint8_t updated = bitsetValue(current.value(), reg::kLampPwmDutyMask, target);
    return registers_.writeByte(reg::kLampPwm, updated);
}

Result<WarmupResult> Lamp::waitUntilStable(const std::function<Result<double>()>& readLevel,
                                           const StabilityCriterion& criterion) {
    if (const Status allowed = gate_.require(SafetyLevel::Lamp, "lamp.waitUntilStable");
        !allowed) {
        return allowed.error();
    }
    if (!readLevel) {
        return fail(ErrorCode::InvalidArgument, "waitUntilStable: nedostaje readLevel");
    }

    // rts8822.c:11157 - `diff` se skalira sa 0.01 pre poredjenja.
    const double threshold = criterion.diff * 0.01;

    const Instant start = clock_.now();
    const Instant deadline = start + criterion.totalTime;

    WarmupResult result;
    double previous = 0.0;

    while (clock_.now() <= deadline) {
        auto level = readLevel();
        if (!level) {
            return level.error();
        }
        ++result.samples;
        result.lastLevel = level.value();

        if (std::abs(level.value() - previous) < threshold) {
            result.stabilised = true;
            break;
        }
        previous = level.value();

        clock_.sleepFor(criterion.interval);
    }

    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_.now() - start);
    return result;
}

Result<WarmupResult> Lamp::warmUp(LampKind kind,
                                  const std::function<Result<double>()>& readLevel,
                                  const StabilityCriterion& criterion) {
    if (const Status s = setLamp(kind, true); !s) {
        return s.error();
    }
    if (const Status s = setupPwm(kind); !s) {
        return s.error();
    }

    // Slepo cekanje pre nego sto merenje pocne (overdrive_flb / overdrive_ta).
    clock_.sleepFor(kind == LampKind::Flatbed ? kOverdriveFlatbed : kOverdriveTma);

    return waitUntilStable(readLevel, criterion);
}

}  // namespace g2710::rts8822

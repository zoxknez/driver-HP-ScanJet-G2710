#include "VirtualLamp.h"

#include <algorithm>
#include <cmath>

namespace g2710::sim {

const char* toString(LampKind kind) noexcept {
    switch (kind) {
        case LampKind::Flatbed: return "flatbed";
        case LampKind::Tma:     return "TMA";
    }
    return "?";
}

void VirtualLamp::turnOn() noexcept {
    if (!on_) {
        on_ = true;
        onTimeMs_ = 0;
    }
}

void VirtualLamp::turnOff() noexcept {
    on_ = false;
    onTimeMs_ = 0;
}

void VirtualLamp::advance(std::uint32_t milliseconds) noexcept {
    if (on_) {
        onTimeMs_ += milliseconds;
    }
}

double VirtualLamp::warmFraction() const noexcept {
    if (!on_) {
        return 0.0;
    }
    if (profile_.timeConstantMs <= 0.0) {
        return 1.0;
    }
    // Eksponencijalno prilazenje, kao kod pravog luminiscentnog izvora.
    return 1.0 - std::exp(-static_cast<double>(onTimeMs_) / profile_.timeConstantMs);
}

double VirtualLamp::level() const noexcept {
    if (!on_) {
        return 0.0;
    }

    const double cold = profile_.stableLevel * profile_.coldFraction;
    const double warmed = cold + (profile_.stableLevel - cold) * warmFraction();

    // PWM skalira nivo; pun opseg je 0x3F.
    const double dutyScale = static_cast<double>(duty_) / 63.0;
    return std::clamp(warmed * dutyScale, 0.0, 65535.0);
}

}  // namespace g2710::sim

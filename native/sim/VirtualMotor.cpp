#include "VirtualMotor.h"

#include <algorithm>

namespace g2710::sim {

const char* toString(MotorOutcome outcome) noexcept {
    switch (outcome) {
        case MotorOutcome::Moved:       return "Moved";
        case MotorOutcome::HitHome:     return "HitHome";
        case MotorOutcome::HitFarLimit: return "HitFarLimit";
        case MotorOutcome::Stalled:     return "Stalled";
        case MotorOutcome::Disabled:    return "Disabled";
    }
    return "?";
}

void VirtualMotor::teleportTo(int position) noexcept {
    position_ = std::clamp(position, 0, geometry_.maxPositionSteps());
}

MotorOutcome VirtualMotor::step(int steps) noexcept {
    if (!enabled_) {
        return MotorOutcome::Disabled;
    }
    if (steps <= 0) {
        return MotorOutcome::Moved;
    }

    int remaining = steps;

    if (stallAfter_ >= 0) {
        // Zastoj: prihvatimo koliko je moglo pre zaustavljanja, ostatak ne.
        remaining = std::min(remaining, stallAfter_);
        stallAfter_ = -1;
    }

    const int limit = geometry_.maxPositionSteps();
    const int sign = direction_ == MotorDirection::Forward ? 1 : -1;
    const int target = position_ + sign * remaining;

    MotorOutcome outcome = MotorOutcome::Moved;
    int actual = target;

    if (target < 0) {
        actual = 0;
        outcome = MotorOutcome::HitHome;
    } else if (target > limit) {
        actual = limit;
        outcome = MotorOutcome::HitFarLimit;
        ++farLimitHits_;
    }

    totalSteps_ += std::abs(actual - position_);
    position_ = actual;

    // Zastoj se prijavljuje tek ako granica nije vec presekla kretanje -
    // udarac u kraj staze je ozbiljniji podatak.
    if (outcome == MotorOutcome::Moved && remaining < steps) {
        return MotorOutcome::Stalled;
    }
    return outcome;
}

}  // namespace g2710::sim

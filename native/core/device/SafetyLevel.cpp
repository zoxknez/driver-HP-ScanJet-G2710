#include "SafetyLevel.h"

namespace g2710 {

const char* toString(SafetyLevel level) noexcept {
    switch (level) {
        case SafetyLevel::ReadOnly: return "1 (read-only)";
        case SafetyLevel::Lamp:     return "2 (lamp)";
        case SafetyLevel::Motor:    return "3 (motor/home)";
        case SafetyLevel::Acquire:  return "4 (ccd acquire)";
        case SafetyLevel::FullScan: return "5 (full scan)";
    }
    return "?";
}

Status SafetyGate::require(SafetyLevel needed, const char* operation) const noexcept {
    if (allows(needed)) {
        return ok();
    }
    // Hard error, ne upozorenje. Pozivalac ne sme nastaviti.
    return fail(ErrorCode::SafetyViolation, operation);
}

}  // namespace g2710

#include "Error.h"

namespace g2710 {

const char* toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:                 return "Ok";
        case ErrorCode::NotOpen:            return "NotOpen";
        case ErrorCode::Timeout:            return "Timeout";
        case ErrorCode::ShortTransfer:      return "ShortTransfer";
        case ErrorCode::Stalled:            return "Stalled";
        case ErrorCode::Cancelled:          return "Cancelled";
        case ErrorCode::TransportLost:      return "TransportLost";
        case ErrorCode::DeviceNotFound:     return "DeviceNotFound";
        case ErrorCode::DeviceError:        return "DeviceError";
        case ErrorCode::Busy:               return "Busy";
        case ErrorCode::SafetyViolation:    return "SafetyViolation";
        case ErrorCode::NotImplementedIn10: return "NotImplementedIn10";
        case ErrorCode::InvalidArgument:    return "InvalidArgument";
        case ErrorCode::InvalidState:       return "InvalidState";
        case ErrorCode::Internal:           return "Internal";
    }
    return "Unknown";
}

}  // namespace g2710

#include "FailureInjector.h"

#include <algorithm>

namespace g2710::sim {

const char* toString(TransferKind kind) noexcept {
    switch (kind) {
        case TransferKind::ControlIn:  return "ControlIn";
        case TransferKind::ControlOut: return "ControlOut";
        case TransferKind::BulkRead:   return "BulkRead";
        case TransferKind::BulkWrite:  return "BulkWrite";
        case TransferKind::Event:      return "Event";
        case TransferKind::Any:        return "Any";
    }
    return "?";
}

bool FailureInjector::matches(TransferKind scheduled, TransferKind actual) noexcept {
    return scheduled == TransferKind::Any || scheduled == actual;
}

std::optional<ErrorCode> FailureInjector::nextFault(TransferKind kind) {
    for (auto it = schedule_.begin(); it != schedule_.end(); ++it) {
        if (!matches(it->kind, kind)) {
            continue;
        }

        if (it->afterOperations > 0) {
            --it->afterOperations;
            continue;
        }

        const ErrorCode error = it->error;
        ++fired_;

        if (it->repeat > 0) {
            --it->repeat;
            if (it->repeat == 0) {
                schedule_.erase(it);
            }
        }
        // repeat < 0 znaci trajno - unos ostaje.

        return error;
    }
    return std::nullopt;
}

}  // namespace g2710::sim

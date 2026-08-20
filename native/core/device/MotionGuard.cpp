#include "MotionGuard.h"

#include <algorithm>

namespace g2710 {

const char* toString(MotionDirection direction) noexcept {
    switch (direction) {
        case MotionDirection::TowardHome:   return "TowardHome";
        case MotionDirection::AwayFromHome: return "AwayFromHome";
    }
    return "?";
}

const char* toString(MotionStatus status) noexcept {
    switch (status) {
        case MotionStatus::Completed:        return "Completed";
        case MotionStatus::StoppedAtHome:    return "StoppedAtHome";
        case MotionStatus::Cancelled:        return "Cancelled";
        case MotionStatus::DeadlineExceeded: return "DeadlineExceeded";
        case MotionStatus::LimitExceeded:    return "LimitExceeded";
        case MotionStatus::TransportLost:    return "TransportLost";
        case MotionStatus::Refused:          return "Refused";
    }
    return "?";
}

#if G2710_MOTOR_PATH_COMPILED

void MotionGuard::setStepChunk(int steps) noexcept {
    stepChunk_ = std::max(1, steps);
}

Result<MotionResult> MotionGuard::refuse(const char* reason) const {
    return fail(ErrorCode::InvalidArgument, reason);
}

void MotionGuard::attemptEmergencyStop(const MotionHooks& hooks,
                                       MotionResult& result) const {
    // Ovo je mesto na kome se lako slaze. Ako veze nema, STOP se NE moze
    // poslati, pa se ne sme ni prijaviti kao pokusan.
    if (!hooks.isTransportAlive || !hooks.isTransportAlive()) {
        result.emergencyStopAttempted = false;
        result.emergencyStopSucceeded = false;
        return;
    }
    if (!hooks.emergencyStop) {
        return;
    }

    result.emergencyStopAttempted = true;
    result.emergencyStopSucceeded = hooks.emergencyStop().hasValue();
}

Result<MotionResult> MotionGuard::run(const MotionRequest& request,
                                      const MotionHooks& hooks,
                                      const CancellationToken& token) {
    // --- provere zahteva, pre nego sto se bilo sta pomeri ---------------

    if (const Status allowed = gate_.require(SafetyLevel::Motor, request.operation);
        !allowed) {
        return allowed.error();
    }
    if (!hooks.step) {
        return refuse("MotionGuard: nedostaje step hook");
    }
    if (request.expectedSteps <= 0) {
        return refuse("MotionGuard: expectedSteps mora biti pozitivan");
    }
    if (request.maximumSteps < request.expectedSteps) {
        return refuse("MotionGuard: maximumSteps je manji od expectedSteps");
    }
    if (request.deadline.count() <= 0) {
        // Kretanje bez roka nad udaljenim uredjajem je upravo ono sto ovaj
        // modul postoji da spreci.
        return refuse("MotionGuard: rok je obavezan");
    }
    if (!machine_.allowsMotion()) {
        return fail(ErrorCode::InvalidState, request.operation);
    }

    MotionResult result;
    result.positionKnown = position_.isKnown();

    const Instant start = clock_.now();
    const Instant limit = start + request.deadline;
    const int sign = request.direction == MotionDirection::TowardHome ? -1 : 1;

    // --- petlja kretanja ------------------------------------------------

    while (result.stepsTaken < request.expectedSteps) {
        if (token.isCancelled()) {
            result.status = MotionStatus::Cancelled;
            attemptEmergencyStop(hooks, result);
            position_.invalidate();
            result.positionKnown = false;
            return result;
        }

        if (clock_.now() >= limit) {
            result.status = MotionStatus::DeadlineExceeded;
            attemptEmergencyStop(hooks, result);
            position_.invalidate();
            result.positionKnown = false;
            return result;
        }

        if (result.stepsTaken >= request.maximumSteps) {
            result.status = MotionStatus::LimitExceeded;
            attemptEmergencyStop(hooks, result);
            position_.invalidate();
            result.positionKnown = false;
            return result;
        }

        // Home senzor se proverava PRE koraka. Kod koji korakne pa proveri
        // prelazi senzor za jedan komad.
        if (hooks.isAtHome && request.direction == MotionDirection::TowardHome) {
            auto atHome = hooks.isAtHome();
            if (!atHome) {
                if (atHome.error().code == ErrorCode::TransportLost) {
                    machine_.onTransportLost();
                    position_.invalidate();
                    result.status = MotionStatus::TransportLost;
                    result.positionKnown = false;
                    return result;
                }
                return atHome.error();
            }
            if (atHome.value()) {
                result.status = MotionStatus::StoppedAtHome;
                if (request.homeIsGoal) {
                    // HOME je jedina operacija posle koje pozicija postaje
                    // poznata bez pretpostavki - senzor je rekao gde smo.
                    position_.setKnown(0);
                    result.positionKnown = true;
                } else {
                    position_.invalidate();
                    result.positionKnown = false;
                }
                return result;
            }
        }

        const int remaining = std::min(request.expectedSteps - result.stepsTaken,
                                       request.maximumSteps - result.stepsTaken);
        const int chunk = std::min(stepChunk_, remaining);

        auto stepped = hooks.step(chunk);
        if (!stepped) {
            if (stepped.error().code == ErrorCode::TransportLost) {
                // Veza je nestala USRED kretanja. Ne mozemo poslati STOP i ne
                // znamo gde je glava stala.
                machine_.onTransportLost();
                position_.invalidate();
                result.status = MotionStatus::TransportLost;
                result.positionKnown = false;
                return result;
            }
            machine_.onFault();
            position_.invalidate();
            result.positionKnown = false;
            return stepped.error();
        }

        const int done = stepped.value();
        result.stepsTaken += done;
        position_.advance(sign * done);

        if (done < chunk) {
            // Motor nije napravio sve korake - zastoj. Pozicija je od ovog
            // trenutka nepouzdana.
            result.status = MotionStatus::LimitExceeded;
            attemptEmergencyStop(hooks, result);
            position_.invalidate();
            result.positionKnown = false;
            return result;
        }
    }

    result.status = MotionStatus::Completed;
    result.positionKnown = position_.isKnown();
    return result;
}

#endif  // G2710_MOTOR_PATH_COMPILED

}  // namespace g2710
